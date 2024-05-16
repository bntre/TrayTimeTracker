
#include <windows.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <psapi.h>
#include <strsafe.h>
#include <codecvt>


#define TRAYICON_ID 0
#define	WM_TRAYICON_CALLBACK (WM_APP + 10)

#define TIMER_ID 0


//-----------------------------------------------------------------------
// Utils
#pragma region Utils

std::wstring GetCurrentProcessDirectory() {
	WCHAR szPath[MAX_PATH];
	if (::GetModuleFileNameW(NULL, szPath, MAX_PATH) == 0) return L"";
	std::wstring wPath = std::wstring(szPath);
	return wPath.substr(0, wPath.find_last_of('\\'));
}

std::string ConvertString16to8(const std::wstring &ws) {
	return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().to_bytes(ws);
}

uint32_t DayTimeToMs(const SYSTEMTIME &st) {
	return ((st.wHour * 60 + st.wMinute) * 60 + st.wSecond) * 1000 + st.wMilliseconds;
}

void MsToDayTime(uint32_t uTimeMs, SYSTEMTIME &st) {
	st.wMilliseconds = uTimeMs % 1000; uTimeMs /= 1000;
	st.wSecond       = uTimeMs % 60;   uTimeMs /= 60;
	st.wMinute       = uTimeMs % 60;   uTimeMs /= 60;
	st.wHour         = uTimeMs % 24;
}

std::string FormatDayTime(uint32_t uTimeMs) {
	char sz[0x10+1] = { 0 };
	SYSTEMTIME tt = { 0 };
	MsToDayTime(uTimeMs, tt);
	::StringCbPrintfA(sz, 0x10, "%02d:%02d:%02d", tt.wHour, tt.wMinute, tt.wSecond);
	return sz;
}


#if _DEBUG
void LogDebugString(const char *szFormat, ...) { //!!! not used
	va_list args;
	va_start(args, szFormat);
	va_list args2;
	va_copy(args2, args);
	int iSize = std::vsnprintf(nullptr, 0, szFormat, args);
	va_end(args);
	std::vector<char> buf(iSize + 2); // '\n' '\0'
	std::vsnprintf(buf.data(), buf.size(), szFormat, args2);
	va_end(args2);
	buf[iSize] = '\n';
	::OutputDebugStringA(buf.data());
}
#define TRACE_DEBUG LogDebugString
#else
#define TRACE_DEBUG
#endif

#pragma endregion Utils

//-----------------------------------------------------------------------
// Notify Icon
#pragma region Notify Icon

NOTIFYICONDATA g_iconData = { 0 };

void CreateTrayIcon(HWND hWnd) {
	g_iconData.cbSize = sizeof(NOTIFYICONDATA);
	g_iconData.hWnd = hWnd;
	g_iconData.uID = TRAYICON_ID;
	g_iconData.uFlags = NIF_MESSAGE | NIF_ICON;
	g_iconData.uCallbackMessage = WM_TRAYICON_CALLBACK;
	g_iconData.hIcon = LoadIcon(NULL, IDI_INFORMATION);
	::Shell_NotifyIcon(NIM_ADD, &g_iconData);
}

void ShowNotification(const std::wstring &sMessage) {
	wcscpy_s(g_iconData.szInfo, sMessage.c_str());
	g_iconData.dwInfoFlags = NIIF_WARNING;
	g_iconData.uFlags = NIF_INFO;
	::Shell_NotifyIcon(NIM_MODIFY, &g_iconData);
}

void DeleteTrayIcon() {
	g_iconData.uFlags = 0;
	::Shell_NotifyIcon(NIM_DELETE, &g_iconData);
}

#pragma endregion Notify Icon

//-----------------------------------------------------------------------
// Config
#pragma region Config

const std::wstring c_sAppDir = GetCurrentProcessDirectory();

const std::wstring c_sConfigFile = c_sAppDir + L"\\TrayTimeTracker_config.ini";
//!!! Save config to registry!

struct SConfig
{
	uint32_t uVersion = 0; // positive when initialized

