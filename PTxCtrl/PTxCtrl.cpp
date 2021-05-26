// PTxCtrl.cpp : �A�v���P�[�V�����̃G���g�� �|�C���g���`���܂��B
//

#include "stdafx.h"
#include "../Common/PTManager.h"
#include "../Common/PTCtrlMain.h"
#include "../Common/ServiceUtil.h"
#include "PTxCtrl.h"

// �T�[�r�X���s���ɃN���C�A���g�����Ȃ��Ȃ�����SDK����ă��������J�����邩�ǂ���
BOOL g_bXCompactService = FALSE ;

CPTCtrlMain g_cMain3(PT0_GLOBAL_LOCK_MUTEX, CMD_PT3_CTRL_EVENT_WAIT_CONNECT, CMD_PT3_CTRL_PIPE, FALSE);
CPTCtrlMain g_cMain1(PT0_GLOBAL_LOCK_MUTEX, CMD_PT1_CTRL_EVENT_WAIT_CONNECT, CMD_PT1_CTRL_PIPE, FALSE);

HANDLE g_hMutex;
SERVICE_STATUS_HANDLE g_hStatusHandle;

#define PT_CTRL_MUTEX L"PT0_CTRL_EXE_MUTEX"
#define SERVICE_NAME L"PTxCtrl Service"

extern "C" IPTManager* CreatePT1Manager(void);
extern "C" IPTManager* CreatePT3Manager(void);

int APIENTRY _tWinMain(HINSTANCE hInstance,
					 HINSTANCE hPrevInstance,
					 LPTSTR    lpCmdLine,
					 int       nCmdShow)
{
	WCHAR strExePath[512] = L"";
	GetModuleFileName(hInstance, strExePath, 512);

	wstring strIni;
	{
		WCHAR szPath[_MAX_PATH];	// �p�X
		WCHAR szDrive[_MAX_DRIVE];
		WCHAR szDir[_MAX_DIR];
		WCHAR szFname[_MAX_FNAME];
		WCHAR szExt[_MAX_EXT];
		_tsplitpath_s( strExePath, szDrive, _MAX_DRIVE, szDir, _MAX_DIR, szFname, _MAX_FNAME, szExt, _MAX_EXT );
		_tmakepath_s(  szPath, _MAX_PATH, szDrive, szDir, NULL, NULL );
		strIni = szPath;
	}

	strIni += L"\\BonDriver_PTx-ST.ini";
	g_bXCompactService = GetPrivateProfileInt(L"SET", L"xCompactService", 0, strIni.c_str());

	if( _tcslen(lpCmdLine) > 0 ){
		if( lpCmdLine[0] == '-' || lpCmdLine[0] == '/' ){
			if( _tcsicmp( _T("install"), lpCmdLine+1 ) == 0 ){
				WCHAR strExePath[512] = L"";
				GetModuleFileName(NULL, strExePath, 512);
				InstallService(strExePath, SERVICE_NAME,SERVICE_NAME);
				return 0;
			}else if( _tcsicmp( _T("remove"), lpCmdLine+1 ) == 0 ){
				RemoveService(SERVICE_NAME);
				return 0;
			}
		}
	}

	if( IsInstallService(SERVICE_NAME) == FALSE ){
		//���ʂ�exe�Ƃ��ċN�����s��
		HANDLE h = ::OpenMutexW(SYNCHRONIZE, FALSE, PT0_GLOBAL_LOCK_MUTEX);
		if (h != NULL) {
			BOOL bErr = FALSE;
			if (::WaitForSingleObject(h, 100) == WAIT_TIMEOUT) {
				bErr = TRUE;
			}
			::ReleaseMutex(h);
			::CloseHandle(h);
			if (bErr) {
				return -1;
			}
		}

		g_hStartEnableEvent = _CreateEvent(TRUE, TRUE, PT0_STARTENABLE_EVENT);
		if (g_hStartEnableEvent == NULL) {
			return -2;
		}
		// �ʃv���Z�X���I���������̏ꍇ�͏I����҂�(�ő�1.5�b)
		if (::WaitForSingleObject(g_hStartEnableEvent, g_bXCompactService?1000:1500) == WAIT_TIMEOUT) {
			::CloseHandle(g_hStartEnableEvent);
			return -3;
		}

		g_hMutex = _CreateMutex(TRUE, PT_CTRL_MUTEX);
		if (g_hMutex == NULL) {
			::CloseHandle(g_hStartEnableEvent);
			return -4;
		}
		if (::WaitForSingleObject(g_hMutex, 100) == WAIT_TIMEOUT) {
			// �ʃv���Z�X�����s��������
			::CloseHandle(g_hMutex);
			::CloseHandle(g_hStartEnableEvent);
			return -5;
		}

		//�N��
		StartMain(FALSE);

		::ReleaseMutex(g_hMutex);
		::CloseHandle(g_hMutex);

		::SetEvent(g_hStartEnableEvent);
		::CloseHandle(g_hStartEnableEvent);
	}else{
		//�T�[�r�X�Ƃ��ăC���X�g�[���ς�
		if( IsStopService(SERVICE_NAME) == FALSE ){
			g_hMutex = _CreateMutex(TRUE, PT_CTRL_MUTEX);
			int err = GetLastError();
			if( g_hMutex != NULL && err != ERROR_ALREADY_EXISTS ) {
				//�N��
				SERVICE_TABLE_ENTRY dispatchTable[] = {
					{ SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)service_main},
					{ NULL, NULL}
				};
				if( StartServiceCtrlDispatcher(dispatchTable) == FALSE ){
					OutputDebugString(_T("StartServiceCtrlDispatcher failed"));
				}
			}
		}else{
			//Stop��ԂȂ̂ŋN������
			StartServiceCtrl(SERVICE_NAME);
		}
	}
	return 0;
}

void WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv)
{
	g_hStatusHandle = RegisterServiceCtrlHandlerEx( SERVICE_NAME, (LPHANDLER_FUNCTION_EX)service_ctrl, NULL);

    do {

		if (g_hStatusHandle == NULL){
			break;
		}

		SendStatusScm(SERVICE_START_PENDING, 0, 1);

		SendStatusScm(SERVICE_RUNNING, 0, 0);
		StartMain(TRUE);

	} while(0);

	SendStatusScm(SERVICE_STOPPED, 0, 0);
}

DWORD WINAPI service_ctrl(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
	switch (dwControl){
		case SERVICE_CONTROL_STOP:
		case SERVICE_CONTROL_SHUTDOWN:
			SendStatusScm(SERVICE_STOP_PENDING, 0, 1);
			StopMain();
			return NO_ERROR;
			break;
		case SERVICE_CONTROL_POWEREVENT:
			OutputDebugString(_T("SERVICE_CONTROL_POWEREVENT"));
			if ( dwEventType == PBT_APMQUERYSUSPEND ){
				OutputDebugString(_T("PBT_APMQUERYSUSPEND"));
				if( g_cMain1.IsFindOpen() || g_cMain3.IsFindOpen() ){
					OutputDebugString(_T("BROADCAST_QUERY_DENY"));
					return BROADCAST_QUERY_DENY;
					}
			}else if( dwEventType == PBT_APMRESUMESUSPEND ){
				OutputDebugString(_T("PBT_APMRESUMESUSPEND"));
			}
			break;
		default:
			break;
	}
	SendStatusScm(NO_ERROR, 0, 0);
	return NO_ERROR;
}

BOOL SendStatusScm(int iState, int iExitcode, int iProgress)
{
	SERVICE_STATUS ss;

	ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	ss.dwCurrentState = iState;
	ss.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_POWEREVENT;
	ss.dwWin32ExitCode = iExitcode;
	ss.dwServiceSpecificExitCode = 0;
	ss.dwCheckPoint = iProgress;
	ss.dwWaitHint = 10000;

	return SetServiceStatus(g_hStatusHandle, &ss);
}

