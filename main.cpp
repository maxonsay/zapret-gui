#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <fstream>
#include <shlobj.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

// Идентификаторы элементов управления
#define IDC_BTN_START           1001
#define IDC_BTN_SERVICE         1002
#define IDC_BTN_SET_DIR         1003
#define IDC_BTN_METHOD_SELECT   1004
#define IDC_BTN_THEME_SELECT    1005
#define IDC_CHECK_PIN           1006

// Новые и измененные идентификаторы для TG и Автозапуска
#define IDC_BTN_TG_SET_FILE     1008
#define IDC_BTN_TG_START        1011
#define IDC_CHECK_AUTORUN_GUI   1012
#define IDC_CHECK_AUTORUN_ZPR   1013
#define IDC_CHECK_AUTORUN_TG    1014

#define ID_METHOD_START_RANGE   2000
#define ID_THEME_LIGHT          3001
#define ID_THEME_DARK           3002

#define ID_TRAY_SHOW            4001
#define ID_TRAY_EXIT            4002
#define WM_TRAYICON             (WM_USER + 1)

// Глобальные переменные окон
HWND hTitle, hStaticDir, hBtnSetDir, hBtnMethod, hCheckPin, hBtnStart, hBtnService, hBtnTheme;
HWND hStaticTgFile, hBtnTgSetFile, hBtnTgStart;
HWND hCheckAutorunGui, hCheckAutorunZpr, hCheckAutorunTg;

HBRUSH hDarkBgBrush = NULL;
NOTIFYICONDATAW nid = { 0 };
HANDLE hMutex = NULL;

// Глобальное состояние
bool isDarkTheme = true;
std::wstring zapretDir = L"";
std::wstring pinnedMethod = L"";
std::vector<std::wstring> batFiles;
int selectedMethodIndex = 0;

// Состояние для TG WS Proxy
std::wstring tgExePath = L"";

// Анимация кнопок
bool hBtnStartH = false, hBtnStartP = false;
bool hBtnServiceH = false, hBtnServiceP = false;
bool hBtnSetDirH = false, hBtnSetDirP = false;
bool hBtnMethodH = false, hBtnMethodP = false;
bool hBtnThemeH = false, hBtnThemeP = false;
bool hBtnTgSetFileH = false, hBtnTgSetFileP = false;
bool hBtnTgStartH = false, hBtnTgStartP = false;

// Прототипы
void SaveConfig();
void LoadConfig();
void FindBatFiles();
void UpdateStatusTexts();
void RunProcess(const std::wstring& dir, const std::wstring& name, bool asAdmin);
bool IsAutoRunEnabled(const std::wstring& valueName);
void SetAutoRun(const std::wstring& valueName, const std::wstring& exePath, bool enable);
void ApplyImmersiveDarkMode(HWND hwnd, bool dark);
void AddTrayIcon(HWND hwnd);
std::wstring BrowseForFolder(HWND hwnd);
std::wstring BrowseForFile(HWND hwnd);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::wstring GetConfigPath() {
    wchar_t szPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath))) {
        std::wstring configDir = std::wstring(szPath) + L"\\ZapretGUI";
        CreateDirectoryW(configDir.c_str(), NULL);
        return configDir + L"\\config.txt";
    }
    return L"config.txt";
}

void SaveConfig() {
    std::wofstream file(GetConfigPath());
    if (file.is_open()) {
        file << (isDarkTheme ? 1 : 0) << L"\n";
        file << zapretDir << L"\n";
        file << pinnedMethod << L"\n";
        file << tgExePath << L"\n";
        file.close();
    }
}

void LoadConfig() {
    std::wifstream file(GetConfigPath());
    if (file.is_open()) {
        std::wstring themeStr, dirStr, pinStr, tgPathStr;
        if (std::getline(file, themeStr)) isDarkTheme = (themeStr == L"1");
        if (std::getline(file, dirStr)) zapretDir = dirStr;
        if (std::getline(file, pinStr)) pinnedMethod = pinStr;
        if (std::getline(file, tgPathStr)) tgExePath = tgPathStr;
        file.close();
    }
}

