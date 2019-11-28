#include "Network.h"
#include "IocpServer.h"
#include <algorithm>
#include <string>

using std::string;
#pragma comment(lib, "WS2_32.lib")

IocpServer::IocpServer(short listenPort, int maxConnectionCount) :
	m_bIsShutdown(false), m_listenPort(listenPort)
	, m_nMaxConnClientCnt(maxConnectionCount)
	, m_hIOCompletionPort(nullptr)
	, m_hExitEvent(nullptr)
	, m_nWorkerCnt(0)
	, m_nConnClientCnt(0)
	, m_lpfnAcceptEx(nullptr)
	, m_lpfnGetAcceptExSockAddrs(nullptr)
	, m_pListenCtx(nullptr)
	, acceptPostCount(0)
{
	InitializeCriticalSection(&m_csLog);
	errorCount = 0;
	if (!Network::init())
	{
		showMessage("��ʼ��WinSock 2.2ʧ�ܣ�");
	}
}

IocpServer::~IocpServer(void)
{
	// ȷ����Դ�����ͷ�
	this->Stop();
	Network::unInit();
	showMessage("~IocpServer()");
	DeleteCriticalSection(&m_csLog);
}

///////////////////////////////////////////////////////////////////
// �������̣߳� ΪIOCP�������Ĺ������߳�
// Ҳ����ÿ����ɶ˿��ϳ�����������ݰ����ͽ�֮ȡ�������д������߳�
///////////////////////////////////////////////////////////////////
/*********************************************************************
*�������ܣ��̺߳���������GetQueuedCompletionStatus����������д�����
*����������lpParam��THREADPARAMS_WORKER����ָ�룻
*����˵����GetQueuedCompletionStatus��ȷ����ʱ��ʾĳ�����Ѿ���ɣ�
	�ڶ�������lpNumberOfBytes��ʾ�����׽��ִ�����ֽ�����
����lpCompletionKey��lpOverlapped������Ҫ����Ϣ�����ѯMSDN�ĵ���
*********************************************************************/
DWORD WINAPI IocpServer::_WorkerThread(LPVOID lpParam)
{
	IocpServer* pIocpModel = (IocpServer*)lpParam;
	pIocpModel->showMessage("�������̣߳�ID:%d", GetCurrentThreadId());
	//ѭ����������ֱ�����յ�Shutdown��ϢΪֹ
	while (WAIT_OBJECT_0 != WaitForSingleObject(pIocpModel->m_hExitEvent, 0))
	{
		DWORD dwBytesTransfered = 0;
		OVERLAPPED* pOverlapped = nullptr;
		SocketContext* pSoContext = nullptr;
		const BOOL bRet = GetQueuedCompletionStatus(pIocpModel->m_hIOCompletionPort,
			&dwBytesTransfered, (PULONG_PTR)&pSoContext, &pOverlapped, INFINITE);		
		IoContext* pIoContext = CONTAINING_RECORD(pOverlapped, 
			IoContext, m_Overlapped); // ��ȡ����Ĳ���
		//����EXIT_CODE�˳���־����ֱ���˳�
		if (EXIT_THREAD == (DWORD)pSoContext)
		{
			break;
		}
		if (!bRet)
		{	//����ֵΪfalse����ʾ����
			const DWORD dwErr = GetLastError();
			// ��ʾһ����ʾ��Ϣ
			if (!pIocpModel->HandleError(pSoContext, dwErr))
			{
				break;
			}
			continue;
		}
		else
		{
			// �ж��Ƿ��пͻ��˶Ͽ���
			if ((0 == dwBytesTransfered)
				&& (PostType::RECV == pIoContext->m_PostType
					|| PostType::SEND == pIoContext->m_PostType))
			{
				pIocpModel->OnConnectionClosed(pSoContext);
				//pIocpModel->_ShowMessage("�ͻ��� %s:%d �Ͽ�����",
				//	inet_ntoa(pSoContext->m_ClientAddr.sin_addr),
				//	ntohs(pSoContext->m_ClientAddr.sin_port));
				// �ͷŵ���Ӧ����Դ
				pIocpModel->_DoClose(pSoContext);
				continue;
			}
			else
			{
				switch (pIoContext->m_PostType)
				{
				case PostType::ACCEPT:
				{
					// Ϊ�����Ӵ���ɶ��ԣ�������ר�ŵ�_DoAccept�������д�����������
					pIoContext->m_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoAccept(pSoContext, pIoContext);
				}
				break;

				case PostType::RECV:
				{
					// Ϊ�����Ӵ���ɶ��ԣ�������ר�ŵ�_DoRecv�������д�����������
					pIoContext->m_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoRecv(pSoContext, pIoContext);
				}
				break;

				// �����Թ���д�ˣ�Ҫ������̫���ˣ����������⣬Send�������������һЩ
				case PostType::SEND:
				{
					pIoContext->m_nSentBytes += dwBytesTransfered;
					pIocpModel->_DoSend(pSoContext, pIoContext);
				}
				break;
				default:
					// ��Ӧ��ִ�е�����
					pIocpModel->showMessage("_WorkThread�е�m_OpType �����쳣");
					break;
				} //switch
			}//if
		}//if
	}//while
	pIocpModel->showMessage("�������߳��˳���ID:%d",
		GetCurrentThreadId());
	return 0;
}