	uint32_t uDebugMode = 0;

	uint32_t uTimerElapseMs = 15000;

	// Limits
	uint32_t uScreenTimeLimitMs = 0; // per day
	uint32_t uShutdownTimeMs = 0; // stopping screen time in evening

	struct STask {
		uint32_t uId = 0; // positive; 0 for unset
		std::string sKey; // e.g. "youtube" - plain!?
		std::wstring sProcessName; // e.g. "chrome.exe"
		std::wstring sWindowTitlePart; // optional; e.g. "YouTube"
		std::string sClosingCode; // e.g. "alt+f4"; !!! not used
	};
	std::vector<STask> tasks; // single (passive screen time) category only
};

SConfig g_config;

bool ReadConfig()
{	
	g_config.uVersion             = ::GetPrivateProfileIntW(L"Main", L"configVersion", 0, c_sConfigFile.c_str());
	if (g_config.uVersion == 0) return false; // config file is missing or invalid
	if (g_config.uVersion > 1) return false; // unsupported version

	g_config.uDebugMode           = ::GetPrivateProfileIntW(L"Main", L"debugMode", 0, c_sConfigFile.c_str());

	g_config.uTimerElapseMs       = ::GetPrivateProfileIntW(L"Main", L"timerMs", g_config.uTimerElapseMs, c_sConfigFile.c_str());
	
	// Limits
	g_config.uScreenTimeLimitMs   = ::GetPrivateProfileIntW(L"Main", L"limitMinutes",        0, c_sConfigFile.c_str()) * 60 * 1000;
	g_config.uShutdownTimeMs      = ::GetPrivateProfileIntW(L"Main", L"eveningShutdownHour", 0, c_sConfigFile.c_str()) * 60 * 60 * 1000;
	
	// Tasks
	WCHAR szTasks[0x1000+1] = {0};
	::GetPrivateProfileSectionW(L"Tasks", szTasks, 0x1000, c_sConfigFile.c_str());
	const WCHAR *p = szTasks;
	uint32_t uTaskId = 1;
	while (*p) {
		std::wstring sLine(p);
		p += sLine.length() + 1;
		size_t u0 = sLine.find(L'=');
		size_t u1 = sLine.find(L',');
		size_t u2 = sLine.find(L',', u1 + 1);
		g_config.tasks.push_back(
			SConfig::STask{ uTaskId++,
				ConvertString16to8(sLine.substr(0, u0)),
				                   sLine.substr(u0+1, u1-u0-1), 
				                   sLine.substr(u1+1, u2-u1-1), 
				ConvertString16to8(sLine.substr(u2+1))
			}
		);
	}

	return true;
}

std::string GetTaskKey(uint32_t uTaskId) {
	return uTaskId == 0 ? "" : g_config.tasks[uTaskId-1].sKey;
}

#pragma endregion Config

//-----------------------------------------------------------------------
// Tracker.
// Most times are in milliseconds from midnight
#pragma region Tracker

const std::wstring c_sStateFile = c_sAppDir + L"\\TrayTimeTracker_state.ini"; //!!! Save state to registry!
const uint32_t c_uOneDayMs = 24 * 60 * 60 * 1000;

class CTracker
{
protected:
	int64_t _iMidnightTicks = 0; // ticks (ms) between "system was started" and "last midnight"; may be negative.
	std::wstring _sCurrentDate; // used for flushing the history

	// State
	uint32_t _uPrevCheckTimeMs = 0; // time of previous Process(..) call
	uint32_t _uPrevTaskId = 0; // 0 for undefined
	uint32_t _uPrevTaskStartMs = 0;
	uint32_t _uTotalScreenTimeMs = 0; // total screen time today
	
