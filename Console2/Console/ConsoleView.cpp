#include "stdafx.h"
#include "resource.h"

#include <fstream>

#include "Console.h"
#include "ConsoleView.h"

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

CDC		ConsoleView::m_dcOffscreen(::CreateCompatibleDC(NULL));
CDC		ConsoleView::m_dcText(::CreateCompatibleDC(NULL));

CBitmap	ConsoleView::m_bmpOffscreen;
CBitmap	ConsoleView::m_bmpText;

CFont	ConsoleView::m_fontText;

int		ConsoleView::m_nCharHeight(0);
int		ConsoleView::m_nCharWidth(0);

//////////////////////////////////////////////////////////////////////////////

ConsoleView::ConsoleView(DWORD dwTabIndex, const wstring& strCmdLineInitialDir, const wstring& strInitialCmd, const wstring& strDbgCmdLine, DWORD dwRows, DWORD dwColumns)
: m_strCmdLineInitialDir(strCmdLineInitialDir)
, m_strInitialCmd(strInitialCmd)
, m_strDbgCmdLine(strDbgCmdLine)
, m_bInitializing(true)
, m_bAppActive(true)
, m_bActive(true)
, m_bUseTextAlphaBlend(false)
, m_bConsoleWindowVisible(false)
, m_dwStartupRows(dwRows)
, m_dwStartupColumns(dwColumns)
, m_bShowVScroll(false)
, m_bShowHScroll(false)
, m_nVScrollWidth(::GetSystemMetrics(SM_CXVSCROLL))
, m_nHScrollWidth(::GetSystemMetrics(SM_CXHSCROLL))
, m_strTitle(g_settingsHandler->GetTabSettings().tabDataVector[dwTabIndex]->strTitle.c_str())
, bigIcon()
, smallIcon()
, m_consoleHandler()
, m_screenBuffer()
, m_consoleSettings(g_settingsHandler->GetConsoleSettings())
, m_appearanceSettings(g_settingsHandler->GetAppearanceSettings())
, m_hotkeys(g_settingsHandler->GetHotKeys())
, m_tabData(g_settingsHandler->GetTabSettings().tabDataVector[dwTabIndex])
, m_background()
, m_backgroundBrush(NULL)
, m_cursor()
, m_selectionHandler()
, m_mouseCommand(MouseSettings::cmdNone)
{
	TRACE(L"Scroll width: %i\n", m_nHScrollWidth);
}