//�������ܣ�����������
bool IocpServer::Start()
{
	// ��ʼ���̻߳�����
	InitializeCriticalSection(&m_csClientList);
	// ����ϵͳ�˳����¼�֪ͨ
	m_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	// ��ʼ��IOCP
	if (!_InitializeIOCP())
	{
		this->showMessage("��ʼ��IOCPʧ�ܣ�");
		return false;
	}
	else
	{
		this->showMessage("��ʼ��IOCP��ϣ�");
	}
	// ��ʼ��Socket
	if (!_InitializeListenSocket())
	{
		this->showMessage("����Socket��ʼ��ʧ�ܣ�");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->showMessage("����Socket��ʼ�����");
	}
	this->showMessage("ϵͳ׼���������Ⱥ�����...");
	return true;
}

////////////////////////////////////////////////////////////////////
//	��ʼ����ϵͳ�˳���Ϣ���˳���ɶ˿ں��߳���Դ
bool IocpServer::Stop()
{
	if (m_pListenCtx != nullptr
		&& m_pListenCtx->m_Socket != INVALID_SOCKET)
	{
		// ����ر���Ϣ֪ͨ
		SetEvent(m_hExitEvent);
		for (int i = 0; i < m_nWorkerCnt; i++)
		{
			// ֪ͨ���е���ɶ˿ڲ����˳�
			PostQueuedCompletionStatus(m_hIOCompletionPort,
				0, (DWORD)EXIT_THREAD, NULL);
		}
		// �ȴ����еĿͻ�����Դ�˳�
		WaitForMultipleObjects(m_nWorkerCnt, 
			m_hWorkerThreads.data(),
			TRUE, INFINITE);
		// ����ͻ����б���Ϣ
		this->_ClearContextList();
		// �ͷ�������Դ
		this->_DeInitialize();
		this->showMessage("ֹͣ����");
	}
	else
	{
		m_pListenCtx = nullptr;
	}
	return true;
}

bool IocpServer::SendData(SocketContext* pSoContext, char* data, int size)
{
	this->showMessage("SendData(): s=%p d=%p", pSoContext, data);
	if (!pSoContext || !data || size <= 0 || size > MAX_BUFFER_LEN)
	{
		this->showMessage("SendData()����������");
		return false;
	}
	//Ͷ��WSASend���󣬷�������
	IoContext* pNewIoContext = pSoContext->GetNewIoContext();
	pNewIoContext->m_acceptSocket = pSoContext->m_Socket;
	pNewIoContext->m_PostType = PostType::SEND;
	pNewIoContext->m_nTotalBytes = size;
	pNewIoContext->m_wsaBuf.len = size;
	memcpy(pNewIoContext->m_wsaBuf.buf, data, size);
	if (!this->_PostSend(pSoContext, pNewIoContext))
	{// ����RELEASE_POINTER��ʧ��ʱ���Ѿ�release��
		// RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	return true;
}

bool IocpServer::SendData(SocketContext* pSoContext, IoContext* pIoContext)
{
	return this->_PostSend(pSoContext, pIoContext);
}

bool IocpServer::RecvData(SocketContext* pSoContext, IoContext* pIoContext)
{
	return this->_PostRecv(pSoContext, pIoContext);
}

////////////////////////////////
// ��ʼ����ɶ˿�
bool IocpServer::_InitializeIOCP()
{
	this->showMessage("��ʼ��IOCP-InitializeIOCP()");
	//If this parameter is zero, the system allows as many 
	//concurrently running threads as there are processors in the system.
	//����˲���Ϊ�㣬��ϵͳ�����Ĳ��������߳�������ϵͳ�еĴ�����������ͬ��
	m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
		nullptr, 0, 0); //NumberOfConcurrentThreads
	if (nullptr == m_hIOCompletionPort)
	{
		this->showMessage("������ɶ˿�ʧ�ܣ��������: %d!", WSAGetLastError());
		return false;
	}
	// ���ݱ����еĴ�����������������Ӧ���߳���
	m_nWorkerCnt = WORKER_THREADS_PER_PROCESSOR * _GetNumOfProcessors();
	// ���ݼ�����������������������߳�
	DWORD nThreadID = 0;
	for (int i = 0; i < m_nWorkerCnt; i++)
	{
		HANDLE hWorker = ::CreateThread(0, 0, _WorkerThread,
			(void*)this, 0, &nThreadID);
		m_hWorkerThreads.emplace_back(hWorker);
	}
	this->showMessage("����WorkerThread %d ��", m_nWorkerCnt);
	return true;
}

