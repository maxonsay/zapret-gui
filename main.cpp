#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <fstream>
#include <shellapi.h>

#define IDC_COMBO_METHODS 101
#define IDC_BTN_START     102
#define IDC_BTN_SERVICE   103
#define IDC_CHECK_PIN     104
#define IDC_CHECK_AUTORUN 105

#define WM_TRAYICON       (WM_USER + 1)
#define ID_TRAY_EXIT      201
#define ID_TRAY_SHOW      202

HWND hCombo, hBtnStart, hBtnService, hCheckPin, hCheckAutorun;
std::vector<std::wstring> batFiles;
const std::wstring PIN_FILE = L"gui_config.txt";
NOTIFYICONDATAW nid = { 0 };
bool isClosing = false;

bool IsAutoRunEnabled() {
    HKEY hKey;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (res == ERROR_SUCCESS) {
        WCHAR path[MAX_PATH];
        DWORD size = sizeof(path);
        res = RegQueryValueExW(hKey, L"ZapretControlPanel", NULL, NULL, (LPBYTE)path, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void SetAutoRun(bool enable) {
    HKEY hKey;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey);
    if (res == ERROR_SUCCESS) {
        if (enable) {
            WCHAR path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            std::wstring pathWithArgs = L"\"" + std::wstring(path) + L"\" --minimized";
            RegSetValueExW(hKey, L"ZapretControlPanel", 0, REG_SZ, (BYTE*)pathWithArgs.c_str(), (DWORD)((pathWithArgs.length() + 1) * sizeof(WCHAR)));
        }
        else {
            RegDeleteValueW(hKey, L"ZapretControlPanel");
        }
        RegCloseKey(hKey);
    }
}

void FindBatFiles() {
    batFiles.clear();
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(L"general*.bat", &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            batFiles.push_back(findData.cFileName);
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
}

void RunProcess(const std::wstring& filename, bool runAsAdmin) {
    if (runAsAdmin) {
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = filename.c_str();
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
    }
    else {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        std::wstring cmd = L"cmd.exe /c \"" + filename + L"\"";
        if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else {
            MessageBoxW(NULL, (L"Не удалось запустить " + filename).c_str(), L"Ошибка", MB_ICONERROR);
        }
    }
}

void SavePinnedMethod(const std::wstring& method) {
    std::wofstream out(PIN_FILE);
    if (out.is_open()) {
        out << method;
    }
}

std::wstring LoadPinnedMethod() {
    std::wifstream in(PIN_FILE);
    std::wstring method;
    if (in.is_open()) {
        std::getline(in, method);
    }
    return method;
}

void AddTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Управление Zapret");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        HWND hTitle = CreateWindowW(L"STATIC", L"Управление Zapret", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 410, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial"), TRUE);

        hCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 20, 50, 390, 200, hwnd, (HMENU)IDC_COMBO_METHODS, NULL, NULL);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

        FindBatFiles();
        for (const auto& file : batFiles) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)file.c_str());
        }

        hCheckPin = CreateWindowW(L"BUTTON", L"Закрепить этот метод по умолчанию", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 85, 390, 20, hwnd, (HMENU)IDC_CHECK_PIN, NULL, NULL);
        SendMessageW(hCheckPin, WM_SETFONT, (WPARAM)hFont, TRUE);

        hCheckAutorun = CreateWindowW(L"BUTTON", L"Запускать вместе с Windows", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 110, 390, 20, hwnd, (HMENU)IDC_CHECK_AUTORUN, NULL, NULL);
        SendMessageW(hCheckAutorun, WM_SETFONT, (WPARAM)hFont, TRUE);
        if (IsAutoRunEnabled()) {
            SendMessageW(hCheckAutorun, BM_SETCHECK, BST_CHECKED, 0);
        }

        std::wstring pinned = LoadPinnedMethod();
        int index = 0;
        if (!pinned.empty()) {
            index = (int)SendMessageW(hCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)pinned.c_str());
            if (index != CB_ERR) {
                SendMessageW(hCheckPin, BM_SETCHECK, BST_CHECKED, 0);
            }
            else {
                index = 0;
            }
        }
        SendMessageW(hCombo, CB_SETCURSEL, index, 0);

        hBtnStart = CreateWindowW(L"BUTTON", L"🚀 ЗАПУСТИТЬ ВЫБРАННЫЙ МЕТОД", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 145, 390, 40, hwnd, (HMENU)IDC_BTN_START, NULL, NULL);
        SendMessageW(hBtnStart, WM_SETFONT, (WPARAM)hFont, TRUE);

        hBtnService = CreateWindowW(L"BUTTON", L"Настройка службы", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 205, 390, 40, hwnd, (HMENU)IDC_BTN_SERVICE, NULL, NULL);
        SendMessageW(hBtnService, WM_SETFONT, (WPARAM)hFont, TRUE);
        Button_SetElevationRequiredState(hBtnService, TRUE);

        AddTrayIcon(hwnd);
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (wmId == IDC_BTN_START) {
            int idx = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (idx != CB_ERR) {
                wchar_t buffer[256];
                SendMessageW(hCombo, CB_GETLBTEXT, idx, (LPARAM)buffer);
                RunProcess(buffer, false);
            }
        }
        else if (wmId == IDC_BTN_SERVICE) {
            if (GetFileAttributesW(L"service.bat") != INVALID_FILE_ATTRIBUTES) {
                RunProcess(L"service.bat", true);
            }
            else {
                MessageBoxW(hwnd, L"Файл service.bat не найден!", L"Ошибка", MB_ICONERROR);
            }
        }
        else if (wmId == IDC_CHECK_PIN) {
            LRESULT isChecked = SendMessageW(hCheckPin, BM_GETCHECK, 0, 0);
            if (isChecked == BST_CHECKED) {
                int idx = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) {
                    wchar_t buffer[256];
                    SendMessageW(hCombo, CB_GETLBTEXT, idx, (LPARAM)buffer);
                    SavePinnedMethod(buffer);
                }
            }
            else {
                SavePinnedMethod(L"");
            }
        }
        else if (wmId == IDC_CHECK_AUTORUN) {
            LRESULT isChecked = SendMessageW(hCheckAutorun, BM_GETCHECK, 0, 0);
            SetAutoRun(isChecked == BST_CHECKED);
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        else if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_SHOW, L"Открыть окно");
            InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Выход");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            int trackId = TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);

            if (trackId == ID_TRAY_SHOW) {
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_RESTORE);
            }
            else if (trackId == ID_TRAY_EXIT) {
                isClosing = true;
                DestroyWindow(hwnd);
            }
            DestroyMenu(hMenu);
        }
        break;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        break;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    InitCommonControls();

    // Исправление рабочей папки при автозагрузке:
    WCHAR exepath[MAX_PATH];
    GetModuleFileNameW(NULL, exepath, MAX_PATH);
    std::wstring exedir = exepath;
    size_t pos = exedir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        exedir = exedir.substr(0, pos);
        SetCurrentDirectoryW(exedir.c_str()); // Принудительно ставим родную папку Zapret рабочей
    }

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ZapretControlPanel";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"ZapretControlPanel", L"Zapret Control Panel",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 310, NULL, NULL, hInstance, NULL);

    std::wstring cmdLine(lpCmdLine);
    bool startMinimized = (cmdLine.find(L"--minimized") != std::wstring::npos);

    if (startMinimized) {
        ShowWindow(hwnd, SW_HIDE);
        std::wstring pinned = LoadPinnedMethod();
        if (!pinned.empty()) {
            if (GetFileAttributesW(pinned.c_str()) != INVALID_FILE_ATTRIBUTES) {
                RunProcess(pinned, false);
            }
        }
    }
    else {
        ShowWindow(hwnd, nCmdShow);
    }

    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}