void StartMain(BOOL bService)
{
	CPTxCtrlCmdServiceOperator(CMD_PTX_CTRL_OP,bService).Main();
}

void StopMain()
{
	CPTxCtrlCmdServiceOperator::Stop();
}


  // CPTxCtrlCmdServiceOperator

CPTxCtrlCmdServiceOperator::CPTxCtrlCmdServiceOperator(wstring name, BOOL bService)
 : CPTxCtrlCmdOperator(name,true)
{
	PtService = bService ;
	PtPipeServer1 = PtPipeServer3 = NULL ;
	PtSupported = PtActivated = 0 ;

	Pt1Manager = CreatePT1Manager();
	Pt3Manager = CreatePT3Manager();

	if(g_cMain1.Init(PtService, Pt1Manager)) {
		PtPipeServer1 = g_cMain1.MakePipeServer() ;
		PtSupported |= 1 ;
		DBGOUT("PTxCtrl: PT1 is Supported and was Activated.\n");
	}else {
		DBGOUT("PTxCtrl: PT1 is NOT Supported.\n");
	}

	if(g_cMain3.Init(PtService, Pt3Manager)) {
		PtPipeServer3 = g_cMain3.MakePipeServer() ;
		PtSupported |= 1<<2 ;
		DBGOUT("PTxCtrl: PT3 is Supported and was Activated.\n");
	}else {
		DBGOUT("PTxCtrl: PT3 is NOT Supported.\n");
	}

	LastActivated = GetTickCount() ;
	PtActivated = PtSupported ;
}

CPTxCtrlCmdServiceOperator::~CPTxCtrlCmdServiceOperator()
{
	SAFE_DELETE(PtPipeServer1) ;
	SAFE_DELETE(PtPipeServer3) ;
	SAFE_DELETE(Pt1Manager) ;
	SAFE_DELETE(Pt3Manager) ;
}

BOOL CPTxCtrlCmdServiceOperator::ResSupported(DWORD &PtBits)
{
	PtBits = PtSupported ;
	return TRUE;
}

BOOL CPTxCtrlCmdServiceOperator::ResActivatePt(DWORD PtVer)
{
	if( ! (PtSupported&(1<<(PtVer-1))) ) {
		DBGOUT("PTxCtrl: PT%d is Not Supported.\n",PtVer);
		return FALSE;
	}

	if( PtActivated &(1<<(PtVer-1)) ) {
		DBGOUT("PTxCtrl: PT%d is Already Activated.\n",PtVer);
		LastActivated = GetTickCount() ;
		return TRUE;
	}

	if(PtVer==3) {
		if(g_cMain3.Init(PtService, Pt3Manager)) {
			PtPipeServer3 = g_cMain3.MakePipeServer() ;
			PtActivated |= 1<<2 ;
			DBGOUT("PTxCtrl: PT3 was Re-Activated.\n");
			LastActivated = GetTickCount() ;
			return TRUE ;
		}
	}else if(PtVer==1) {
		if(g_cMain1.Init(PtService, Pt1Manager)) {
			PtPipeServer1 = g_cMain1.MakePipeServer() ;
			PtActivated |= 1 ;
			DBGOUT("PTxCtrl: PT1 was Re-Activated.\n");
			LastActivated = GetTickCount() ;
			return TRUE ;
		}
	}

	return FALSE;
}