/////////////////////////////////////////////////////////////////
// ��ʼ��Socket
bool IocpServer::_InitializeListenSocket()
{
	this->showMessage("��ʼ��Socket-InitializeListenSocket()");
	// �������ڼ�����Socket����Ϣ
	m_pListenCtx = new SocketContext;
	// ��Ҫʹ���ص�IO�������ʹ��WSASocket������Socket���ſ���֧���ص�IO����
	m_pListenCtx->m_Socket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_pListenCtx->m_Socket)
	{
		this->showMessage("WSASocket() ʧ�ܣ�err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->showMessage("���� WSASocket() ���");
	}

	// ��Listen Socket������ɶ˿���
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenCtx->m_Socket,
		m_hIOCompletionPort, (DWORD)m_pListenCtx, 0))
	{
		this->showMessage("��ʧ�ܣ�err=%d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->showMessage("����ɶ˿� ���");
	}

	// ����ַ��Ϣ
	// ��������ַ��Ϣ�����ڰ�Socket
	sockaddr_in serverAddress;
	ZeroMemory((char*)&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	// ������԰��κο��õ�IP��ַ�����߰�һ��ָ����IP��ַ 
	// ServerAddress.sin_addr.s_addr = inet_addr(m_strIP.c_str());
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(m_listenPort);

	// �󶨵�ַ�Ͷ˿�
	if (SOCKET_ERROR == bind(m_pListenCtx->m_Socket,
		(sockaddr*)&serverAddress, sizeof(serverAddress)))
	{
		this->showMessage("bind()����ִ�д���");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->showMessage("bind() ���");
	}

	// ��ʼ���м���
	if (SOCKET_ERROR == listen(m_pListenCtx->m_Socket, MAX_LISTEN_SOCKET))
	{
		this->showMessage("listen()����, err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->showMessage("listen() ���");
	}

	// ʹ��AcceptEx��������Ϊ���������WinSock2�淶֮���΢�������ṩ����չ����
	// ������Ҫ�����ȡһ�º�����ָ�룬��ȡAcceptEx����ָ��
	DWORD dwBytes = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(m_pListenCtx->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx), &dwBytes, NULL, NULL))
	{
		this->showMessage("WSAIoctl δ�ܻ�ȡAcceptEx����ָ�롣�������: %d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	// ��ȡGetAcceptExSockAddrs����ָ�룬Ҳ��ͬ��
	if (SOCKET_ERROR == WSAIoctl(m_pListenCtx->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs), &m_lpfnGetAcceptExSockAddrs,
		sizeof(m_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		this->showMessage("WSAIoctl δ�ܻ�ȡGuidGetAcceptExSockAddrs����ָ�롣"
			"�������: %d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	// ΪAcceptEx ׼��������Ȼ��Ͷ��AcceptEx I/O����
	// ����10���׽��֣�Ͷ��AcceptEx���󣬼�����10���׽��ֽ���accept������
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		// �½�һ��IO_CONTEXT
		IoContext* pIoContext = m_pListenCtx->GetNewIoContext();
		if (pIoContext && !this->_PostAccept(pIoContext))
		{
			m_pListenCtx->RemoveContext(pIoContext);
			return false;
		}
	}
	this->showMessage("Ͷ�� %d ��AcceptEx�������", MAX_POST_ACCEPT);
	return true;
}

////////////////////////////////////////////////////////////
//	����ͷŵ�������Դ
void IocpServer::_DeInitialize()
{
	//�رչ����߳̾��
	for_each(m_hWorkerThreads.begin(), m_hWorkerThreads.end(),
		[](const HANDLE& h) { CloseHandle(h); });
	// ɾ���ͻ����б��Ļ�����
	DeleteCriticalSection(&m_csClientList);
	// �ر�ϵͳ�˳��¼����
	RELEASE_HANDLE(m_hExitEvent);
	// �ر�IOCP���
	RELEASE_HANDLE(m_hIOCompletionPort);
	// �رռ���Socket
	RELEASE_POINTER(m_pListenCtx);
	this->showMessage("�ͷ���Դ���");
}

//================================================================================
//				 Ͷ����ɶ˿�����
//================================================================================
//////////////////////////////////////////////////////////////////
// Ͷ��Accept����
bool IocpServer::_PostAccept(IoContext* pIoContext)
{
	if (m_pListenCtx == NULL || m_pListenCtx->m_Socket == INVALID_SOCKET)
	{
		throw "_PostAccept,m_pListenContext or m_Socket INVALID!";
	}
	// ׼������
	pIoContext->ResetBuffer();
	pIoContext->m_PostType = PostType::ACCEPT;
	// SOCKET hClient = accept(hSocket, NULL, NULL); //��ͳaccept
	// Ϊ�Ժ�������Ŀͻ�����׼����Socket( ������봫ͳaccept�������� ) 
	pIoContext->m_acceptSocket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == pIoContext->m_acceptSocket)
	{// Ͷ�ݶ��ٴ�ACCEPT���ʹ������ٸ�socket��
		showMessage("��������Accept��Socketʧ�ܣ�err=%d", WSAGetLastError());
		return false;
	}
	//https://docs.microsoft.com/zh-cn/windows/win32/api/mswsock/nf-mswsock-acceptex
	// Ͷ��AcceptEx // �����ջ�����Ϊ0,��AcceptExֱ�ӷ���,��ֹ�ܾ����񹥻�	
	DWORD dwBytes = 0, dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	WSABUF* pWSAbuf = &pIoContext->m_wsaBuf; //����+16,�μ�MSDN
	if (!m_lpfnAcceptEx(m_pListenCtx->m_Socket,
		pIoContext->m_acceptSocket, pWSAbuf->buf,
		0, //pWSAbuf->len - (dwAddrLen * 2),
		dwAddrLen, dwAddrLen, &dwBytes,
		&pIoContext->m_Overlapped))
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			showMessage("Ͷ�� AcceptEx ʧ�ܣ�err=%d", nErr);
			return false;
		}
	}
	InterlockedIncrement(&acceptPostCount);
	return true;
}

////////////////////////////////////////////////////////////
// ���пͻ��������ʱ�򣬽��д���
// �����е㸴�ӣ���Ҫ�ǿ������Ļ����Ϳ����׵��ĵ���....
// �������������Ļ�����ɶ˿ڵĻ������������һ�����

// ��֮��Ҫ֪�����������ListenSocket��Context��������Ҫ����һ�ݳ������������Socket��
// ԭ����Context����Ҫ���������Ͷ����һ��Accept����
/********************************************************************
*�������ܣ��������пͻ��˽��봦����
*����˵����
SocketContext* pSoContext:	����accept������Ӧ���׽��֣����׽�������Ӧ�����ݽṹ��
IoContext* pIoContext:			����accept������Ӧ�����ݽṹ��
DWORD		dwIOSize:			���β�������ʵ�ʴ�����ֽ���
********************************************************************/
#include <mstcpip.h> //tcp_keepalive
bool IocpServer::_DoAccept(SocketContext* pSoContext, IoContext* pIoContext)
{//�����pSoContext��listenSocketContext
	InterlockedIncrement(&m_nConnClientCnt);
	InterlockedDecrement(&acceptPostCount);
#if 1 //�޷��õ��Է���IP��ַ�أ�
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	int remoteLen = 0, localLen = 0; //����+16,�μ�MSDN
	this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf,
		0, //pIoContext->m_wsaBuf.len - (dwAddrLen * 2),
		dwAddrLen, dwAddrLen, (LPSOCKADDR*)&localAddr,
		&localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);

	// 2. Ϊ�����ӽ���һ��SocketContext 
	SocketContext* pNewSocketContext = new SocketContext;
	//���뵽ContextList��ȥ(��Ҫͳһ�����������ͷ���Դ)
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->m_Socket = pIoContext->m_acceptSocket;
	memcpy(&(pNewSocketContext->m_ClientAddr), clientAddr, remoteLen);

	// 3. ��listenSocketContext��IOContext ���ú����Ͷ��AcceptEx
	if (!_PostAccept(pIoContext))
	{
		pSoContext->RemoveContext(pIoContext);
	}

	// 4. ����socket����ɶ˿ڰ�
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{//����RELEASE_POINTER��ʧ��ʱ���Ѿ�release��
		// RELEASE_POINTER(pNewSocketContext);
		return false;
	}

	// ������tcp_keepalive
	tcp_keepalive alive_in = { 0 }, alive_out = { 0 };
	// 60s  �೤ʱ�䣨 ms ��û�����ݾͿ�ʼ send ������
	alive_in.keepalivetime = 1000 * 60; //1����
	// 10s  ÿ���೤ʱ�䣨 ms �� send һ��������
	alive_in.keepaliveinterval = 1000 * 10; //10s
	alive_in.onoff = TRUE;
	DWORD lpcbBytesReturned = 0;
	if (SOCKET_ERROR == WSAIoctl(pNewSocketContext->m_Socket,
		SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in), &alive_out,
		sizeof(alive_out), &lpcbBytesReturned, NULL, NULL))
	{
		showMessage("WSAIoctl() failed: %d\n", WSAGetLastError());
	}
	OnConnectionAccepted(pNewSocketContext);

	// 5. ����recv���������ioContext���������ӵ�socket��Ͷ��recv����
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	if (pNewIoContext != NULL)
	{//���ɹ�������ô����
		pNewIoContext->m_PostType = PostType::RECV;
		pNewIoContext->m_acceptSocket = pNewSocketContext->m_Socket;
		// Ͷ��recv����
		return _PostRecv(pNewSocketContext, pNewIoContext);
	}
	else
	{
		_DoClose(pNewSocketContext);
		return false;
	}
