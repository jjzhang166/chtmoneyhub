// stdafx.cpp : source file that includes just the standard includes
//	Updater.pch will be the pre-compiled header
//	stdafx.obj will contain the pre-compiled type information

#include "stdafx.h"


tstring _TempCachePath;

//////////////////////////////////////////////////////////////////////////

const UINT WM_TASKBARCREATED = RegisterWindowMessage(_T("TaskbarCreated"));
const UINT WM_DOWNLOADOK = RegisterWindowMessage(_T("BankUpdateOK"));
const UINT WM_DOWNLOADSTOP = RegisterWindowMessage(_T("BankUpdateStop"));
const UINT WM_TRAYICON = RegisterWindowMessage(_T("BankUpdateTrayMsg"));
const UINT WM_BROADCAST_QUIT = RegisterWindowMessage(_T("BankQuitBroadcastMsg"));
const UINT WM_UPDATERETRY = RegisterWindowMessage(_T("BankUpdateRetryMsg"));
const UINT WM_SETPROGRESSPOS = RegisterWindowMessage(_T("BankSetProgressMsg"));