void CPTxCtrlCmdServiceOperator::Main()
{
	//------ BEGIN OF THE SERVICE LOOP ------

	DBGOUT("PTxCtrl: The service is started.\n");

	DWORD LastDeactivated = GetTickCount();
	BOOL bRstStEnable=FALSE ;

	while(!PtTerminated) {

		DWORD dwServiceWait=15000;
		DWORD dwDurLastAct = dur(LastActivated) ;

		if(dwDurLastAct<5000) {

			// �V�K�N���C�A���g�A�N�e�B�u���⍇�킹����5�b�Ԃ͔j�������֎~
			dwServiceWait=5000-dwDurLastAct;

		}else {

			// �j������

			if(PtActivated&(1<<2)) { // PT3
				if(WaitForSingleObject(g_cMain3.GetStopEvent(),0)==WAIT_OBJECT_0) {
					if(!bRstStEnable) {
						ResetEvent(g_hStartEnableEvent);
						bRstStEnable=TRUE ;
					}
					if(g_bXCompactService||!PtService) {
						SAFE_DELETE(PtPipeServer3);
						g_cMain3.UnInit();
						PtActivated &= ~(1<<2) ;
						DBGOUT("PTxCtrl: PT3 was De-Activated.\n");
						if(!PtActivated) LastDeactivated = GetTickCount();
					}
					ResetEvent(g_cMain3.GetStopEvent());
				}
			}

			if(PtActivated&1) { // PT1/PT2
				if(WaitForSingleObject(g_cMain1.GetStopEvent(),0)==WAIT_OBJECT_0) {
					if(!bRstStEnable) {
						ResetEvent(g_hStartEnableEvent);
						bRstStEnable=TRUE ;
					}
					if(g_bXCompactService||!PtService) {
						SAFE_DELETE(PtPipeServer1);
						g_cMain1.UnInit();
						PtActivated &= ~1 ;
						DBGOUT("PTxCtrl: PT1 was De-Activated.\n");
						if(!PtActivated) LastDeactivated = GetTickCount();
					}
					ResetEvent(g_cMain1.GetStopEvent());
				}
			}

		}

		if(!PtActivated && !PtService) {
			// ���ׂẴN���C�A���g�����Ȃ��Ȃ������
			DWORD dwDurLastDeact = dur(LastDeactivated) ;
			// T->S�Ȃǂ̐ؑւ̍ۂ�PTxCtrl.exe�̍ċN������莞�ԗ}�����鏈��
			if(!g_bXCompactService && dwDurLastDeact<500) {
				// 500�~���b�����V�K�N���C�A���g�ɐڑ��̃`�����X��^����
				dwServiceWait=500-dwDurLastDeact;
			}else {
				// 500�~���b�o�߂��Ă��V�K�N���C�A���g������Ȃ�������I������
				PtTerminated=TRUE; continue;
			}
		}
		else if(bRstStEnable) {
			SetEvent(g_hStartEnableEvent);
			bRstStEnable=FALSE;
		}

		if(WaitForCmd(dwServiceWait)==WAIT_OBJECT_0) {
			if(!ServiceReaction()) {
				DBGOUT("PTxCtrl: The service reaction was failed.\n");
			}
		}else{
			//�A�v���w���񂾎��p�̃`�F�b�N
			if(PtActivated&(1<<2)) { // PT3
				if( Pt3Manager->CloseChk() == FALSE){
					if(!PtService) SetEvent(g_cMain3.GetStopEvent()) ;
				}
			}
			if(PtActivated&1) { // PT1/PT2
				if( Pt1Manager->CloseChk() == FALSE){
					if(!PtService) SetEvent(g_cMain1.GetStopEvent()) ;
				}
			}
		}

	}

	DBGOUT("PTxCtrl: The service was finished.\n");

	//------ END OF THE SERVICE LOOP ------

	SAFE_DELETE(PtPipeServer3);
	SAFE_DELETE(PtPipeServer1);
	if(PtActivated&1)
		g_cMain1.UnInit();
	if(PtActivated&(1<<2))
		g_cMain3.UnInit();

	PtActivated = 0 ;
}

BOOL CPTxCtrlCmdServiceOperator::PtTerminated=FALSE;
void CPTxCtrlCmdServiceOperator::Stop()
{
	g_cMain1.StopMain();
	g_cMain3.StopMain();
	PtTerminated = TRUE ;
}