#else //ò���������� �Ƿ�WithData
	if (pIoContext->m_nTotalBytes > 0)
	{
		//�ͻ�����ʱ����һ�ν���dwIOSize�ֽ�����
		_DoFirstRecvWithData(pIoContext);
	}
	else
	{
		//�ͻ��˽���ʱ��û�з������ݣ���Ͷ��WSARecv���󣬽�������
		_DoFirstRecvWithoutData(pIoContext);
	}
	// 5. ʹ�����֮�󣬰�Listen Socket���Ǹ�IoContext���ã�Ȼ��׼��Ͷ���µ�AcceptEx
	return this->_PostAccept(pIoContext);
#endif
}

/*************************************************************
*�������ܣ�AcceptEx���տͻ����ӳɹ������տͻ���һ�η��͵����ݣ���Ͷ��WSASend����
*����������IoContext* pIoContext:	���ڼ����׽����ϵĲ���
**************************************************************/
bool IocpServer::_DoFirstRecvWithData(IoContext* pIoContext)
{
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	int remoteLen, localLen, addrLen = sizeof(SOCKADDR_IN);
	///////////////////////////////////////////////////////////////////////////
	// 1. ����ȡ������ͻ��˵ĵ�ַ��Ϣ
	// ��� m_lpfnGetAcceptExSockAddrs �����˰�~~~~~~
	// ��������ȡ�ÿͻ��˺ͱ��ض˵ĵ�ַ��Ϣ������˳��ȡ����һ�����ݣ���ǿ����...
	this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf,
		pIoContext->m_wsaBuf.len - ((addrLen + 16) * 2),
		addrLen + 16, addrLen + 16, (LPSOCKADDR*)&localAddr,
		&localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);
	// ��ʾ�ͻ�����Ϣ
	this->showMessage("�ͻ��� %s:%d ������!", inet_ntoa(clientAddr->sin_addr),
		ntohs(clientAddr->sin_port));
	this->showMessage("�յ� %s:%d ��Ϣ��%s", inet_ntoa(clientAddr->sin_addr),
		ntohs(clientAddr->sin_port), pIoContext->m_wsaBuf.buf);

	////////////////////////////////////////////////////////////////////////////////
	// 2. ������Ҫע�⣬���ﴫ��������ListenSocket�ϵ�Context��
	// ���Context���ǻ���Ҫ���ڼ�����һ�����ӣ������һ���Ҫ��ListenSocket
	//	�ϵ�Context���Ƴ���һ�ݣ�Ϊ�������Socket�½�һ��SocketContext
	//	Ϊ�½�����׽Ӵ���SocketContext���������׽��ְ󶨵���ɶ˿�
	SocketContext* pNewSocketContext = new SocketContext;
	this->showMessage("pNewSocketContext=%p", pNewSocketContext);
	//���뵽ContextList��ȥ(��Ҫͳһ�����������ͷ���Դ)
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->m_Socket = pIoContext->m_acceptSocket;
	memcpy(&(pNewSocketContext->m_ClientAddr), clientAddr, remoteLen);
	// 3. �����׽��ְ󶨵���ɶ˿�
	// ����������ϣ������Socket����ɶ˿ڰ�(��Ҳ��һ���ؼ�����)
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{//����RELEASE_POINTER��ʧ��ʱ���Ѿ�release��
		// RELEASE_POINTER(pNewSocketContext);
		return false;
	}

	// 4. ���Ͷ�ݳɹ�����ô�Ͱ������Ч�Ŀͻ�����Ϣ��
	this->OnConnectionAccepted(pNewSocketContext);
	// һ��Ҫ�½�һ��IoContext����Ϊԭ�е���ListenSocket��
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	pNewIoContext->m_PostType = PostType::RECV;
	pNewIoContext->m_acceptSocket = pNewSocketContext->m_Socket;
	pNewIoContext->m_nTotalBytes = pIoContext->m_nTotalBytes;
	pNewIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes;
	memcpy(pNewIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.buf,
		pIoContext->m_nTotalBytes);	//�������ݵ�WSASend�����Ĳ���������
	this->_DoRecv(pNewSocketContext, pNewIoContext);

	//////////////////////////////////////////////////////////////////////////////////
	// 5. ʹ�����֮�󣬰�Listen Socket���Ǹ�IoContext���ã�Ȼ��׼��Ͷ���µ�AcceptEx
	// return this->_PostAccept(pIoContext );
	return true;
}