void FindBatFiles() {
    batFiles.clear();
    selectedMethodIndex = 0;
    if (zapretDir.empty()) return;

    std::wstring searchPath = zapretDir + L"\\*.bat";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::wstring filename = fd.cFileName;
            if (filename != L"service.bat" && filename.find(L"service_") == std::wstring::npos) {
                batFiles.push_back(filename);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (!pinnedMethod.empty()) {
        auto it = std::find(batFiles.begin(), batFiles.end(), pinnedMethod);
        if (it != batFiles.end()) {
            selectedMethodIndex = std::distance(batFiles.begin(), it);
        }
    }

    if (!batFiles.empty() && hBtnMethod) {
        SetWindowTextW(hBtnMethod, (batFiles[selectedMethodIndex] + L"  ▼").c_str());
    }
    else if (hBtnMethod) {
        SetWindowTextW(hBtnMethod, L"Методы не найдены  ▼");
    }
}

void UpdateStatusTexts() {
    if (hStaticDir) {
        if (zapretDir.empty()) {
            SetWindowTextW(hStaticDir, L"Папка Zapret: не указана");
        }
        else {
            std::wstring shortDir = zapretDir;
            if (shortDir.length() > 32) shortDir = shortDir.substr(0, 13) + L"..." + shortDir.substr(shortDir.length() - 16);
            SetWindowTextW(hStaticDir, (L"Папка Zapret: " + shortDir).c_str());
        }
    }
    if (hStaticTgFile) {
        if (tgExePath.empty()) {
            SetWindowTextW(hStaticTgFile, L"Файл TG Proxy: не выбран");
        }
        else {
            std::wstring shortPath = tgExePath;
            if (shortPath.length() > 32) shortPath = shortPath.substr(0, 13) + L"..." + shortPath.substr(shortPath.length() - 16);
            SetWindowTextW(hStaticTgFile, (L"Файл TG Proxy: " + shortPath).c_str());
        }
    }
}

void RunProcess(const std::wstring& dir, const std::wstring& name, bool asAdmin) {
    if (name.empty()) return;
    std::wstring runPath = dir.empty() ? name : dir + L"\\" + name;
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = asAdmin ? L"runas" : L"open";
    sei.lpFile = runPath.c_str();
    sei.lpDirectory = dir.empty() ? NULL : dir.c_str();
    sei.nShow = SW_SHOW;
    ShellExecuteExW(&sei);
}

bool IsAutoRunEnabled(const std::wstring& valueName) {
    HKEY hKey;
    bool enabled = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)path, &size) == ERROR_SUCCESS) {
            enabled = true;
        }
        RegCloseKey(hKey);
    }
    return enabled;
}

void SetAutoRun(const std::wstring& valueName, const std::wstring& exePath, bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable && !exePath.empty()) {
            RegSetValueExW(hKey, valueName.c_str(), 0, REG_SZ, (BYTE*)exePath.c_str(), (lstrlenW(exePath.c_str()) + 1) * sizeof(wchar_t));
        }
        else {
            RegDeleteValueW(hKey, valueName.c_str());
        }
        RegCloseKey(hKey);
    }
}

void ApplyImmersiveDarkMode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT(WINAPI* pfnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        pfnDwmSetWindowAttribute pDwmSetWindowAttribute = (pfnDwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (pDwmSetWindowAttribute) pDwmSetWindowAttribute(hwnd, 20, &value, sizeof(value));
        FreeLibrary(hDwm);
    }
}

void AddTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    lstrcpyW(nid.szTip, L"Управление Обходом и TG Proxy");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

std::wstring BrowseForFolder(HWND hwnd) {
    std::wstring result = L"";
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwnd;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpszTitle = L"Выберите папку с Zapret:";
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != NULL) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) result = path;
        CoTaskMemFree(pidl);
    }
    return result;
}

std::wstring BrowseForFile(HWND hwnd) {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Исполняемые файлы (*.exe)\0*.exe\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrTitle = L"Выберите исполняемый файл tgwssproxy:";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) return szFile;
    return L"";
}