	// History
	struct SRecord {
		uint32_t uTaskId = 0;
		uint32_t uBeginMs = 0;
		uint32_t uEndMs = 0;
	};
	std::vector<SRecord> _history;

public:
	uint32_t ResetMidnight() // returns todays time in ms
	{
		uint64_t uSystemTicks = ::GetTickCount64();

		SYSTEMTIME st; ::GetLocalTime(&st);

		uint32_t uTodayMs = DayTimeToMs(st);
		_iMidnightTicks = uSystemTicks - uTodayMs;

		WCHAR szDate[0x10+1] = {0};
		::StringCbPrintfW(szDate, 0x20, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
		_sCurrentDate = std::wstring(szDate);

		return uTodayMs;
	}

	uint32_t Process(uint32_t uCurrentTaskId, uint32_t &uTimeMs) // returns today's total screen time
	{
		uint64_t uSystemTicks = ::GetTickCount64();
		uTimeMs = (uint32_t)(uSystemTicks - _iMidnightTicks);

		if (uTimeMs - _uPrevCheckTimeMs > g_config.uTimerElapseMs * 20) { // long delay, probably PC was sleeping - forget the previous task
			_uPrevTaskId = 0;
		}

		// Check next day coming
		if (uTimeMs >= c_uOneDayMs)
		{
			if (_uPrevTaskId != 0) {
				_uTotalScreenTimeMs += c_uOneDayMs - _uPrevCheckTimeMs;
				_history.push_back(SRecord{ _uPrevTaskId, _uPrevTaskStartMs, c_uOneDayMs });
			}

			FlushHistory();

			uTimeMs = ResetMidnight();

			// Reset tickers; keep _uPrevTaskId
			_uPrevCheckTimeMs = 0;
			_uPrevTaskStartMs = 0;
			_uTotalScreenTimeMs = 0;
		}

		// Update state
		if (_uPrevTaskId != 0) {
			_uTotalScreenTimeMs += uTimeMs - _uPrevCheckTimeMs;
		}

		// Switch task
		if (_uPrevTaskId != uCurrentTaskId) {
			if (g_config.uDebugMode) { //!!! debug
				FlushHistory(false);
				WriteDataToHistory(FormatDayTime(uTimeMs) + " Switching task: " + GetTaskKey(uCurrentTaskId) + " <- " + GetTaskKey(_uPrevTaskId) + "\n");
			}
			// add history record
			if (_uPrevTaskId != 0) {
				_history.push_back(SRecord{ _uPrevTaskId, _uPrevTaskStartMs, uTimeMs });
			}
			// switch task
			_uPrevTaskId = uCurrentTaskId;
			_uPrevTaskStartMs = uTimeMs;
		}

		_uPrevCheckTimeMs = uTimeMs;

		return _uTotalScreenTimeMs;
	}

	std::wstring GetStats() { // Short statistics for info window
		SYSTEMTIME st; MsToDayTime(_uTotalScreenTimeMs, st);
		WCHAR szTime[0x20+1] = { 0 };
		::StringCbPrintfW(szTime, 0x20, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
		return L"Screen time today: " + std::wstring(szTime);
	}

	void SaveState() { // Save today's screen time e.g. on reboot
		if (::WritePrivateProfileStringW(L"Main", L"Date", _sCurrentDate.c_str(), c_sStateFile.c_str())) {
			::WritePrivateProfileStringW(L"Main", L"ScreenTimeMs", std::to_wstring(_uTotalScreenTimeMs).c_str(), c_sStateFile.c_str());
		}
	}

	void LoadState() { // Restore today's screen time e.g. after reboot 
		WCHAR szDate[0x10+1];
		if (::GetPrivateProfileStringW(L"Main", L"Date", L"", szDate, 0x10, c_sStateFile.c_str())) {
			if (_sCurrentDate == szDate) {
				_uTotalScreenTimeMs = (uint32_t)::GetPrivateProfileIntW(L"Main", L"ScreenTimeMs", 0, c_sStateFile.c_str());
			}
		}
	}

	void FlushHistory(bool bAddTotalScreenTime = true) { // Write (or append) the history records to \history\<date>.txt
		if (_history.empty()) return;

		std::string sData;
		char szLine[0x200+1] = { 0 };

		// Add all history records
		for (const SRecord &r : _history) {
			//if (r.uEndMs - r.uBeginMs <= g_config.uTimerElapseMs) continue; //!!! skipping short ranges to avoid spam?
			SYSTEMTIME t0 = {0}, t1 = {0}, td = {0};
			MsToDayTime(r.uBeginMs, t0);
			MsToDayTime(r.uEndMs, t1);
			MsToDayTime(r.uEndMs - r.uBeginMs, td);
			::StringCbPrintfA(szLine, 0x200, "%02d:%02d:%02d - %02d:%02d:%02d (%02d:%02d:%02d): %s\n",
				t0.wHour, t0.wMinute, t0.wSecond,
				t1.wHour, t1.wMinute, t1.wSecond,
				td.wHour, td.wMinute, td.wSecond,
				GetTaskKey(r.uTaskId).c_str()
			);
			sData += szLine;
		}

		_history.clear();

		// Add total screen time
		if (bAddTotalScreenTime) {
			sData += "Total screen time: " + FormatDayTime(_uTotalScreenTimeMs) + "\n";
		}

		// Write (append) to history file
		WriteDataToHistory(sData);
	}

	void WriteDataToHistory(const std::string &sData) {		
		std::wstring sPath = c_sAppDir + L"\\history";
		if (::GetFileAttributesW(sPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
			::CreateDirectoryW(sPath.c_str(), NULL);
		}
		sPath += L"\\" + _sCurrentDate + L".txt";
		HANDLE hFile = ::CreateFileW(sPath.c_str(), FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			DWORD dwBytesWritten = 0;
			::WriteFile(hFile, sData.c_str(), (DWORD)sData.length(), &dwBytesWritten, NULL);
			::CloseHandle(hFile);
		}
	}

};

CTracker g_tracker;

#pragma endregion Tracker

//-----------------------------------------------------------------------
// Windows Processes
#pragma region Processes

bool GetForegroundWindowProcess(HWND &hWnd, std::wstring &sProcessName) {
	hWnd = ::GetForegroundWindow();
	if (!hWnd) return false;

	if (::IsIconic(hWnd)) return false;

	static HWND s_hWndPrev = NULL; // Cache previous values
	static std::wstring s_sProcessNamePrev;
	if (hWnd == s_hWndPrev) {
		sProcessName = s_sProcessNamePrev;
		return true;
	} else {
		s_hWndPrev = hWnd;
		s_sProcessNamePrev.clear();
	}

	DWORD dwPID = 0;
	DWORD dwThreadID = ::GetWindowThreadProcessId(hWnd, &dwPID);
	if (!dwThreadID) return false;

	HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPID);
	if (!hProcess) return false;

	WCHAR szProcessPath[MAX_PATH];
	BOOL bResult = ::GetProcessImageFileNameW(hProcess, (LPWSTR)&szProcessPath, MAX_PATH - 1);

	::CloseHandle(hProcess);

	if (!bResult) return false;

	sProcessName = std::wstring(szProcessPath);
	sProcessName = sProcessName.substr(sProcessName.find_last_of('\\') + 1);

	s_sProcessNamePrev = sProcessName;

	return true;
}

bool GetWindowTitle(HWND hWnd, std::wstring &sTitle) {
	WCHAR szTitle[0x400+1];
	int iLength = ::GetWindowTextW(hWnd, szTitle, 0x400);
	if (!iLength) return false;
	sTitle = std::wstring(szTitle);
	return true;
}

uint32_t GetCurrentTaskId(const std::vector<SConfig::STask> &tasks) {
	//!!! this probably should return all non-minimized windows (EnumWindows), not only the active one
	HWND hWnd = 0;
	std::wstring sProcessName;
	if (GetForegroundWindowProcess(hWnd, sProcessName))
	{
		if (g_config.uDebugMode) { //!!! debug
			std::wstring sTitle;
			if (hWnd) ::GetWindowTitle(hWnd, sTitle);
			SYSTEMTIME st; ::GetLocalTime(&st);
			uint32_t uTodayMs = DayTimeToMs(st);
			g_tracker.FlushHistory(false);
			g_tracker.WriteDataToHistory(FormatDayTime(uTodayMs) + " Current process " + ConvertString16to8(sProcessName) + "; title: " + ConvertString16to8(sTitle) + "\n");
		}

		for (const SConfig::STask &task : tasks) {
			if (task.sProcessName == sProcessName) {
				if (task.sWindowTitlePart.empty()) {
					return task.uId;
				} else {
					std::wstring sTitle;
					::GetWindowTitle(hWnd, sTitle);
					if (sTitle.find(task.sWindowTitlePart) != sTitle.npos) {
						return task.uId;
					}
				}
			}
		}
	}
	return 0;
}

#pragma endregion Processes

//-----------------------------------------------------------------------

void ProcessTimer()
{
	uint32_t uCurrentTaskId = GetCurrentTaskId(g_config.tasks);

	uint32_t uTimeMs = 0; // today's time in ms
	uint32_t uTotalScreenTimeMs = g_tracker.Process(uCurrentTaskId, uTimeMs);

	if (uCurrentTaskId != 0)
	{
		// Today's screen time limit
		if (g_config.uScreenTimeLimitMs != 0 && uTotalScreenTimeMs > g_config.uScreenTimeLimitMs) {
			ShowNotification(L"Today's screen time limit is reached!");
			if (g_config.uDebugMode) {
				g_tracker.FlushHistory(false);
				g_tracker.WriteDataToHistory(FormatDayTime(uTimeMs) + ": Today's screen time limit is reached!\n");
			}
			//!!! close the window?
		}

		// Evening shutdown
		else if (g_config.uShutdownTimeMs != 0 && uTimeMs >= g_config.uShutdownTimeMs) {
			ShowNotification(L"It's evening screen time shutdown!");
			if (g_config.uDebugMode) {
				g_tracker.FlushHistory(false);
				g_tracker.WriteDataToHistory(FormatDayTime(uTimeMs) + ": It's evening screen time shutdown!\n");
			}
			//!!! close the window?
		}
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_TRAYICON_CALLBACK:
		if (wParam != TRAYICON_ID) return 0;

		if (LOWORD(lParam) == WM_LBUTTONUP) {
			g_tracker.SaveState();
			g_tracker.FlushHistory(); // Flush history at the same time
			std::wstring sStats = g_tracker.GetStats();
			ShowNotification(sStats);
		}
#if _DEBUG
		else if (LOWORD(lParam) == WM_RBUTTONUP) { // close by r-click in debug
			::PostMessage(hWnd, WM_CLOSE, 0, 0);
		}
#endif
		return 1;

	case WM_TIMER:
		if (wParam == TIMER_ID) {
			ProcessTimer();
			return 0;
		}
		break;

	case WM_ENDSESSION:
		g_tracker.SaveState();
		g_tracker.FlushHistory();
		break;

	case WM_DESTROY:
		g_tracker.SaveState();
		g_tracker.FlushHistory();
		::PostQuitMessage(0);
		break;

	default:
		return ::DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	//!!! use RegisterApplicationRestart ?

	const WCHAR szClassName[] = L"TTT";

	// Register window class
	WNDCLASSEX wcex = { sizeof(WNDCLASSEX), 0 };
	wcex.lpfnWndProc     = (WNDPROC)WndProc;
	wcex.hInstance       = hInstance;
	wcex.lpszClassName   = szClassName;
	::RegisterClassEx(&wcex);

	HWND hwnd = ::CreateWindowEx(0, szClassName, L"", 0, 0,0,0,0, NULL, NULL, hInstance, NULL);
	if (hwnd == NULL) return 1;

	CreateTrayIcon(hwnd);

	if (!ReadConfig()) {
		ShowNotification(L"Invalid or missing configuration");
	}

	g_tracker.ResetMidnight();
	g_tracker.LoadState();

	::SetTimer(hwnd, TIMER_ID, g_config.uTimerElapseMs, (TIMERPROC)NULL);

	MSG msg = {0};
	while (::GetMessage(&msg, NULL, 0, 0) > 0) {
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}

	DeleteTrayIcon();

	return (int)msg.wParam;
}