/*************************************************************
*�������ܣ�AcceptEx���տͻ����ӳɹ�����ʱ��δ���յ����ݣ���Ͷ��WSARecv����
*����������IoContext* pIoContext:	���ڼ����׽����ϵĲ���
**************************************************************/
bool IocpServer::_DoFirstRecvWithoutData(IoContext* pIoContext)
{
	//Ϊ�½�����׽��ִ���SocketContext�ṹ�����󶨵���ɶ˿�
	// 1. ����ȡ������ͻ��˵ĵ�ַ��Ϣ
	SOCKADDR_IN clientAddr = { 0 };
	int addrLen = sizeof(clientAddr);
	getpeername(pIoContext->m_acceptSocket, (SOCKADDR*)&clientAddr, &addrLen);
	this->showMessage("�ͻ��� %s:%d ������", inet_ntoa(clientAddr.sin_addr),
		ntohs(clientAddr.sin_port));
	// 2. ������Ҫע�⣬���ﴫ��������ListenSocket�ϵ�Context��
	SocketContext* pNewSocketContext = new SocketContext;
	this->showMessage("pNewSocketContext=%p", pNewSocketContext);
	//���뵽ContextList��ȥ(��Ҫͳһ�����������ͷ���Դ)
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->m_Socket = pIoContext->m_acceptSocket;
	memcpy(&(pNewSocketContext->m_ClientAddr),
		&clientAddr, sizeof(clientAddr));
	// 3. �����׽��ְ󶨵���ɶ˿�
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{//����RELEASE_POINTER��ʧ��ʱ���Ѿ�release��
		//RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	// 4.Ͷ��WSARecv���󣬽�������
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	//��ʱ��AcceptExδ���յ��ͻ��˵�һ�η��͵����ݣ�
	//�����������PostRecv���������Կͻ��˵�����
	if (!this->_PostRecv(pNewSocketContext, pNewIoContext))
	{//����RELEASE_POINTER��ʧ��ʱ���Ѿ�release��
		//RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	// 5.�������̾�ȫ���ɹ�������Ͷ��Accept
	// return this->_PostAccept(pIoContext );
	this->OnConnectionAccepted(pNewSocketContext);
	return true;
}

/*************************************************************
*�������ܣ�Ͷ��WSARecv����
*����������
IoContext* pIoContext:	���ڽ���IO���׽����ϵĽṹ����ҪΪWSARecv������WSASend������
**************************************************************/
bool IocpServer::_PostRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	pIoContext->ResetBuffer();
	pIoContext->m_PostType = PostType::RECV;
	pIoContext->m_nTotalBytes = 0;
	pIoContext->m_nSentBytes = 0;
	// ��ʼ������
	DWORD dwFlags = 0, dwBytes = 0;
	// ��ʼ����ɺ�Ͷ��WSARecv����
	const int nBytesRecv = WSARecv(pIoContext->m_acceptSocket,
		&pIoContext->m_wsaBuf, 1, &dwBytes, &dwFlags,
		&pIoContext->m_Overlapped, NULL);
	// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
	if (SOCKET_ERROR == nBytesRecv)
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			this->showMessage("Ͷ��WSARecvʧ�ܣ�err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

/////////////////////////////////////////////////////////////////
// ���н��յ����ݵ����ʱ�򣬽��д���
bool IocpServer::_DoRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	// �Ȱ���һ�ε�������ʾ���֣�Ȼ�������״̬��������һ��Recv����
	SOCKADDR_IN* clientAddr = &pSoContext->m_ClientAddr;
	//this->_ShowMessage("�յ� %s:%d ��Ϣ��%s", inet_ntoa(clientAddr->sin_addr),
	//	ntohs(clientAddr->sin_port), pIoContext->m_wsaBuf.buf);
	// Ȼ��ʼͶ����һ��WSARecv���� //��������
	//���ﲻӦ��ֱ��PostWrite����ʲôӦ����Ӧ�þ���
	this->OnRecvCompleted(pSoContext, pIoContext);
	//return _PostRecv(pSoContext, pIoContext);
	return true; //����Ӧ�ò㣬������������
}

/*************************************************************
*�������ܣ�Ͷ��WSASend����
*����������
IoContext* pIoContext:	���ڽ���IO���׽����ϵĽṹ����ҪΪWSARecv������WSASend����
*����˵��������PostWrite֮ǰ��Ҫ����pIoContext��m_wsaBuf, m_nTotalBytes, m_nSendBytes��
**************************************************************/
bool IocpServer::_PostSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	// ��ʼ������
	////pIoContext->ResetBuffer(); //�ⲿ����m_wsaBuf
	pIoContext->m_PostType = PostType::SEND;
	pIoContext->m_nTotalBytes = 0;
	pIoContext->m_nSentBytes = 0;
	//Ͷ��WSASend���� -- ��Ҫ�޸�
	const DWORD dwFlags = 0;
	DWORD dwSendNumBytes = 0;
	const int nRet = WSASend(pIoContext->m_acceptSocket,
		&pIoContext->m_wsaBuf, 1, &dwSendNumBytes, dwFlags,
		&pIoContext->m_Overlapped, NULL);
	// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
	if (SOCKET_ERROR == nRet)
	{ //WSAENOTCONN=10057L
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			this->showMessage("Ͷ��WSASendʧ�ܣ�err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

bool IocpServer::_DoSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	if (pIoContext->m_nSentBytes < pIoContext->m_nTotalBytes)
	{
		//����δ�ܷ����꣬������������
		pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer
			+ pIoContext->m_nSentBytes;
		pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes
			- pIoContext->m_nSentBytes;
		return this->_PostSend(pSoContext, pIoContext);
	}
	else
	{
		this->OnSendCompleted(pSoContext, pIoContext);
		//return this->_PostRecv(pSoContext, pIoContext);
		return true; //֪ͨӦ�ò㣬������ϣ�����������
	}
}

bool IocpServer::_DoClose(SocketContext* pSoContext)
{
	//this->_ShowMessage("_DoClose() pSoContext=%p", pSoContext);
	if (pSoContext != m_pListenCtx)
	{// m_pListenContext����vector�У��Ҳ���
		InterlockedDecrement(&m_nConnClientCnt);
		this->_RemoveContext(pSoContext);
		return true;
	}
	InterlockedIncrement(&errorCount);
	return false;
}

/////////////////////////////////////////////////////
// �����(Socket)�󶨵���ɶ˿���
bool IocpServer::_AssociateWithIOCP(SocketContext* pSoContext)
{
	// �����ںͿͻ���ͨ�ŵ�SOCKET�󶨵���ɶ˿���
	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pSoContext->m_Socket,
		m_hIOCompletionPort, (DWORD)pSoContext, 0);
	if (nullptr == hTemp) // ERROR_INVALID_PARAMETER=87L
	{
		this->showMessage("��IOCPʧ�ܡ�err=%d", GetLastError());
		this->_DoClose(pSoContext);
		return false;
	}
	return true;
}

//=====================================================================
//				 ContextList ��ز���
//=====================================================================
//////////////////////////////////////////////////////////////
// ���ͻ��˵������Ϣ�洢��������
void IocpServer::_AddToContextList(SocketContext* pSoContext)
{
	EnterCriticalSection(&m_csClientList);
	m_arrayClientContext.push_back(pSoContext);
	LeaveCriticalSection(&m_csClientList);
}

////////////////////////////////////////////////////////////////
//	�Ƴ�ĳ���ض���Context
void IocpServer::_RemoveContext(SocketContext* pSoContext)
{
	EnterCriticalSection(&m_csClientList);
	vector<SocketContext*>::iterator it;
	it = m_arrayClientContext.begin();
	while (it != m_arrayClientContext.end())
	{
		SocketContext* pContext = *it;
		if (pSoContext == pContext)
		{
			delete pSoContext;
			pSoContext = nullptr;
			it = m_arrayClientContext.erase(it);
			break;
		}
		it++;
	}
	LeaveCriticalSection(&m_csClientList);
}

////////////////////////////////////////////////////////////////
// ��տͻ�����Ϣ
void IocpServer::_ClearContextList()
{
	EnterCriticalSection(&m_csClientList);
	for (size_t i = 0; i < m_arrayClientContext.size(); i++)
	{
		delete m_arrayClientContext.at(i);
	}
	m_arrayClientContext.clear();
	LeaveCriticalSection(&m_csClientList);
}

//================================================================================
//				 ����������������
//================================================================================
///////////////////////////////////////////////////////////////////
// ��ñ����д�����������
int IocpServer::_GetNumOfProcessors() noexcept
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

/////////////////////////////////////////////////////////////////////
// �жϿͻ���Socket�Ƿ��Ѿ��Ͽ���������һ����Ч��Socket��Ͷ��WSARecv����������쳣
// ʹ�õķ����ǳ��������socket�������ݣ��ж����socket���õķ���ֵ
// ��Ϊ����ͻ��������쳣�Ͽ�(����ͻ��˱������߰ε����ߵ�)��ʱ��
// �����������޷��յ��ͻ��˶Ͽ���֪ͨ��
bool IocpServer::_IsSocketAlive(SOCKET s) noexcept
{
	const int nByteSent = send(s, "", 0, 0);
	if (SOCKET_ERROR == nByteSent)
	{
		return false;
	}
	else
	{
		return true;
	}
}

///////////////////////////////////////////////////////////////////
//�������ܣ���ʾ��������ɶ˿��ϵĴ���
bool IocpServer::HandleError(SocketContext* pSoContext, const DWORD& dwErr)
{
	// ����ǳ�ʱ�ˣ����ټ����Ȱ� 0x102=258L
	if (WAIT_TIMEOUT == dwErr)
	{
		// ȷ�Ͽͻ����Ƿ񻹻���...
		if (!_IsSocketAlive(pSoContext->m_Socket))
		{
			this->showMessage("��⵽�ͻ����쳣�˳���");
			this->OnConnectionClosed(pSoContext);
			this->_DoClose(pSoContext);
			return true;
		}
		else
		{
			this->showMessage("���������ʱ��������..");
			return true;
		}
	}
	// �����ǿͻ����쳣�˳���; 0x40=64L
	else if (ERROR_NETNAME_DELETED == dwErr)
	{// ��������������Ǽ���SOCKET�ҵ���
		//this->_ShowMessage("��⵽�ͻ����쳣�˳���");
		this->OnConnectionError(pSoContext, dwErr);
		if (!this->_DoClose(pSoContext))
		{
			this->showMessage("��⵽�쳣��");
		}
		return true;
	}
	else
	{//ERROR_OPERATION_ABORTED=995L
		this->showMessage("��ɶ˿ڲ����������߳��˳���err=%d", dwErr);
		this->OnConnectionError(pSoContext, dwErr);
		this->_DoClose(pSoContext);
		return false;
	}
}

void print_datetime()
{
	SYSTEMTIME sysTime = { 0 };
	GetLocalTime(&sysTime);
	printf("%4d-%02d-%02d %02d:%02d:%02d.%03d��",
		sysTime.wYear, sysTime.wMonth, sysTime.wDay,
		sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
		sysTime.wMilliseconds);
}

/////////////////////////////////////////////////////////////////////
// ������������ʾ��ʾ��Ϣ
void IocpServer::showMessage(const char* szFormat, ...)
{
	//printf(".");
	//return;
	__try
	{
		EnterCriticalSection(&m_csLog);
		print_datetime();
		// �����䳤����
		va_list arglist;
		va_start(arglist, szFormat);
		vprintf(szFormat, arglist);
		va_end(arglist);
		printf("\n");
		return;
	}
	__finally
	{
		::LeaveCriticalSection(&m_csLog);
	}
}