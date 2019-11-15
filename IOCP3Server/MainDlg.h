// MainDlg.h : 头文件
#pragma once
#include "IOCPModel.h"

// CMainDlg 对话框
class CMainDlg : public CDialog
{
public:
	CMainDlg(CWnd* pParent = NULL);	// 标准构造函数

	enum { IDD = IDD_SERVER_DIALOG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	// 实现
protected:
	HICON m_hIcon;
	// 生成的消息映射函数
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	// 开始监听
	afx_msg void OnBnClickedOk();
	// 停止监听
	afx_msg void OnBnClickedStop();
	// "退出"按钮
	afx_msg void OnBnClickedCancel();
	// 系统退出的时候，为确保资源释放，停止监听，清空Socket类库
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()

private:
	// 初始化Socket库以及界面信息
	void Init();
	// 初始化List控件
	void InitListCtrl();

public:
	// 当前客户端有新消息到来的时候，在主界面中显示
	//		新到来的信息(在类CIocpModel中调用)
	// 为了减少界面代码对效率的影响，此处使用了内联
	inline void AddInformation(const CString strInfo)
	{
		CListCtrl* pList = (CListCtrl*)GetDlgItem(IDC_LIST_INFO);
		pList->InsertItem(0, strInfo);
	}

private:
	CIocpModel m_IOCP;// 主要对象，完成端口模型
};