LRESULT CALLBACK NewButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    bool* pHovered = nullptr; bool* pPressed = nullptr;
    LONG_PTR id = GetWindowLongPtrW(hwnd, GWLP_ID);

    if (id == IDC_BTN_START) { pHovered = &hBtnStartH; pPressed = &hBtnStartP; }
    else if (id == IDC_BTN_SERVICE) { pHovered = &hBtnServiceH; pPressed = &hBtnServiceP; }
    else if (id == IDC_BTN_SET_DIR) { pHovered = &hBtnSetDirH; pPressed = &hBtnSetDirP; }
    else if (id == IDC_BTN_METHOD_SELECT) { pHovered = &hBtnMethodH; pPressed = &hBtnMethodP; }
    else if (id == IDC_BTN_THEME_SELECT) { pHovered = &hBtnThemeH; pPressed = &hBtnThemeP; }
    else if (id == IDC_BTN_TG_SET_FILE) { pHovered = &hBtnTgSetFileH; pPressed = &hBtnTgSetFileP; }
    else if (id == IDC_BTN_TG_START) { pHovered = &hBtnTgStartH; pPressed = &hBtnTgStartP; }

    if (pHovered && pPressed) {
        switch (msg) {
        case WM_MOUSEMOVE:
            if (!(*pHovered)) {
                *pHovered = true;
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        case WM_MOUSELEAVE: *pHovered = false; InvalidateRect(hwnd, NULL, FALSE); break;
        case WM_LBUTTONDOWN: *pPressed = true; InvalidateRect(hwnd, NULL, FALSE); break;
        case WM_LBUTTONUP: *pPressed = false; InvalidateRect(hwnd, NULL, FALSE); break;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hFontSmall = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        hDarkBgBrush = CreateSolidBrush(RGB(30, 30, 30));

        hTitle = CreateWindowW(L"STATIC", L"Управление Обходом & Прокси", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 410, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial"), TRUE);

        // --- КОНФИГУРАЦИЯ ZAPRET ---
        hStaticDir = CreateWindowW(L"STATIC", L"Папка Zapret: не указана", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 50, 290, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(hStaticDir, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        hBtnSetDir = CreateWindowW(L"BUTTON", L"Обзор...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 320, 47, 90, 22, hwnd, (HMENU)IDC_BTN_SET_DIR, NULL, NULL);
        hBtnMethod = CreateWindowW(L"BUTTON", L"Выбрать метод Zapret...  ▼", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 75, 390, 28, hwnd, (HMENU)IDC_BTN_METHOD_SELECT, NULL, NULL);

        hCheckPin = CreateWindowW(L"BUTTON", L"Закрепить выбранный метод", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 110, 390, 20, hwnd, (HMENU)IDC_CHECK_PIN, NULL, NULL);
        SendMessageW(hCheckPin, WM_SETFONT, (WPARAM)hFont, TRUE);

        hBtnStart = CreateWindowW(L"BUTTON", L"🚀 ЗАПУСТИТЬ МЕТОД ZAPRET", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 135, 390, 38, hwnd, (HMENU)IDC_BTN_START, NULL, NULL);
        hBtnService = CreateWindowW(L"BUTTON", L"🛠️ Настройка службы Zapret", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 180, 390, 38, hwnd, (HMENU)IDC_BTN_SERVICE, NULL, NULL);

        // --- КОНФИГУРАЦИЯ TG WS PROXY ---
        hStaticTgFile = CreateWindowW(L"STATIC", L"Файл TG Proxy: не выбран", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 235, 290, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(hStaticTgFile, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        hBtnTgSetFile = CreateWindowW(L"BUTTON", L"Обзор...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 320, 232, 90, 22, hwnd, (HMENU)IDC_BTN_TG_SET_FILE, NULL, NULL);
        hBtnTgStart = CreateWindowW(L"BUTTON", L"✈️ ЗАПУСТИТЬ TG WS PROXY", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 260, 390, 38, hwnd, (HMENU)IDC_BTN_TG_START, NULL, NULL);

        // --- РАЗДЕЛЬНЫЙ АВТОЗАПУСК С WINDOWS ---
        hCheckAutorunGui = CreateWindowW(L"BUTTON", L"Автозапуск этой графической утилиты", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 315, 390, 20, hwnd, (HMENU)IDC_CHECK_AUTORUN_GUI, NULL, NULL);
        hCheckAutorunZpr = CreateWindowW(L"BUTTON", L"Автозапуск закрепленного батника Zapret", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 340, 390, 20, hwnd, (HMENU)IDC_CHECK_AUTORUN_ZPR, NULL, NULL);
        hCheckAutorunTg = CreateWindowW(L"BUTTON", L"Автозапуск выбранного TG Proxy (.exe)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 365, 390, 20, hwnd, (HMENU)IDC_CHECK_AUTORUN_TG, NULL, NULL);

        SendMessageW(hCheckAutorunGui, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hCheckAutorunZpr, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hCheckAutorunTg, WM_SETFONT, (WPARAM)hFont, TRUE);

        hBtnTheme = CreateWindowW(L"BUTTON", L"🎨 Смена темы...  ▼", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 400, 140, 25, hwnd, (HMENU)IDC_BTN_THEME_SELECT, NULL, NULL);

        UpdateStatusTexts();
        FindBatFiles();

        // Проверка состояний автозагрузки в реестре
        if (IsAutoRunEnabled(L"ZapretGUI")) SendMessageW(hCheckAutorunGui, BM_SETCHECK, BST_CHECKED, 0);
        if (IsAutoRunEnabled(L"ZapretGUI_PinnedBat")) SendMessageW(hCheckAutorunZpr, BM_SETCHECK, BST_CHECKED, 0);
        if (IsAutoRunEnabled(L"ZapretGUI_TgProxy")) SendMessageW(hCheckAutorunTg, BM_SETCHECK, BST_CHECKED, 0);
        if (!pinnedMethod.empty() && !batFiles.empty()) SendMessageW(hCheckPin, BM_SETCHECK, BST_CHECKED, 0);

        SetWindowSubclass(hBtnStart, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnService, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnSetDir, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnMethod, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnTheme, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnTgSetFile, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnTgStart, NewButtonSubclassProc, 0, 0);

        ApplyImmersiveDarkMode(hwnd, isDarkTheme);
        AddTrayIcon(hwnd);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        if (isDarkTheme) {
            SetTextColor(hdc, RGB(240, 240, 240));
            return (LRESULT)hDarkBgBrush;
        }
        else {
            SetTextColor(hdc, RGB(0, 0, 0));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            HDC hdc = pDIS->hDC;
            RECT rect = pDIS->rcItem;

            bool isHovered = false, isPressed = false;
            if (pDIS->CtlID == IDC_BTN_START) { isHovered = hBtnStartH; isPressed = hBtnStartP; }
            else if (pDIS->CtlID == IDC_BTN_SERVICE) { isHovered = hBtnServiceH; isPressed = hBtnServiceP; }
            else if (pDIS->CtlID == IDC_BTN_SET_DIR) { isHovered = hBtnSetDirH; isPressed = hBtnSetDirP; }
            else if (pDIS->CtlID == IDC_BTN_METHOD_SELECT) { isHovered = hBtnMethodH; isPressed = hBtnMethodP; }
            else if (pDIS->CtlID == IDC_BTN_THEME_SELECT) { isHovered = hBtnThemeH; isPressed = hBtnThemeP; }
            else if (pDIS->CtlID == IDC_BTN_TG_SET_FILE) { isHovered = hBtnTgSetFileH; isPressed = hBtnTgSetFileP; }
            else if (pDIS->CtlID == IDC_BTN_TG_START) { isHovered = hBtnTgStartH; isPressed = hBtnTgStartP; }
            else { return DefWindowProcW(hwnd, msg, wParam, lParam); }

            COLORREF bgColor, textColor, borderColor, outerColor;

            if (isDarkTheme) {
                textColor = RGB(255, 255, 255); borderColor = RGB(75, 75, 75); outerColor = RGB(30, 30, 30);
                if (isPressed) bgColor = RGB(45, 45, 45); else if (isHovered) bgColor = RGB(65, 65, 65); else bgColor = RGB(40, 40, 40);
            }
            else {
                textColor = RGB(0, 0, 0); borderColor = RGB(170, 170, 170); outerColor = GetSysColor(COLOR_BTNFACE);
                if (isPressed) bgColor = RGB(205, 205, 205); else if (isHovered) bgColor = RGB(225, 225, 225); else bgColor = RGB(245, 245, 245);
            }

            HBRUSH hOuterBrush = CreateSolidBrush(outerColor); FillRect(hdc, &rect, hOuterBrush); DeleteObject(hOuterBrush);
            HBRUSH hBtnBrush = CreateSolidBrush(bgColor); HPEN hBtnPen = CreatePen(PS_SOLID, 1, borderColor);
            HGDIOBJ oldBrush = SelectObject(hdc, hBtnBrush); HGDIOBJ oldPen = SelectObject(hdc, hBtnPen);

            RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);

            wchar_t text[128]; GetWindowTextW(pDIS->hwndItem, text, 128);
            SetTextColor(hdc, textColor); SetBkMode(hdc, TRANSPARENT);

            HFONT hBtnFont = CreateFontW((pDIS->CtlID == IDC_BTN_SET_DIR || pDIS->CtlID == IDC_BTN_TG_SET_FILE || pDIS->CtlID == IDC_BTN_THEME_SELECT) ? 14 : 16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            HGDIOBJ oldFont = SelectObject(hdc, hBtnFont);

            DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont); DeleteObject(hBtnFont);
            SelectObject(hdc, oldBrush); SelectObject(hdc, oldPen);
            DeleteObject(hBtnBrush); DeleteObject(hBtnPen);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);

        if (wmId == IDC_BTN_METHOD_SELECT) {
            if (batFiles.empty()) break;
            HMENU hMethodMenu = CreatePopupMenu();
            for (size_t i = 0; i < batFiles.size(); ++i) {
                UINT flags = MF_STRING | ((int)i == selectedMethodIndex ? MF_CHECKED : MF_UNCHECKED);
                InsertMenuW(hMethodMenu, (UINT)i, flags, ID_METHOD_START_RANGE + i, batFiles[i].c_str());
            }
            RECT rcButton; GetWindowRect(hBtnMethod, &rcButton);
            int trackId = TrackPopupMenu(hMethodMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, rcButton.left, rcButton.bottom, 0, hwnd, NULL);
            if (trackId >= ID_METHOD_START_RANGE && trackId < ID_METHOD_START_RANGE + (int)batFiles.size()) {
                selectedMethodIndex = trackId - ID_METHOD_START_RANGE;
                SetWindowTextW(hBtnMethod, (batFiles[selectedMethodIndex] + L"  ▼").c_str());
                if (SendMessageW(hCheckPin, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    pinnedMethod = batFiles[selectedMethodIndex];
                    SaveConfig();
                }
            }
            DestroyMenu(hMethodMenu);
        }
        else if (wmId == IDC_BTN_START) {
            if (!batFiles.empty() && selectedMethodIndex < (int)batFiles.size()) {
                RunProcess(zapretDir, batFiles[selectedMethodIndex], true);
            }
        }
        else if (wmId == IDC_BTN_TG_START) {
            if (!tgExePath.empty()) {
                // Извлекаем директорию и имя из полного пути
                size_t pos = tgExePath.find_last_of(L"\\/");
                std::wstring dir = (pos != std::wstring::npos) ? tgExePath.substr(0, pos) : L"";
                std::wstring name = (pos != std::wstring::npos) ? tgExePath.substr(pos + 1) : tgExePath;
                RunProcess(dir, name, false); // tgwssproxy обычно не требует админ-прав, запускаем стандартно
            }
        }
        else if (wmId == IDC_BTN_SET_DIR) {
            std::wstring selected = BrowseForFolder(hwnd);
            if (!selected.empty()) {
                zapretDir = selected;
                SaveConfig(); UpdateStatusTexts(); FindBatFiles();
                SendMessageW(hCheckPin, BM_SETCHECK, BST_UNCHECKED, 0);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wmId == IDC_BTN_TG_SET_FILE) {
            std::wstring selected = BrowseForFile(hwnd);
            if (!selected.empty()) {
                tgExePath = selected;
                SaveConfig(); UpdateStatusTexts();
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wmId == IDC_CHECK_PIN) {
            pinnedMethod = (SendMessageW(hCheckPin, BM_GETCHECK, 0, 0) == BST_CHECKED && !batFiles.empty()) ? batFiles[selectedMethodIndex] : L"";
            SaveConfig();
        }
        // Раздельная логика автозапусков
        else if (wmId == IDC_CHECK_AUTORUN_GUI) {
            wchar_t szPath[MAX_PATH]; GetModuleFileNameW(NULL, szPath, MAX_PATH);
            SetAutoRun(L"ZapretGUI", szPath, SendMessageW(hCheckAutorunGui, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (wmId == IDC_CHECK_AUTORUN_ZPR) {
            std::wstring fullBatPath = (zapretDir.empty() || pinnedMethod.empty()) ? L"" : zapretDir + L"\\" + pinnedMethod;
            SetAutoRun(L"ZapretGUI_PinnedBat", fullBatPath, SendMessageW(hCheckAutorunZpr, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (wmId == IDC_CHECK_AUTORUN_TG) {
            SetAutoRun(L"ZapretGUI_TgProxy", tgExePath, SendMessageW(hCheckAutorunTg, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (wmId == IDC_BTN_THEME_SELECT) {
            HMENU hThemeMenu = CreatePopupMenu();
            InsertMenuW(hThemeMenu, 0, MF_STRING | (!isDarkTheme ? MF_CHECKED : MF_UNCHECKED), ID_THEME_LIGHT, L"Светлая тема");
            InsertMenuW(hThemeMenu, 1, MF_STRING | (isDarkTheme ? MF_CHECKED : MF_UNCHECKED), ID_THEME_DARK, L"Тёмная тема");

            RECT rcButton; GetWindowRect(hBtnTheme, &rcButton);
            int trackId = TrackPopupMenu(hThemeMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, rcButton.left, rcButton.bottom, 0, hwnd, NULL);
            if (trackId == ID_THEME_LIGHT || trackId == ID_THEME_DARK) {
                isDarkTheme = (trackId == ID_THEME_DARK);
                SaveConfig();

                HBRUSH hNewBg = isDarkTheme ? hDarkBgBrush : (HBRUSH)(COLOR_BTNFACE + 1);
                SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)hNewBg);
                ApplyImmersiveDarkMode(hwnd, isDarkTheme);
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
            }
            DestroyMenu(hThemeMenu);
        }
        else if (wmId == IDC_BTN_SERVICE) {
            std::wstring checkPath = zapretDir.empty() ? L"service.bat" : zapretDir + L"\\service.bat";
            if (GetFileAttributesW(checkPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                RunProcess(zapretDir, L"service.bat", true);
            }
            else {
                MessageBoxW(hwnd, L"Файл service.bat не найден в папке Zapret!", L"Ошибка", MB_ICONERROR);
            }
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW); ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd);
        }
        else if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_SHOW, L"Открыть окно");
            InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
            InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Выход");
            POINT pt; GetCursorPos(&pt); SetForegroundWindow(hwnd);
            int trackId = TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
            if (trackId == ID_TRAY_SHOW) { ShowWindow(hwnd, SW_SHOW); ShowWindow(hwnd, SW_RESTORE); }
            else if (trackId == ID_TRAY_EXIT) DestroyWindow(hwnd);
            DestroyMenu(hMenu);
        }
        break;
    }
    case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); break;
    case WM_DESTROY:
        if (hDarkBgBrush) DeleteObject(hDarkBgBrush);
        Shell_NotifyIconW(NIM_DELETE, &nid);
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        PostQuitMessage(0);
        break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hMutex = CreateMutexW(NULL, TRUE, L"ZapretGUI_Mutex_Unique");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hWndExisting = FindWindowW(L"ZapretGUIClass", L"Управление Обходом & Прокси");
        if (hWndExisting) {
            ShowWindow(hWndExisting, SW_SHOW); ShowWindow(hWndExisting, SW_RESTORE); SetForegroundWindow(hWndExisting);
        }
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    LoadConfig();

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ZapretGUIClass";
    wc.hbrBackground = isDarkTheme ? CreateSolidBrush(RGB(30, 30, 30)) : (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassW(&wc)) return 0;

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 445;
    int windowHeight = 480;
    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = (screenHeight - windowHeight) / 2;

    HWND hwnd = CreateWindowExW(0, L"ZapretGUIClass", L"Управление Обходом & Прокси",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        xPos, yPos, windowWidth, windowHeight, NULL, NULL, hInstance, NULL);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
