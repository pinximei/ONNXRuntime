#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS

#include <afxwin.h>      // MFC core
#include <afxext.h>      // MFC extensions
#include <afxcmn.h>      // MFC common controls (CListCtrl)
#include <afxdialogex.h>

#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include <atomic>
#include <memory>
#include <string>
#include <vector>