ConsoleView::~ConsoleView()
{
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

BOOL ConsoleView::PreTranslateMessage(MSG* pMsg)
{
	if ((pMsg->message == WM_KEYDOWN) || 
		(pMsg->message == WM_KEYUP) ||
		(pMsg->message == WM_SYSKEYDOWN) || 
		(pMsg->message == WM_SYSKEYUP))
	{
		// Avoid calling ::TranslateMessage for WM_KEYDOWN, WM_KEYUP,
		// WM_SYSKEYDOWN and WM_SYSKEYUP.
		// This prevents WM_CHAR and WM_SYSCHAR messages, enabling stuff like
		// handling 'dead' characters input and passing all keys to console.
		::DispatchMessage(pMsg);
		return TRUE;
	}

	return FALSE;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
/*
	m_dcOffscreen.CreateCompatibleDC(NULL);
	m_dcText.CreateCompatibleDC(NULL);
*/

	// set view title
	SetWindowText(m_strTitle);

	DragAcceptFiles(TRUE);

	// load icon
	if (m_tabData->strIcon.length() > 0)
	{
		bigIcon = static_cast<HICON>(::LoadImage(
											NULL, 
											m_tabData->strIcon.c_str(), 
											IMAGE_ICON, 
											0, 
											0, 
											LR_DEFAULTCOLOR|LR_LOADFROMFILE|LR_DEFAULTSIZE));

		smallIcon = static_cast<HICON>(::LoadImage(
											NULL, 
											m_tabData->strIcon.c_str(), 
											IMAGE_ICON, 
											16, 
											16, 
											LR_DEFAULTCOLOR|LR_LOADFROMFILE));
	}
	else
	{
		bigIcon = static_cast<HICON>(::LoadImage(
											::GetModuleHandle(NULL), 
											MAKEINTRESOURCE(IDR_MAINFRAME), 
											IMAGE_ICON, 
											0, 
											0, 
											LR_DEFAULTCOLOR|LR_DEFAULTSIZE));

		smallIcon = static_cast<HICON>(::LoadImage(
											::GetModuleHandle(NULL), 
											MAKEINTRESOURCE(IDR_MAINFRAME), 
											IMAGE_ICON, 
											16, 
											16, 
											LR_DEFAULTCOLOR));
	}

	// set console delegates
	m_consoleHandler.SetupDelegates(
						fastdelegate::MakeDelegate(this, &ConsoleView::OnConsoleChange), 
						fastdelegate::MakeDelegate(this, &ConsoleView::OnConsoleClose));

	// load background image
	if (m_tabData->backgroundImageType == bktypeImage)
	{
		m_background = g_imageHandler->GetImage(m_tabData->imageData);
	}
	else if (m_tabData->backgroundImageType == bktypeDesktop)
	{
		m_background = g_imageHandler->GetDesktopImage(m_tabData->imageData);
	}

	if (m_background.get() == NULL) m_tabData->backgroundImageType = bktypeNone;

	// TODO: error handling
	wstring strInitialDir(m_consoleSettings.strInitialDir);

	if (m_strCmdLineInitialDir.length() > 0)
	{
		strInitialDir = m_strCmdLineInitialDir;
	}
	else if (m_tabData->strInitialDir.length() > 0)
	{
		strInitialDir = m_tabData->strInitialDir;
	}

	wstring	strShell(m_consoleSettings.strShell);
	bool	bDebugFlag = false;

	if (m_strDbgCmdLine.length() > 0)
	{
		strShell	= m_strDbgCmdLine;
		bDebugFlag	= true;
	}
	else if (m_tabData->strShell.length() > 0)
	{
		strShell	= m_tabData->strShell;
	}

	if (!m_consoleHandler.StartShellProcess(
								strShell, 
								strInitialDir, 
								m_strInitialCmd,
								g_settingsHandler->GetAppearanceSettings().windowSettings.bUseConsoleTitle ? m_tabData->strTitle : wstring(L""),
								m_dwStartupRows, 
								m_dwStartupColumns,
								bDebugFlag))
	{
		return -1;
	}
	m_bInitializing = false;

	// set current language in the console window
	::PostMessage(
		m_consoleHandler.GetConsoleParams()->hwndConsoleWindow,
		WM_INPUTLANGCHANGEREQUEST, 
		0, 
		reinterpret_cast<LPARAM>(::GetKeyboardLayout(0)));

	// scrollbar stuff
	InitializeScrollbars();

	// create offscreen buffers
	CreateOffscreenBuffers();

	// TODO: put this in console size change handler
	m_screenBuffer.reset(new CHAR_INFO[m_consoleHandler.GetConsoleParams()->dwRows*m_consoleHandler.GetConsoleParams()->dwColumns]);
	::ZeroMemory(m_screenBuffer.get(), sizeof(CHAR_INFO)*m_consoleHandler.GetConsoleParams()->dwRows*m_consoleHandler.GetConsoleParams()->dwColumns);

	m_consoleHandler.StartMonitorThread();

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	CPaintDC	dc(m_hWnd);

	if ((m_tabData->backgroundImageType != bktypeNone) && m_tabData->imageData.bRelative)
	{
		// we need to update offscreen buffers here for relative backgrounds
		UpdateOffscreen(dc.m_ps.rcPaint);
	}

	dc.BitBlt(
		dc.m_ps.rcPaint.left, 
		dc.m_ps.rcPaint.top, 
		dc.m_ps.rcPaint.right, 
		dc.m_ps.rcPaint.bottom,
		m_dcOffscreen, 
		dc.m_ps.rcPaint.left, 
		dc.m_ps.rcPaint.top, 
		SRCCOPY);

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnWindowPosChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	WINDOWPOS* pWinPos = reinterpret_cast<WINDOWPOS*>(lParam);

	// showing the view, repaint
	if (pWinPos->flags & SWP_SHOWWINDOW) Repaint();

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnSysKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if ((wParam == VK_SPACE) && (lParam & (0x1 << 29)))
	{
		return ::DefWindowProc(GetParent(), uMsg, wParam, lParam);
	}

	return OnConsoleFwdMsg(uMsg, wParam, lParam, bHandled);
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnConsoleFwdMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	if (!TranslateKeyDown(uMsg, wParam, lParam))
	{
//		TRACE(L"Msg: 0x%04X\n", uMsg);
		::PostMessage(m_consoleHandler.GetConsoleParams()->hwndConsoleWindow, uMsg, wParam, lParam);
	}

	return 0;
}


//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnVScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	DoScroll(SB_VERT, LOWORD(wParam), HIWORD(wParam));
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnHScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	DoScroll(SB_HORZ, LOWORD(wParam), HIWORD(wParam));
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	HandleMouseClick(uMsg, static_cast<UINT>(wParam), CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
/*
	UINT				uiFlags = static_cast<UINT>(wParam);
	CPoint				point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	MouseSettings&		mouseSettings	= g_settingsHandler->GetMouseSettings();
	DWORD				dwModifierKeys	= MouseSettings::mkNone;

	if (uiFlags & MK_CONTROL)		dwModifierKeys	|= MouseSettings::mkCtrl;
	if (uiFlags & MK_SHIFT)			dwModifierKeys	|= MouseSettings::mkShift;
	if (GetKeyState(VK_MENU) < 0)	dwModifierKeys	|= MouseSettings::mkAlt;

	if (mouseSettings.bUseDrag && (mouseSettings.dwDragModifiers == dwModifierKeys))
	{
		// mouse drag
		// selection active, return
		if (m_selectionHandler->GetState() > SelectionHandler::selstateNoSelection) return 0;

		ClientToScreen(&point);
		GetParent().PostMessage(UM_START_MOUSE_DRAG, wParam, MAKELPARAM(point.x, point.y));
	}
	else if (mouseSettings.bUseSelection && (mouseSettings.dwSelectionModifiers == dwModifierKeys))
	{
		// start selection
		if (m_selectionHandler->GetState() == SelectionHandler::selstateSelected) return 0;

		m_selectionHandler->StartSelection(GetConsoleCoord(point), m_screenBuffer);
	}
	else
	{
		// send mouse event to console
		m_consoleHandler.SendMouseEvent(GetConsoleCoord(point), FROM_LEFT_1ST_BUTTON_PRESSED, 0);
	}
*/

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	HandleMouseClick(uMsg, static_cast<UINT>(wParam), CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));

/*
	CPoint	point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

	if (m_selectionHandler->GetState() == SelectionHandler::selstateStartedSelecting)
	{

		m_selectionHandler->EndSelection();
		m_selectionHandler->ClearSelection();

	}
	else if (m_selectionHandler->GetState() == SelectionHandler::selstateSelected)
	{
		Copy(&point);

	}
	else if (m_selectionHandler->GetState() == SelectionHandler::selstateSelecting)
	{
		m_selectionHandler->EndSelection();

		// copy on select
		if (g_settingsHandler->GetBehaviorSettings().copyPasteSettings.bCopyOnSelect)
		{
			Copy(NULL);
		}
	}
	else
	{
		// send mouse event to console
	 	m_consoleHandler.SendMouseEvent(GetConsoleCoord(point), 0, 0, 0);
	}
*/

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnLButtonBblClk(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	UINT	uiFlags = static_cast<UINT>(wParam);
	CPoint	point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	DWORD	dwModifierKeys	= MouseSettings::mkNone;

	if (uiFlags & MK_CONTROL)		dwModifierKeys	|= MouseSettings::mkCtrl;
	if (uiFlags & MK_SHIFT)			dwModifierKeys	|= MouseSettings::mkShift;
	if (GetKeyState(VK_MENU) < 0)	dwModifierKeys	|= MouseSettings::mkAlt;

	if (dwModifierKeys == MouseSettings::mkNone)
	{
		// send mouse event to console
		m_consoleHandler.SendMouseEvent(GetConsoleCoord(point), FROM_LEFT_1ST_BUTTON_PRESSED, 0, DOUBLE_CLICK);
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnRButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	GetParent().SendMessage(UM_SHOW_POPUP_MENU, wParam, lParam);
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnMButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	Paste();
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	UINT	uiFlags = static_cast<UINT>(wParam);
	CPoint	point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

	if (uiFlags & MK_LBUTTON)
	{
		if ((m_selectionHandler->GetState() == SelectionHandler::selstateStartedSelecting) ||
			(m_selectionHandler->GetState() == SelectionHandler::selstateSelecting))
		{
			CRect	rectClient;
			GetClientRect(&rectClient);

			DWORD dwInsideBorder = g_settingsHandler->GetAppearanceSettings().stylesSettings.dwInsideBorder;

			if (point.x < rectClient.left + static_cast<LONG>(dwInsideBorder))
			{
				DoScroll(SB_HORZ, SB_LINELEFT, 0);
			}			
			else if (point.x > rectClient.right - static_cast<LONG>(dwInsideBorder))
			{
				DoScroll(SB_HORZ, SB_LINERIGHT, 0);
			}
			
			if (point.y < rectClient.top + static_cast<LONG>(dwInsideBorder))
			{
				DoScroll(SB_VERT, SB_LINEUP, 0);
			}
			else if (point.y > rectClient.bottom - static_cast<LONG>(dwInsideBorder))
			{
				DoScroll(SB_VERT, SB_LINEDOWN, 0);
			}

			m_selectionHandler->UpdateSelection(GetConsoleCoord(point), m_screenBuffer);
			BitBltOffscreen();
		}
		else
		{
			// send mouse event to console
			m_consoleHandler.SendMouseEvent(GetConsoleCoord(point), FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED);
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	if (m_bActive && (wParam == CURSOR_TIMER) && (m_cursor.get() != NULL))
	{
		m_cursor->PrepareNext();
		m_cursor->Draw(m_bAppActive);
		BitBltOffscreen(true);
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnInputLangChangeRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	::PostMessage(m_consoleHandler.GetConsoleParams()->hwndConsoleWindow, uMsg, wParam, lParam);
	bHandled = FALSE;
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

LRESULT ConsoleView::OnDropFiles(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	HDROP	hDrop = reinterpret_cast<HDROP>(wParam);
	UINT	uiFilesCount = ::DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
	CString	strFilenames;

	// concatenate all filenames
	for (UINT i = 0; i < uiFilesCount; ++i)
	{
		CString	strFilename;
		::DragQueryFile(hDrop, i, strFilename.GetBuffer(MAX_PATH), MAX_PATH);
		strFilename.ReleaseBuffer();

		// put quotes around the filename
		strFilename = CString(L"\"") + strFilename + CString("\"");
		
		if (i > 0) strFilenames += L" ";
		strFilenames += strFilename;

	}
	::DragFinish(hDrop);
	
	SendTextToConsole(strFilenames);
	return 0;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::GetRect(CRect& clientRect)
{
	StylesSettings& stylesSettings = g_settingsHandler->GetAppearanceSettings().stylesSettings;

	clientRect.left		= 0;
	clientRect.top		= 0;
	clientRect.right	= m_consoleHandler.GetConsoleParams()->dwColumns*m_nCharWidth + 2*stylesSettings.dwInsideBorder;
	clientRect.bottom	= m_consoleHandler.GetConsoleParams()->dwRows*m_nCharHeight + 2*stylesSettings.dwInsideBorder;

	if (m_bShowVScroll) clientRect.right	+= m_nVScrollWidth;
	if (m_bShowHScroll) clientRect.bottom	+= m_nHScrollWidth;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

bool ConsoleView::GetMaxRect(CRect& maxClientRect)
{
	if (m_bInitializing) return false;

	StylesSettings& stylesSettings = g_settingsHandler->GetAppearanceSettings().stylesSettings;

	// TODO: take care of max window size
	maxClientRect.left	= 0;
	maxClientRect.top	= 0;
	maxClientRect.right	= m_consoleHandler.GetConsoleParams()->dwMaxColumns*m_nCharWidth + 2*stylesSettings.dwInsideBorder;
	maxClientRect.bottom= m_consoleHandler.GetConsoleParams()->dwMaxRows*m_nCharHeight + 2*stylesSettings.dwInsideBorder;

	CWindow desktopWindow(::GetDesktopWindow());
	CRect	rectDesktop;
	bool	bRecalc = false;

	desktopWindow.GetWindowRect(rectDesktop);

	if (rectDesktop.Width() < maxClientRect.Width())
	{
		m_consoleHandler.GetConsoleParams()->dwMaxColumns = (rectDesktop.Width() - 2*stylesSettings.dwInsideBorder) / m_nCharWidth;
		bRecalc = true;
	}

	if (rectDesktop.Height() < maxClientRect.Height())
	{
		m_consoleHandler.GetConsoleParams()->dwMaxRows = (rectDesktop.Height() - 2*stylesSettings.dwInsideBorder) / m_nCharHeight;
		bRecalc = true;
	}

	if (bRecalc)
	{
		maxClientRect.right	= m_consoleHandler.GetConsoleParams()->dwMaxColumns*m_nCharWidth + 2*stylesSettings.dwInsideBorder;
		maxClientRect.bottom= m_consoleHandler.GetConsoleParams()->dwMaxRows*m_nCharHeight + 2*stylesSettings.dwInsideBorder;
	}

	if (m_bShowVScroll) maxClientRect.right	+= m_nVScrollWidth;
	if (m_bShowHScroll) maxClientRect.bottom+= m_nHScrollWidth;

	return true;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::AdjustRectAndResize(CRect& clientRect, DWORD dwResizeWindowEdge, bool bGetClientRect)
{
	StylesSettings& stylesSettings = g_settingsHandler->GetAppearanceSettings().stylesSettings;

	if (bGetClientRect) GetWindowRect(&clientRect);
/*
	TRACE(L"================================================================\n");
	TRACE(L"rect: %ix%i - %ix%i\n", clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
*/

	// exclude scrollbars from row/col calculation
	if (m_bShowVScroll) clientRect.right	-= m_nVScrollWidth;
	if (m_bShowHScroll) clientRect.bottom	-= m_nHScrollWidth;

	// TODO: handle variable fonts
	DWORD dwColumns	= (clientRect.Width() - 2*stylesSettings.dwInsideBorder) / m_nCharWidth;
	DWORD dwRows	= (clientRect.Height() - 2*stylesSettings.dwInsideBorder) / m_nCharHeight;

	clientRect.right	= clientRect.left + dwColumns*m_nCharWidth + 2*stylesSettings.dwInsideBorder;
	clientRect.bottom	= clientRect.top + dwRows*m_nCharHeight + 2*stylesSettings.dwInsideBorder;

	// adjust for scrollbars
	if (m_bShowVScroll) clientRect.right	+= m_nVScrollWidth;
	if (m_bShowHScroll) clientRect.bottom	+= m_nHScrollWidth;

	SharedMemory<ConsoleSize>&	newConsoleSize = m_consoleHandler.GetNewConsoleSize();
	SharedMemoryLock			memLock(newConsoleSize);

	newConsoleSize->dwColumns			= dwColumns;
	newConsoleSize->dwRows				= dwRows;
	newConsoleSize->dwResizeWindowEdge	= dwResizeWindowEdge;

/*
	TRACE(L"console view: 0x%08X, adjusted: %ix%i\n", m_hWnd, dwRows, dwColumns);
	TRACE(L"================================================================\n");
*/

	m_consoleHandler.GetNewConsoleSize().SetReqEvent();
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::SetConsoleWindowVisible(bool bVisible)
{
	m_bConsoleWindowVisible = bVisible;
	::ShowWindow(m_consoleHandler.GetConsoleParams()->hwndConsoleWindow, bVisible ? SW_SHOW : SW_HIDE);
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::SetAppActiveStatus(bool bAppActive)
{
	m_bAppActive = bAppActive;
	if (m_cursor.get() != NULL) m_cursor->Draw(m_bAppActive);
	BitBltOffscreen();
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::RecreateOffscreenBuffers()
{
	if (!m_fontText.IsNull())		m_fontText.DeleteObject();
	if (!m_bmpOffscreen.IsNull())	m_bmpOffscreen.DeleteObject();
	if (!m_bmpText.IsNull())		m_bmpText.DeleteObject();
	CreateOffscreenBuffers();
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::RepaintView()
{
	RepaintText();
	BitBltOffscreen();
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::SetActive(bool bActive)
{
	m_bActive = bActive;
	if (m_bActive)
	{
		RepaintView();
		UpdateTitle();
	}
}

//////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::SetTitle(const CString& strTitle)
{
/*
	if (g_settingsHandler->GetAppearanceSettings().windowSettings.bUseConsoleTitle)
	{
		// if we're using console titles, update it
		CWindow consoleWnd(m_consoleHandler.GetConsoleParams()->hwndConsoleWindow);
		consoleWnd.SetWindowText(strTitle);

		m_strTitle = strTitle;
	}
	else
	{
		// we're not using console window title, parse the command part
		int		nPos = m_strTitle.Find(L'-');

		if (nPos == -1)
		{
			m_strTitle = strTitle;
		}
		else
		{
			m_strTitle = strTitle + m_strTitle.Right(m_strTitle.GetLength() - nPos + 1);
		}
	}
*/

	m_strTitle = strTitle;
	SetWindowText(m_strTitle);
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

CString ConsoleView::GetConsoleCommand()
{
	CWindow consoleWnd(m_consoleHandler.GetConsoleParams()->hwndConsoleWindow);
	CString strConsoleTitle(L"");

	consoleWnd.GetWindowText(strConsoleTitle);

	int nPos = strConsoleTitle.Find(L'-');

	if (nPos == -1)
	{
		return CString(L"");
	}
	else
	{
		return strConsoleTitle.Right(strConsoleTitle.GetLength() - nPos + 1);
	}
}

/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::Copy(const CPoint* pPoint /* = NULL */)
{
	if ((m_selectionHandler->GetState() != SelectionHandler::selstateSelecting) &&
		(m_selectionHandler->GetState() != SelectionHandler::selstateSelected))
	{
		return;
	}

	SharedMemory<CHAR_INFO>&	consoleBuffer = m_consoleHandler.GetConsoleBuffer();
	SharedMemoryLock			memLock(consoleBuffer);

	if (pPoint != NULL)
	{
		m_selectionHandler->CopySelection(GetConsoleCoord(*pPoint));
	}
	else
	{
		// called by mainframe
		m_selectionHandler->CopySelection();
	}

	m_selectionHandler->ClearSelection();
	BitBltOffscreen();
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::Paste()
{
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
	
	if (::OpenClipboard(m_hWnd))
	{
		HANDLE	hData = ::GetClipboardData(CF_UNICODETEXT);

		SendTextToConsole(reinterpret_cast<wchar_t*>(::GlobalLock(hData)));

		::GlobalUnlock(hData);
		::CloseClipboard();
	}
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::DumpBuffer()
{
	wstring		strText(L"");
	DWORD dwOffset		= 0;

	for (DWORD i = 0; i < m_consoleHandler.GetConsoleParams()->dwRows; ++i)
	{
		for (DWORD j = 0; j < m_consoleHandler.GetConsoleParams()->dwColumns; ++j)
		{
			strText += m_screenBuffer[dwOffset].Char.UnicodeChar;
			++dwOffset;
		}

		strText += L"\n";
	}

	wofstream of;
	of.open("F:\\Smece\\console_dump.log");
	of << strText;
	of.close();
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::OnConsoleChange(bool bResize)
{
	// console size changed, resize offscreen buffers
	if (bResize)
	{
/*
		TRACE(L"================================================================\n");
		TRACE(L"Resizing console wnd: 0x%08X\n", m_hWnd);
*/
		InitializeScrollbars();

		if (m_bActive) RecreateOffscreenBuffers();

		// TODO: put this in console size change handler
		m_screenBuffer.reset(new CHAR_INFO[m_consoleHandler.GetConsoleParams()->dwRows*m_consoleHandler.GetConsoleParams()->dwColumns]);
		::ZeroMemory(m_screenBuffer.get(), sizeof(CHAR_INFO)*m_consoleHandler.GetConsoleParams()->dwRows*m_consoleHandler.GetConsoleParams()->dwColumns);

		// notify parent about resize
		GetParent().SendMessage(UM_CONSOLE_RESIZED, 0, 0);
	}

	UpdateTitle();
	
	// if the view is not visible, don't repaint
	if (!m_bActive) return;

	SharedMemory<CONSOLE_SCREEN_BUFFER_INFO>& consoleInfo = m_consoleHandler.GetConsoleInfo();

	if (m_bShowVScroll)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(si); 
		si.fMask  = SIF_POS; 
		si.nPos   = consoleInfo->srWindow.Top; 
		::FlatSB_SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);

/*
		TRACE(L"----------------------------------------------------------------\n");
		TRACE(L"VScroll pos: %i\n", consoleInfo->srWindow.Top);
*/
	}

	if (m_bShowHScroll)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(si); 
		si.fMask  = SIF_POS; 
		si.nPos   = consoleInfo->srWindow.Left; 
		::FlatSB_SetScrollInfo(m_hWnd, SB_HORZ, &si, TRUE);
	}

	m_selectionHandler->UpdateSelection();
	Repaint();
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::OnConsoleClose()
{
	if (::IsWindow(m_hWnd)) ::PostMessage(GetParent(), UM_CONSOLE_CLOSED, reinterpret_cast<WPARAM>(m_hWnd), 0);
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::CreateOffscreenBuffers()
{
	CWindowDC	dcWindow(m_hWnd);
	CRect		rectWindowMax;
//	CRect		rectWindow;

	// create font
	if (!CreateFont(m_appearanceSettings.fontSettings.strName))
	{
		CreateFont(wstring(L"Courier New"));
	}

	// get ClearType status
	BOOL	bSmoothing		= FALSE;
	UINT	uiSmoothingType= 0;
	CDC		dcDdesktop(::GetDC(NULL));
	
	::SystemParametersInfo(SPI_GETFONTSMOOTHING, 0, (void*)&bSmoothing, 0);
	::SystemParametersInfo(SPI_GETFONTSMOOTHINGTYPE, 0, (void*)&uiSmoothingType, 0);

	if ((dcDdesktop.GetDeviceCaps(BITSPIXEL)*dcDdesktop.GetDeviceCaps(PLANES) == 32) && // 32-bit depth only
		bSmoothing && 
		(uiSmoothingType == FE_FONTSMOOTHINGCLEARTYPE))
	{
		m_bUseTextAlphaBlend = true;
	}
	else
	{
		m_bUseTextAlphaBlend = false;
	}

	// get max window rect based on font and console size
	GetMaxRect(rectWindowMax);
//	GetWindowRect(&rectWindow);

	// create offscreen bitmaps
	CreateOffscreenBitmap(dcWindow, rectWindowMax, m_dcOffscreen, m_bmpOffscreen);
	CreateOffscreenBitmap(dcWindow, rectWindowMax, m_dcText, m_bmpText);

	// create background brush
	if (!m_backgroundBrush.IsNull()) m_backgroundBrush.DeleteObject();
	m_backgroundBrush.CreateSolidBrush(m_tabData->crBackgroundColor);

	// initial offscreen paint
	m_dcOffscreen.FillRect(&rectWindowMax, m_backgroundBrush);

	// set text DC stuff
	m_dcText.SetBkMode(OPAQUE);
	m_dcText.FillRect(&rectWindowMax, m_backgroundBrush);

	// create selection handler
	m_selectionHandler.reset(new SelectionHandler(
									m_hWnd, 
									dcWindow, 
									rectWindowMax, 
									m_consoleHandler.GetConsoleParams(), 
									m_consoleHandler.GetConsoleInfo(), 
									m_consoleHandler.GetCopyInfo(),
									m_nCharWidth, 
									m_nCharHeight, RGB(255, 255, 255)));

	// create and initialize cursor
	CRect		rectCursor(0, 0, m_nCharWidth, m_nCharHeight);

	m_cursor.reset();
	m_cursor = CursorFactory::CreateCursor(
								m_hWnd, 
								m_bAppActive, 
								m_tabData.get() ? static_cast<CursorStyle>(m_tabData->dwCursorStyle) : cstyleXTerm, 
								dcWindow, 
								rectCursor, 
								m_tabData.get() ? static_cast<CursorStyle>(m_tabData->crCursorColor) : RGB(255, 255, 255));
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::CreateOffscreenBitmap(const CWindowDC& dcWindow, const CRect& rect, CDC& cdc, CBitmap& bitmap)
{
	if (!bitmap.IsNull()) return;// bitmap.DeleteObject();

	Helpers::CreateBitmap(dcWindow, rect.Width(), rect.Height(), bitmap);
//	bitmap.CreateCompatibleBitmap(dcWindow, rect.right, rect.bottom);
	cdc.SelectBitmap(bitmap);
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

bool ConsoleView::CreateFont(const wstring& strFontName)
{
	if (!m_fontText.IsNull()) return true;// m_fontText.DeleteObject();
	m_fontText.CreateFont(
		-::MulDiv(m_appearanceSettings.fontSettings.dwSize , m_dcText.GetDeviceCaps(LOGPIXELSY), 72),
		0,
		0,
		0,
		m_appearanceSettings.fontSettings.bBold ? FW_BOLD : 0,
		m_appearanceSettings.fontSettings.bItalic,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,						
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
//		NONANTIALIASED_QUALITY,
		DEFAULT_QUALITY,
		DEFAULT_PITCH,
		strFontName.c_str());

	TEXTMETRIC	textMetric;

	m_dcText.SelectFont(m_fontText);
	m_dcText.GetTextMetrics(&textMetric);

	if (textMetric.tmPitchAndFamily & TMPF_FIXED_PITCH)
	{
		if (!m_fontText.IsNull()) m_fontText.DeleteObject();
		return false;
	}

	// fixed pitch font (TMPF_FIXED_PITCH is cleared!!!)
	m_nCharWidth = textMetric.tmAveCharWidth;
	m_nCharHeight = textMetric.tmHeight;

	return true;
}

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

void ConsoleView::InitializeScrollbars()
{
	SharedMemory<ConsoleParams>& consoleParams = m_consoleHandler.GetConsoleParams();

 	m_bShowVScroll = consoleParams->dwBufferRows > consoleParams->dwRows;
 	m_bShowHScroll = consoleParams->dwBufferColumns > consoleParams->dwColumns;

//	if (m_nScrollbarStyle != FSB_REGULAR_MODE)
	::InitializeFlatSB(m_hWnd);

	::FlatSB_ShowScrollBar(m_hWnd, SB_VERT, m_bShowVScroll);
	::FlatSB_ShowScrollBar(m_hWnd, SB_HORZ, m_bShowHScroll);

/*
	TRACE(L"InitializeScrollbars, console wnd: 0x%08X\n", m_hWnd);
	TRACE(L"Sizes: %i, %i    %i, %i\n", consoleParams->dwRows, consoleParams->dwBufferRows - 1, consoleParams->dwColumns, consoleParams->dwBufferColumns - 1);
	TRACE(L"----------------------------------------------------------------\n");
*/

	if (m_bShowVScroll)
	{
		// set vertical scrollbar stuff
		SCROLLINFO	si ;

		si.cbSize	= sizeof(SCROLLINFO) ;
		si.fMask	= SIF_PAGE | SIF_RANGE ;
		si.nPage	= consoleParams->dwRows;
		si.nMax		= consoleParams->dwBufferRows - 1;
		si.nMin		= 0 ;

		::FlatSB_SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
	}

	if (m_bShowHScroll)
	{
		// set vertical scrollbar stuff
		SCROLLINFO	si ;

		si.cbSize	= sizeof(SCROLLINFO) ;
		si.fMask	= SIF_PAGE | SIF_RANGE ;
		si.nPage	= consoleParams->dwColumns;
		si.nMax		= consoleParams->dwBufferColumns - 1;
		si.nMin		= 0 ;

		::FlatSB_SetScrollInfo(m_hWnd, SB_HORZ, &si, TRUE) ;
	}

/*
	::FlatSB_SetScrollProp(m_hWnd, WSB_PROP_VSTYLE, FSB_FLAT_MODE, TRUE);
	::FlatSB_SetScrollProp(m_hWnd, WSB_PROP_CXVSCROLL , 5, TRUE);
*/

/*
	// set scrollbar properties
	::FlatSB_SetScrollProp(m_hWnd, WSB_PROP_VSTYLE, m_nScrollbarStyle, FALSE);
	::FlatSB_SetScrollProp(m_hWnd, WSB_PROP_VBKGCOLOR, m_crScrollbarColor, FALSE);
	::FlatSB_SetScrollProp(m_hWnd, WSB_PROP_CXVSCROLL , m_nScrollbarWidth, FALSE);
	::FlatSB_SetScrollProp(m_hWnd, WSB_PROP_CYVSCROLL, m_nScrollbarButtonHeight, FALSE);
	::FlatSB_SetScrollProp(m_hWnd, WSB_PROP_CYVTHUMB, m_nScrollbarThunmbHeight, TRUE);
*/
}

//////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::DoScroll(int nType, int nScrollCode, int nThumbPos)
{
	int nCurrentPos = ::FlatSB_GetScrollPos(m_hWnd, nType);
	int nDelta = 0;

	ScrollSettings& scrollSettings = g_settingsHandler->GetBehaviorSettings().scrollSettings;
	
	switch(nScrollCode)
	{ 
		case SB_PAGEUP: /* SB_PAGELEFT */

			if (scrollSettings.dwPageScrollRows > 0)
			{
				nDelta = -static_cast<int>(scrollSettings.dwPageScrollRows);
			}
			else
			{
				nDelta = (nType == SB_VERT) ? -static_cast<int>(m_consoleHandler.GetConsoleParams()->dwRows) : -static_cast<int>(m_consoleHandler.GetConsoleParams()->dwColumns);
			}
			break; 
			
		case SB_PAGEDOWN: /* SB_PAGERIGHT */
			if (scrollSettings.dwPageScrollRows > 0)
			{
				nDelta = static_cast<int>(scrollSettings.dwPageScrollRows);
			}
			else
			{
				nDelta = (nType == SB_VERT) ? static_cast<int>(m_consoleHandler.GetConsoleParams()->dwRows) : static_cast<int>(m_consoleHandler.GetConsoleParams()->dwColumns);
			}
			break; 
			
		case SB_LINEUP: /* SB_LINELEFT */
			nDelta = -1; 
			break; 
			
		case SB_LINEDOWN: /* SB_LINERIGHT */
			nDelta = 1; 
			break; 
			
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION:
			nDelta = nThumbPos - nCurrentPos; 
			break;
			
		case SB_ENDSCROLL:
			return;
			
		default: 
			return;
	}
	
	if (nDelta != 0)
	{
		SharedMemory<SIZE>& newScrollPos = m_consoleHandler.GetNewScrollPos();

		if (nType == SB_VERT)
		{
			newScrollPos->cx = 0;
			newScrollPos->cy = nDelta;
		}
		else
		{
			newScrollPos->cx = nDelta;
			newScrollPos->cy = 0;
		}

		newScrollPos.SetReqEvent();
	}
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

DWORD ConsoleView::GetBufferDifference()
{
	SharedMemory<CHAR_INFO>&	consoleBuffer = m_consoleHandler.GetConsoleBuffer();
	SharedMemoryLock			memLock(consoleBuffer);

	DWORD dwCount				= m_consoleHandler.GetConsoleParams()->dwRows * m_consoleHandler.GetConsoleParams()->dwColumns;
	DWORD dwChangedPositions	= 0;

	for (DWORD i = 0; i < dwCount; ++i)
	{
		if (consoleBuffer[i].Char.UnicodeChar != m_screenBuffer[i].Char.UnicodeChar) ++dwChangedPositions;
	}

	return dwChangedPositions*100/dwCount;
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::UpdateTitle()
{
	CString	strCommandText(L"");

	if (g_settingsHandler->GetAppearanceSettings().windowSettings.bUseConsoleTitle)
	{
		CWindow consoleWnd(m_consoleHandler.GetConsoleParams()->hwndConsoleWindow);
		CString strConsoleTitle(L"");

		consoleWnd.GetWindowText(strConsoleTitle);

		// if we're using console titles, update the title
		if (strConsoleTitle == m_strTitle) return;

		m_strTitle = strConsoleTitle;
		SetWindowText(m_strTitle);
	}
	else
	{
		strCommandText = GetConsoleCommand();
	}

/*
	m_strTitle = strTitleText;
	SetWindowText(m_strTitle);
*/

	GetParent().SendMessage(
					UM_UPDATE_TITLES, 
					reinterpret_cast<WPARAM>(m_hWnd), 
					reinterpret_cast<LPARAM>(LPCTSTR(strCommandText)));
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::Repaint()
{
	// repaint text layer
 	if (GetBufferDifference() > 15)
 	{
		RepaintText();
	}
	else
	{
		RepaintTextChanges();
	}

	BitBltOffscreen();
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::RepaintText()
{
	SIZE	bitmapSize;
	CRect	bitmapRect;

	m_bmpText.GetSize(bitmapSize);
	bitmapRect.left		= 0;
	bitmapRect.top		= 0;
	bitmapRect.right	= bitmapSize.cx;
	bitmapRect.bottom	= bitmapSize.cy;

	m_dcText.FillRect(&bitmapRect, m_backgroundBrush);
	
	StylesSettings&					stylesSettings	= g_settingsHandler->GetAppearanceSettings().stylesSettings;
	SharedMemory<ConsoleParams>&	consoleParams	= m_consoleHandler.GetConsoleParams();

	DWORD dwX			= stylesSettings.dwInsideBorder;
	DWORD dwY			= stylesSettings.dwInsideBorder;
	DWORD dwOffset		= 0;
	
	WORD attrBG;

	// stuff used for caching
	int			nBkMode		= TRANSPARENT;
	COLORREF	crBkColor	= RGB(0, 0, 0);
	COLORREF	crTxtColor	= RGB(0, 0, 0);
	
	bool		bTextOut	= false;
	
	wstring		strText(L"");

	{
		SharedMemory<CHAR_INFO>&	consoleBuffer = m_consoleHandler.GetConsoleBuffer();
		SharedMemoryLock			memLock(consoleBuffer);
		
		::CopyMemory(
			m_screenBuffer.get(), 
			consoleBuffer.Get(), 
			sizeof(CHAR_INFO) * consoleParams->dwRows * consoleParams->dwColumns);
	}

	for (DWORD i = 0; i < consoleParams->dwRows; ++i)
	{
		dwX = stylesSettings.dwInsideBorder;
		dwY = i*m_nCharHeight + stylesSettings.dwInsideBorder;

		nBkMode			= TRANSPARENT;
		crBkColor		= RGB(0, 0, 0);
		crTxtColor		= RGB(0, 0, 0);
		
		bTextOut		= false;
		
		attrBG = (m_screenBuffer[dwOffset].Attributes & 0xFF) >> 4;
		
		// here we decide how to paint text over the background
		if (m_consoleSettings.consoleColors[attrBG] == RGB(0, 0, 0))
		{
			nBkMode		= TRANSPARENT;
		}
		else
		{
			nBkMode		= OPAQUE;
			crBkColor	= m_consoleSettings.consoleColors[attrBG];
		}

		m_dcText.SetBkMode(nBkMode);
		m_dcText.SetBkColor(crBkColor);

		crTxtColor		= m_appearanceSettings.fontSettings.bUseColor ? m_appearanceSettings.fontSettings.crFontColor : m_consoleSettings.consoleColors[m_screenBuffer[dwOffset].Attributes & 0xF];
		m_dcText.SetTextColor(crTxtColor);

		strText = m_screenBuffer[dwOffset].Char.UnicodeChar;
		++dwOffset;

		for (DWORD j = 1; j < consoleParams->dwColumns; ++j, ++dwOffset)
		{
			if (m_screenBuffer[dwOffset].Attributes & COMMON_LVB_TRAILING_BYTE) continue;
			
			attrBG = (m_screenBuffer[dwOffset].Attributes & 0xFF) >> 4;

			if (m_consoleSettings.consoleColors[attrBG] == RGB(0, 0, 0))
			{
				if (nBkMode != TRANSPARENT)
				{
					nBkMode = TRANSPARENT;
					bTextOut = true;
				}
			}
			else
			{
				if (nBkMode != OPAQUE)
				{
					nBkMode = OPAQUE;
					bTextOut = true;
				}
				if (crBkColor != m_consoleSettings.consoleColors[attrBG])
				{
					crBkColor = m_consoleSettings.consoleColors[attrBG];
					bTextOut = true;
				}
			}

			if (crTxtColor != (m_appearanceSettings.fontSettings.bUseColor ? m_appearanceSettings.fontSettings.crFontColor : m_consoleSettings.consoleColors[m_screenBuffer[dwOffset].Attributes & 0xF]))
			{
				crTxtColor = m_appearanceSettings.fontSettings.bUseColor ? m_appearanceSettings.fontSettings.crFontColor : m_consoleSettings.consoleColors[m_screenBuffer[dwOffset].Attributes & 0xF];
				bTextOut = true;
			}

			if (bTextOut)
			{
				m_dcText.TextOut(dwX, dwY, strText.c_str(), static_cast<int>(strText.length()));
				dwX += static_cast<int>(strText.length() * m_nCharWidth);

				m_dcText.SetBkMode(nBkMode);
				m_dcText.SetBkColor(crBkColor);
				m_dcText.SetTextColor(crTxtColor);

				strText = m_screenBuffer[dwOffset].Char.UnicodeChar;
			}
			else
			{
				strText += m_screenBuffer[dwOffset].Char.UnicodeChar;
			}
		}

		if (strText.length() > 0)
		{
			m_dcText.TextOut(dwX, dwY, strText.c_str(), static_cast<int>(strText.length()));
		}
	}
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::RepaintTextChanges()
{
	SIZE	bitmapSize;

	m_bmpText.GetSize(bitmapSize);

	StylesSettings& stylesSettings = g_settingsHandler->GetAppearanceSettings().stylesSettings;

	DWORD	dwX			= stylesSettings.dwInsideBorder;
	DWORD	dwY			= stylesSettings.dwInsideBorder;
	DWORD	dwOffset	= 0;
	
	WORD	attrBG;

	SharedMemory<CHAR_INFO>&	consoleBuffer = m_consoleHandler.GetConsoleBuffer();
	SharedMemoryLock			memLock(consoleBuffer);

	for (DWORD i = 0; i < m_consoleHandler.GetConsoleParams()->dwRows; ++i)
	{
		dwX = stylesSettings.dwInsideBorder;
		dwY = i*m_nCharHeight + stylesSettings.dwInsideBorder;

		for (DWORD j = 0; j < m_consoleHandler.GetConsoleParams()->dwColumns; ++j, ++dwOffset, dwX += m_nCharWidth)
		{
			if (memcmp(&(m_screenBuffer[dwOffset]), &(consoleBuffer[dwOffset]), sizeof(CHAR_INFO)))
			{
				memcpy(&(m_screenBuffer[dwOffset]), &(consoleBuffer[dwOffset]), sizeof(CHAR_INFO));

				if (m_screenBuffer[dwOffset].Attributes & COMMON_LVB_TRAILING_BYTE) continue;

				CRect rect;
				rect.top	= dwY;
				rect.left	= dwX;
				rect.bottom	= dwY + m_nCharHeight;
				// we have to erase two spaces for double-width characters
				rect.right	= (m_screenBuffer[dwOffset].Attributes & COMMON_LVB_LEADING_BYTE) ? dwX + 2*m_nCharWidth : dwX + m_nCharWidth;
				
				m_dcText.FillRect(&rect, m_backgroundBrush);

				attrBG = (m_screenBuffer[dwOffset].Attributes & 0xFF) >> 4;

				// here we decide how to paint text over the background
				if (m_consoleSettings.consoleColors[attrBG] == RGB(0, 0, 0))
				{
					m_dcText.SetBkMode(TRANSPARENT);
				}
				else
				{
					m_dcText.SetBkMode(OPAQUE);
					m_dcText.SetBkColor(m_consoleSettings.consoleColors[attrBG]);
				}
				
				m_dcText.SetBkColor(m_consoleSettings.consoleColors[attrBG]);
				m_dcText.SetTextColor(m_appearanceSettings.fontSettings.bUseColor ? m_appearanceSettings.fontSettings.crFontColor : m_consoleSettings.consoleColors[m_screenBuffer[dwOffset].Attributes & 0xF]);
				m_dcText.TextOut(dwX, dwY, &(m_screenBuffer[dwOffset].Char.UnicodeChar), 1);
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::BitBltOffscreen(bool bOnlyCursor /*= false*/)
{
	CRect			rectBlit;
	StylesSettings& stylesSettings = g_settingsHandler->GetAppearanceSettings().stylesSettings;

	if (bOnlyCursor)
	{
		// blit only cursor
		if ((m_cursor.get() == NULL) || !m_consoleHandler.GetCursorInfo()->bVisible) return;

		SharedMemory<CONSOLE_SCREEN_BUFFER_INFO>& consoleInfo = m_consoleHandler.GetConsoleInfo();

		rectBlit		= m_cursor->GetCursorRect();
		rectBlit.left	+= (consoleInfo->dwCursorPosition.X - consoleInfo->srWindow.Left) * m_nCharWidth + stylesSettings.dwInsideBorder;
		rectBlit.top	+= (consoleInfo->dwCursorPosition.Y - consoleInfo->srWindow.Top) * m_nCharHeight + stylesSettings.dwInsideBorder;
		rectBlit.right	+= (consoleInfo->dwCursorPosition.X - consoleInfo->srWindow.Left) * m_nCharWidth + stylesSettings.dwInsideBorder;
		rectBlit.bottom	+= (consoleInfo->dwCursorPosition.Y - consoleInfo->srWindow.Top) * m_nCharHeight + stylesSettings.dwInsideBorder;
	}
	else
	{
		// blit rect is entire view
		GetClientRect(&rectBlit);
	}

	if ((m_tabData->backgroundImageType == bktypeNone) || !m_tabData->imageData.bRelative)
	{
		// we don't do this for relative backgrounds here
		UpdateOffscreen(rectBlit);
	}

	InvalidateRect(&rectBlit, FALSE);
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::UpdateOffscreen(const CRect& rectBlit)
{
	CRect	rectWindow;
	GetClientRect(&rectWindow);

	if (m_tabData->backgroundImageType != bktypeNone)
	{
		g_imageHandler->UpdateImageBitmap(m_dcOffscreen, rectWindow, m_background);

		if (m_tabData->imageData.bRelative)
		{
			POINT	pointClientScreen = {0, 0};
			ClientToScreen(&pointClientScreen);

			m_dcOffscreen.BitBlt(
							rectBlit.left, 
							rectBlit.top, 
							rectBlit.right, 
							rectBlit.bottom, 
							m_background->dcImage, 
							rectBlit.left + pointClientScreen.x - ::GetSystemMetrics(SM_XVIRTUALSCREEN), 
							rectBlit.top + pointClientScreen.y - ::GetSystemMetrics(SM_YVIRTUALSCREEN), 
							SRCCOPY);

		}
		else
		{
			m_dcOffscreen.BitBlt(
							rectBlit.left, 
							rectBlit.top, 
							rectBlit.right, 
							rectBlit.bottom, 
							m_background->dcImage, 
							rectBlit.left, 
							rectBlit.top, 
							SRCCOPY);
		}

		// if ClearType is active, use AlphaBlend
		if (m_bUseTextAlphaBlend)
		{
			BLENDFUNCTION blendFn;

			blendFn.BlendOp				= AC_SRC_OVER;
			blendFn.BlendFlags			= 0;
			blendFn.SourceConstantAlpha	= 255;
			blendFn.AlphaFormat			= AC_SRC_ALPHA;

			m_dcOffscreen.AlphaBlend(
							rectWindow.left, 
							rectWindow.top, 
							rectWindow.right, 
							rectWindow.bottom, 
							m_dcText, 
							rectWindow.left, 
							rectWindow.top, 
							rectWindow.right, 
							rectWindow.bottom, 
							blendFn);
		}
		else
		{
			// TransparentBlt seems to fail for small rectangles, so we blit entire window here
			m_dcOffscreen.TransparentBlt(
							rectWindow.left, 
							rectWindow.top, 
							rectWindow.right, 
							rectWindow.bottom, 
							m_dcText, 
							rectWindow.left, 
							rectWindow.top, 
							rectWindow.right, 
							rectWindow.bottom, 
							m_tabData->crBackgroundColor);
		}

/*
		BOOL b = m_dcOffscreen.TransparentBlt(
						dc.m_ps.rcPaint.left, 
						dc.m_ps.rcPaint.top, 
						dc.m_ps.rcPaint.right, 
						dc.m_ps.rcPaint.bottom, 
						m_dcText, 
						dc.m_ps.rcPaint.left, 
						dc.m_ps.rcPaint.top, 
						dc.m_ps.rcPaint.right, 
						dc.m_ps.rcPaint.bottom, 
						m_tabData->crBackgroundColor);

		TRACE(L"B: %i, %i\n", b ? 1 : 0, ::GetLastError());
*/

	}
	else
	{
		// if ClearType is active, use AlphaBlend
		if (m_bUseTextAlphaBlend)
		{
			BLENDFUNCTION blendFn;

			blendFn.BlendOp				= AC_SRC_OVER;
			blendFn.BlendFlags			= 0;
			blendFn.SourceConstantAlpha	= 255;
			blendFn.AlphaFormat			= AC_SRC_ALPHA;

			m_dcOffscreen.FillRect(rectBlit, m_backgroundBrush);
			m_dcOffscreen.AlphaBlend(
							rectBlit.left, 
							rectBlit.top, 
							rectBlit.right, 
							rectBlit.bottom, 
							m_dcText, 
							rectBlit.left, 
							rectBlit.top, 
							rectBlit.right, 
							rectBlit.bottom, 
							blendFn);
		}
		else
		{
			m_dcOffscreen.BitBlt(
							rectBlit.left, 
							rectBlit.top, 
							rectBlit.right, 
							rectBlit.bottom, 
							m_dcText, 
							rectBlit.left, 
							rectBlit.top, 
							SRCCOPY);
		}
	}

	// blit cursor
	if (m_consoleHandler.GetCursorInfo()->bVisible && (m_cursor.get() != NULL))
	{
		CRect			rectCursor(0, 0, 0, 0);
		SharedMemory<CONSOLE_SCREEN_BUFFER_INFO>& consoleInfo = m_consoleHandler.GetConsoleInfo();
		StylesSettings& stylesSettings = g_settingsHandler->GetAppearanceSettings().stylesSettings;

		// don't blit if cursor is outside visible window
		if ((consoleInfo->dwCursorPosition.X >= consoleInfo->srWindow.Left) &&
			(consoleInfo->dwCursorPosition.X <= consoleInfo->srWindow.Right) &&
			(consoleInfo->dwCursorPosition.Y >= consoleInfo->srWindow.Top) &&
			(consoleInfo->dwCursorPosition.Y <= consoleInfo->srWindow.Bottom))
		{
			rectCursor			= m_cursor->GetCursorRect();
			rectCursor.left		+= (consoleInfo->dwCursorPosition.X - consoleInfo->srWindow.Left) * m_nCharWidth + stylesSettings.dwInsideBorder;
			rectCursor.top		+= (consoleInfo->dwCursorPosition.Y - consoleInfo->srWindow.Top) * m_nCharHeight + stylesSettings.dwInsideBorder;
			rectCursor.right	+= (consoleInfo->dwCursorPosition.X - consoleInfo->srWindow.Left) * m_nCharWidth + stylesSettings.dwInsideBorder;
			rectCursor.bottom	+= (consoleInfo->dwCursorPosition.Y - consoleInfo->srWindow.Top) * m_nCharHeight + stylesSettings.dwInsideBorder;

			m_cursor->BitBlt(
						m_dcOffscreen, 
						rectCursor.left, 
						rectCursor.top);
		}
	}

	// blit selection
	m_selectionHandler->BitBlt(m_dcOffscreen);
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::SendTextToConsole(const wchar_t* pszText)
{
	if (!pszText || (wcslen(pszText) == 0)) return;

	SharedMemoryLock memLock(m_consoleHandler.GetPasteInfo());

	void* pRemoteMemory = ::VirtualAllocEx(
								m_consoleHandler.GetConsoleHandle().get(),
								NULL, 
								(wcslen(pszText)+1)*sizeof(wchar_t), 
								MEM_COMMIT, 
								PAGE_READWRITE);

	if (pRemoteMemory == NULL) return;

	if (!::WriteProcessMemory(
				m_consoleHandler.GetConsoleHandle().get(),
				pRemoteMemory, 
				(PVOID)pszText, 
				(wcslen(pszText)+1)*sizeof(wchar_t), 
				NULL))
	{
		::VirtualFreeEx(m_consoleHandler.GetConsoleHandle().get(), pRemoteMemory, NULL, MEM_RELEASE);
		return;
	}

	m_consoleHandler.GetPasteInfo() = reinterpret_cast<UINT_PTR>(pRemoteMemory);
	m_consoleHandler.GetPasteInfo().SetReqEvent();
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

bool ConsoleView::TranslateKeyDown(UINT uMsg, WPARAM wParam, LPARAM /*lParam*/)
{
	if (uMsg == WM_KEYDOWN)
	{
		if (m_hotkeys.bUseScrollLock && ((::GetKeyState(VK_SCROLL) & 0x01) == 0x01))
		{
			switch(wParam)
			{
				case VK_UP:   
					DoScroll(SB_VERT, SB_LINEUP, 0); 
					return true;

				case VK_PRIOR:  
					DoScroll(SB_VERT, SB_PAGEUP, 0); 
					return true;

				case VK_DOWN: 
					DoScroll(SB_VERT, SB_LINEDOWN, 0); 
					return true;

				case VK_NEXT: 
					DoScroll(SB_VERT, SB_PAGEDOWN, 0); 
					return true;

				case VK_LEFT: 
					DoScroll(SB_HORZ, SB_LINELEFT, 0); 
					return true;

				case VK_RIGHT:  
					DoScroll(SB_HORZ, SB_LINERIGHT, 0); 
					return true;
			}
		}
	}
	
	if ((uMsg == WM_SYSKEYDOWN) || (uMsg == WM_SYSKEYUP))
	{
		// eat ALT+ENTER
		if ((wParam == VK_RETURN) && ((::GetKeyState(VK_MENU) & 0x80) == 0x80))
		{
			return true;
		}
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::HandleMouseClick(UINT uMsg, UINT uiFlags, const CPoint& point)
{
	MouseSettings&				mouseSettings	= g_settingsHandler->GetMouseSettings();
	bool						bMouseButtonUp	= false;
	MouseSettings::Action		mouseAction;

	// get modifiers
	if (uiFlags & MK_CONTROL)		mouseAction.modifiers |= MouseSettings::mkCtrl;
	if (uiFlags & MK_SHIFT)			mouseAction.modifiers |= MouseSettings::mkShift;
	if (GetKeyState(VK_MENU) < 0)	mouseAction.modifiers |= MouseSettings::mkAlt;

	// get mouse button
	switch (uMsg)
	{
		case WM_LBUTTONDOWN :
		case WM_LBUTTONUP :
		case WM_LBUTTONDBLCLK :
			mouseAction.button = MouseSettings::btnLeft;
			break;

		case WM_RBUTTONDOWN :
		case WM_RBUTTONUP :
		case WM_RBUTTONDBLCLK :
			mouseAction.button = MouseSettings::btnRight;
			break;

		case WM_MBUTTONDOWN :
		case WM_MBUTTONUP :
		case WM_MBUTTONDBLCLK :
			mouseAction.button = MouseSettings::btnMiddle;
			break;
	}

	// get click type
	switch (uMsg)
	{
		case WM_LBUTTONDOWN :
		case WM_RBUTTONDOWN :
		case WM_MBUTTONDOWN :
			mouseAction.clickType = MouseSettings::clickSingle;
			break;

		case WM_LBUTTONDBLCLK :
		case WM_RBUTTONDBLCLK :
		case WM_MBUTTONDBLCLK :
			mouseAction.clickType = MouseSettings::clickDouble;
			break;

		case WM_LBUTTONUP :
		case WM_RBUTTONUP :
		case WM_MBUTTONUP :
			bMouseButtonUp = true;
			break;
	}


	do
	{
		if (m_mouseCommand == MouseSettings::cmdNone)
		{
			MouseSettings::CommandsSequence::iterator it2 = mouseSettings.commands.begin();

			for (; it2 != mouseSettings.commands.end(); ++it2)
			{
				MouseSettings::CommandData& dd = *(*it2);
				DWORD a = 0;
			}

			// no current mouse action, see if we have a command defined
			typedef MouseSettings::Commands::index<MouseSettings::commandAction>::type	CommandActionIndex;

			CommandActionIndex::iterator it = mouseSettings.commands.get<MouseSettings::commandAction>().find(mouseAction);
			if (it != mouseSettings.commands.get<MouseSettings::commandAction>().end())
			{
				if ((m_selectionHandler->GetState() == SelectionHandler::selstateSelected) &&
					((*it)->command == MouseSettings::cmdCopy))
				{
					TRACE(L"Copy 1\n");
					m_mouseCommand = MouseSettings::cmdCopy;
					return;
				}
				else if ((m_selectionHandler->GetState() == SelectionHandler::selstateNoSelection) &&
						(((*it)->command == MouseSettings::cmdSelect)))
				{
					TRACE(L"Sel 1\n");
					m_selectionHandler->StartSelection(GetConsoleCoord(point), m_screenBuffer);

					m_mouseCommand = MouseSettings::cmdSelect;
					return;
				}
			}
		}
		else
		{
			// we have an active command, handle it...
			switch (m_mouseCommand)
			{
				case MouseSettings::cmdCopy :
				{
					TRACE(L"Copy 2\n");
					Copy(&point);
					break;
				}

				case MouseSettings::cmdSelect :
				{
					if (m_selectionHandler->GetState() == SelectionHandler::selstateStartedSelecting)
					{
						TRACE(L"Sel 2\n");
						m_selectionHandler->EndSelection();
						m_selectionHandler->ClearSelection();
					}
					else if (m_selectionHandler->GetState() == SelectionHandler::selstateSelecting)
					{
						TRACE(L"Sel 3\n");
						m_selectionHandler->EndSelection();

						// copy on select
						if (g_settingsHandler->GetBehaviorSettings().copyPasteSettings.bCopyOnSelect)
						{
							Copy(NULL);
						}
					}

					break;
				}

				default :
				{
					if (bMouseButtonUp) return;
				}
			}

			TRACE(L"Here!\n");
			m_mouseCommand = MouseSettings::cmdNone;
			return;
		}

	} while (false);

	ForwardMouseClick(uMsg, point);


/*
	do
	{
		if (m_mouseAction == MouseSettings::actionNone)
		{
			// no current mouse action, see if we have a command defined
			MouseSettings::Actions::iterator it = mouseSettings.m_actions.begin();
			for (; it != mouseSettings.m_actions.end(); ++it)
			{
				if ((it->second.button == mouseButton) && 
					(it->second.modifiers == dwModifiers) &&
					(it->second.clickType == clickType))
				{
					break;
				}
			}

			if (it == mouseSettings.m_actions.end()) break;

			if (it->second.action == MouseSettings::actionDrag)
			{
				// mouse drag
				// selection active, return
				if (m_selectionHandler->GetState() > SelectionHandler::selstateNoSelection) return;

				CPoint clientPoint(point);

				ClientToScreen(&clientPoint);
				GetParent().PostMessage(UM_START_MOUSE_DRAG, static_cast<WPARAM>(uiFlags), MAKELPARAM(clientPoint.x, clientPoint.y));

				return;
			}
			else
			{
			}

				case MouseSettings::actionSelect :
				{
					if (m_selectionHandler->GetState() == SelectionHandler::selstateSelected) return;

					m_selectionHandler->StartSelection(GetConsoleCoord(point), m_screenBuffer);
					m_mouseAction = MouseSettings::actionSelect;

					return;
				}
			}
		}
		else
		{
		}
	}
	while(false);

	ForwardMouseClick(uMsg, point);
*/

/*
	MouseSettings::Actions::iterator it = mouseSettings.m_actions.begin();
	for (; it != mouseSettings.m_actions.end(); ++it)
	{
		if ((it->second.button == mouseButton) && 
			(it->second.modifiers == dwModifiers) &&
			(it->second.clickType == clickType))
		{
			break;
		}
	}


	// command not found
	if (it == mouseSettings.m_actions.end())
	{
		// mouse button up, see if we need to end an action
		if (clickType == 2)
		{
			// 'select' action...
			it = mouseSettings.m_actions.find(L"select");
			ATLASSERT(it != mouseSettings.m_actions.end());

			if (mouseButton == it->second.button)
			{
				if (m_selectionHandler->GetState() == SelectionHandler::selstateStartedSelecting)
				{
					m_selectionHandler->EndSelection();
					m_selectionHandler->ClearSelection();
				}
				else if (m_selectionHandler->GetState() == SelectionHandler::selstateSelected)
				{
					Copy(&point);
				}
				else if (m_selectionHandler->GetState() == SelectionHandler::selstateSelecting)
				{
					m_selectionHandler->EndSelection();

					// copy on select
					if (g_settingsHandler->GetBehaviorSettings().copyPasteSettings.bCopyOnSelect)
					{
						Copy(NULL);
					}
				}

				return;
			}
		}

		ForwardMouseClick(uMsg, point);
		return;
	}

	if (it->first == L"drag")
	{
		// mouse drag
		// selection active, return
		if (m_selectionHandler->GetState() > SelectionHandler::selstateNoSelection) return;

		CPoint clientPoint(point);

		ClientToScreen(&clientPoint);
		GetParent().PostMessage(UM_START_MOUSE_DRAG, static_cast<WPARAM>(uiFlags), MAKELPARAM(clientPoint.x, clientPoint.y));
	}
	else if (it->first == L"select")
	{
		if (clickType == 1)
		{
		}
		else if (clickType == 2)
		{
			if (m_selectionHandler->GetState() == SelectionHandler::selstateStartedSelecting)
			{
				m_selectionHandler->EndSelection();
				m_selectionHandler->ClearSelection();
			}
			else if (m_selectionHandler->GetState() == SelectionHandler::selstateSelected)
			{
				Copy(&point);
			}
			else if (m_selectionHandler->GetState() == SelectionHandler::selstateSelecting)
			{
				m_selectionHandler->EndSelection();

				// copy on select
				if (g_settingsHandler->GetBehaviorSettings().copyPasteSettings.bCopyOnSelect)
				{
					Copy(NULL);
				}
			}
		}
	}
*/
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void ConsoleView::ForwardMouseClick(UINT uMsg, const CPoint& point)
{
	DWORD dwMouseButtonState= 0;
	DWORD dwControlKeyState	= 0;
	DWORD dwEventFlags		= 0;


	switch (uMsg)
	{
		case WM_LBUTTONDOWN : 
		{
			dwMouseButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
			break;
		}

		case WM_LBUTTONDBLCLK : 
		{
			dwMouseButtonState	 = FROM_LEFT_1ST_BUTTON_PRESSED;
			dwEventFlags		|= DOUBLE_CLICK;
			break;
		}
	}

	// get control key states
	if (GetKeyState(VK_RMENU) < 0)		dwControlKeyState |= RIGHT_ALT_PRESSED;
	if (GetKeyState(VK_LMENU) < 0)		dwControlKeyState |= LEFT_ALT_PRESSED;
	if (GetKeyState(VK_RCONTROL) < 0)	dwControlKeyState |= RIGHT_CTRL_PRESSED;
	if (GetKeyState(VK_LCONTROL) < 0)	dwControlKeyState |= LEFT_CTRL_PRESSED;
//	if (GetKeyState(VK_CAPITAL) < 0)	dwControlKeyState |= CAPSLOCK_ON;
//	if (GetKeyState(VK_NUMLOCK) < 0)	dwControlKeyState |= NUMLOCK_ON;
//	if (GetKeyState(VK_SCROLL) < 0)		dwControlKeyState |= SCROLLLOCK_ON;
	if (GetKeyState(VK_SHIFT) < 0)		dwControlKeyState |= SHIFT_PRESSED;


	m_consoleHandler.SendMouseEvent(GetConsoleCoord(point), dwMouseButtonState, dwControlKeyState, dwEventFlags);
}

/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

COORD ConsoleView::GetConsoleCoord(const CPoint& clientPoint)
{
	StylesSettings& stylesSettings	= g_settingsHandler->GetAppearanceSettings().stylesSettings;

	DWORD			dwColumns		= m_consoleHandler.GetConsoleParams()->dwColumns;
	DWORD			dwBufferColumns	= m_consoleHandler.GetConsoleParams()->dwBufferColumns;
	SMALL_RECT&		srWindow		= m_consoleHandler.GetConsoleInfo()->srWindow;

	CPoint			point(clientPoint);
	COORD			consolePoint;
	SHORT			maxX = (dwBufferColumns > 0) ? static_cast<SHORT>(dwBufferColumns - 1) : static_cast<SHORT>(dwColumns - 1);

	consolePoint.X = static_cast<SHORT>((point.x - static_cast<LONG>(stylesSettings.dwInsideBorder)) / m_nCharWidth + srWindow.Left);
	consolePoint.Y = static_cast<SHORT>((point.y - static_cast<LONG>(stylesSettings.dwInsideBorder)) / m_nCharHeight + srWindow.Top);

	if (consolePoint.X < 0)
	{
		consolePoint.X = maxX;
		--consolePoint.Y;
	}

	if (consolePoint.X > srWindow.Right) consolePoint.X = srWindow.Right;

	if (consolePoint.Y < 0) consolePoint.Y = 0;

	if (consolePoint.Y > srWindow.Bottom)
	{
		consolePoint.X = maxX;
		consolePoint.Y = srWindow.Bottom;
	}

	return consolePoint;
}

/////////////////////////////////////////////////////////////////////////////

