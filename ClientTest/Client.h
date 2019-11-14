/*==========================================================================
Purpose:
* �����CClient�Ǳ�����ĺ����࣬���ڲ�������ָ���Ĳ����߳���ָ������������
 ��Ϣ�����Է���������Ӧ����Դռ�����������ʹ����MFC�Ի������������˵��
Notes:
* �ͻ���ʹ�õ�����򵥵Ķ��߳�����ʽSocket������ÿ���߳�ֻ����һ������
 �����Ҫ�����޸ĳɷ��Ͷ�����ݵ����
==========================================================================*/
#pragma once
// ����deprecation����
#pragma warning(disable: 4996)
// ����������(8*1024�ֽ�)
#define MAX_BUFFER_LEN 8196 
#define DEFAULT_PORT 10240 // Ĭ�϶˿�
#define DEFAULT_IP _T("127.0.0.1") // Ĭ��IP��ַ
#define DEFAULT_THREADS 100 // Ĭ�ϲ����߳���
#define DEFAULT_TIMES 10000 // Ĭ�Ϸ��ʹ���
#define DEFAULT_MESSAGE _T("Hello!") // Ĭ�ϵķ�����Ϣ

class CClient;

// ���ڷ������ݵ��̲߳���
struct WorkerThreadParam
{
	CClient* pClient; // ��ָ�룬���ڵ������еĺ���
	SOCKET sock; // ÿ���߳�ʹ�õ�Socket
	int nThreadNo; // �̱߳��
	int nSendTimes; // ���ʹ���
	char szSendBuffer[MAX_BUFFER_LEN];
	char szRecvBuffer[MAX_BUFFER_LEN];
};

// ����Socket���ӵ��߳�
struct ConnectionThreadParam
{
	CClient* pClient; // ��ָ�룬���ڵ������еĺ���
};

class CClient
{
public:
	CClient(void);
	~CClient(void);

public:
	// ����Socket��
	bool LoadSocketLib();
	// ж��Socket�⣬��������
	void UnloadSocketLib() { WSACleanup(); }
	// ��ʼ����
	bool Start();
	//	ֹͣ����
	void Stop();
	// ��ñ�����IP��ַ
	CString GetLocalIP();
	// ��������IP��ַ
	void SetIP(const CString& strIP) { m_strServerIP = strIP; }
	// ���ü����˿�
	void SetPort(const int& nPort) { m_nPort = nPort; }
	// ���ò����̷߳��ʹ���
	void SetTimes(const int& n) { m_nTimes = n; }
	// ���ò����߳�����
	void SetThreads(const int& n) { m_nThreads = n; }
	// ����Ҫ�����͵���Ϣ
	void SetMessage(const CString& strMessage) { m_strMessage = strMessage; }
	// �����������ָ�룬���ڵ����亯��
	void SetMainDlg(CDialog* p) { m_pMain = p; }
	// ������������ʾ��Ϣ
	void ShowMessage(const CString strInfo, ...);

private:
	// ��������
	bool EstablishConnections();
	// ���������������
	bool ConnetToServer(SOCKET* pSocket, CString strServer, int nPort);
	// ���ڽ������ӵ��߳�
	static DWORD WINAPI _ConnectionThread(LPVOID lpParam);
	// ���ڷ�����Ϣ���߳�
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);
	// �ͷ���Դ
	void CleanUp();

private:
	CDialog* m_pMain; // ����ָ��
	CString m_strServerIP; // �������˵�IP��ַ
	CString m_strLocalIP; // ����IP��ַ
	CString m_strMessage; // ��������������Ϣ
	int m_nPort; // �����˿�
	int m_nTimes; // �����̷߳��ʹ���
	int m_nThreads; // �����߳�����
	HANDLE* m_phWorkerThreads;
	HANDLE m_hConnectionThread; // �������ӵ��߳̾��
	HANDLE m_hShutdownEvent; // ����֪ͨ�߳�ϵͳ�˳����¼���Ϊ���ܹ����õ��˳��߳�

	WorkerThreadParam* m_pWorkerParams; // �̲߳���
};