/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
//////////////////////////////////////////////////////////////////////////////
// CONSOLINST.h - Netscape SuiteSpot Installation Plug-In Directory Server
//
//

#ifndef __CONSOLINST_H
#define __CONSOLINST_H


extern __declspec(dllexport) INT  __cdecl CONSOLINST_AskOptions(HWND hwndParent, INT nDirection);
extern __declspec(dllexport) VOID __cdecl CONSOLINST_GetSummary(LPSTR lpszSummary);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_WriteCacheGlobal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_WriteCacheLocal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_ReadCacheGlobal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_ReadCacheLocal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_PreInstall(LPCSTR lpszInstallPath);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_Install(void);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_PostInstall(void);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_PreUnInstall(LPCSTR pszServerRoot);
extern __declspec(dllexport) BOOL __cdecl CONSOLINST_PostUnInstall(LPCSTR pszServerRoot);


typedef struct tagMODULEINFO {
  HINSTANCE m_hModule;
  HWND      m_hwndParent;
  INT       m_nResult;
} MODULEINFO;

#define MAX_STR_SIZE 512
#define CON_ID_DIR   "slapd-client"
#define CON_TARGET   "java"
#define CON_JAR		 "ds40jars.z"
#define CON_MESSAGE  "Installing Directory Server Management Console..."
#endif // __CONSOLINST_H
