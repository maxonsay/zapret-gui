#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <fstream>
#include <shlobj.h>
#include <algorithm>

// Подключаем библиотеки и управляем подсистемой линкера прямо из кода
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

// Константы идентификаторов элементов управления
#define IDC_BTN_START           1001
#define IDC_BTN_SERVICE         1002
#define IDC_BTN_SET_DIR         1003
#define IDC_BTN_METHOD_SELECT   1004
#define IDC_BTN_THEME_SELECT    1005
#define IDC_CHECK_PIN           1006
#define IDC_CHECK_AUTORUN        1007

#define ID_METHOD_START_RANGE   2000
#define ID_THEME_LIGHT          3001
#define ID_THEME_DARK           3002

#define ID_TRAY_SHOW            4001
#define ID_TRAY_EXIT            4002
#define WM_TRAYICON             (WM_USER + 1)

// Глобальные переменные окон
HWND hTitle, hStaticDir, hBtnSetDir, hBtnMethod, hCheckPin, hCheckAutorun, hBtnStart, hBtnService, hBtnTheme;
HBRUSH hDarkBgBrush = NULL;
NOTIFYICONDATAW nid = { 0 };
HANDLE hMutex = NULL;

// Глобальное состояние приложения
bool isDarkTheme = true;
std::wstring zapretDir = L"";
std::wstring pinnedMethod = L"";
std::vector<std::wstring> batFiles;
int selectedMethodIndex = 0;

// Состояния анимации кастомных кнопок
bool hBtnStartH = false, hBtnStartP = false;
bool hBtnServiceH = false, hBtnServiceP = false;
bool hBtnSetDirH = false, hBtnSetDirP = false;
bool hBtnMethodH = false, hBtnMethodP = false;
bool hBtnThemeH = false, hBtnThemeP = false;

// Прототипы функций логики приложения
void SaveConfig();
void LoadConfig();
void FindBatFiles();
void UpdateDirStatusText();
void RunProcess(const std::wstring& name, bool isService);
bool IsAutoRunEnabled();
void SetAutoRun(bool enable);
void ApplyImmersiveDarkMode(HWND hwnd, bool dark);
void AddTrayIcon(HWND hwnd);
std::wstring BrowseForFolder(HWND hwnd);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Функция получения пути к файлу конфигурации в AppData
std::wstring GetConfigPath() {
    wchar_t szPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath))) {
        std::wstring configDir = std::wstring(szPath) + L"\\ZapretGUI";
        CreateDirectoryW(configDir.c_str(), NULL);
        return configDir + L"\\config.txt";
    }
    return L"config.txt";
}

// Сохранение конфигурации
void SaveConfig() {
    std::wofstream file(GetConfigPath());
    if (file.is_open()) {
        file << (isDarkTheme ? 1 : 0) << L"\n";
        file << zapretDir << L"\n";
        file << pinnedMethod << L"\n";
        file.close();
    }
}

// Загрузка конфигурации
void LoadConfig() {
    std::wifstream file(GetConfigPath());
    if (file.is_open()) {
        std::wstring themeStr, dirStr, pinStr;
        if (std::getline(file, themeStr)) isDarkTheme = (themeStr == L"1");
        if (std::getline(file, dirStr)) zapretDir = dirStr;
        if (std::getline(file, pinStr)) pinnedMethod = pinStr;
        file.close();
    }
}

// Поиск .bat файлов в выбранной директории
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
            if (filename != L"service.bat") {
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

// Обновление текста выбранной папки
void UpdateDirStatusText() {
    if (hStaticDir) {
        if (zapretDir.empty()) {
            SetWindowTextW(hStaticDir, L"Папка: не указана");
        }
        else {
            std::wstring shortDir = zapretDir;
            if (shortDir.length() > 35) {
                shortDir = shortDir.substr(0, 15) + L"..." + shortDir.substr(shortDir.length() - 17);
            }
            SetWindowTextW(hStaticDir, (L"Папка: " + shortDir).c_str());
        }
    }
}

// Запуск выбранного скрипта с правами администратора
void RunProcess(const std::wstring& name, bool isService) {
    std::wstring runPath = zapretDir.empty() ? name : zapretDir + L"\\" + name;
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = runPath.c_str();
    sei.lpDirectory = zapretDir.empty() ? NULL : zapretDir.c_str();
    sei.nShow = SW_SHOW;
    ShellExecuteExW(&sei);
}

// Проверка автозагрузки в реестре
bool IsAutoRunEnabled() {
    HKEY hKey;
    bool enabled = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, L"ZapretGUI", NULL, NULL, (LPBYTE)path, &size) == ERROR_SUCCESS) {
            enabled = true;
        }
        RegCloseKey(hKey);
    }
    return enabled;
}

// Установка/удаление автозагрузки
void SetAutoRun(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t szPath[MAX_PATH];
            GetModuleFileNameW(NULL, szPath, MAX_PATH);
            RegSetValueExW(hKey, L"ZapretGUI", 0, REG_SZ, (BYTE*)szPath, (lstrlenW(szPath) + 1) * sizeof(wchar_t));
        }
        else {
            RegDeleteValueW(hKey, L"ZapretGUI");
        }
        RegCloseKey(hKey);
    }
}

// Применение темной темы к заголовку окна (DWM)
void ApplyImmersiveDarkMode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT(WINAPI* pfnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        pfnDwmSetWindowAttribute pDwmSetWindowAttribute = (pfnDwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (pDwmSetWindowAttribute) {
            pDwmSetWindowAttribute(hwnd, 20, &value, sizeof(value));
        }
        FreeLibrary(hDwm);
    }
}

// Добавление иконки в системный трей
void AddTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    lstrcpyW(nid.szTip, L"Управление Zapret");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

// Диалог выбора папки
std::wstring BrowseForFolder(HWND hwnd) {
    std::wstring result = L"";
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwnd;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpszTitle = L"Выберите папку с Zapret:";
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != NULL) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            result = path;
        }
        CoTaskMemFree(pidl);
    }
    return result;
}

// Сабкласс для анимации кастомных (OWNERDRAW) кнопок
LRESULT CALLBACK NewButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    bool* pHovered = nullptr;
    bool* pPressed = nullptr;
    LONG_PTR id = GetWindowLongPtrW(hwnd, GWLP_ID);

    if (id == IDC_BTN_START) { pHovered = &hBtnStartH; pPressed = &hBtnStartP; }
    else if (id == IDC_BTN_SERVICE) { pHovered = &hBtnServiceH; pPressed = &hBtnServiceP; }
    else if (id == IDC_BTN_SET_DIR) { pHovered = &hBtnSetDirH; pPressed = &hBtnSetDirP; }
    else if (id == IDC_BTN_METHOD_SELECT) { pHovered = &hBtnMethodH; pPressed = &hBtnMethodP; }
    else if (id == IDC_BTN_THEME_SELECT) { pHovered = &hBtnThemeH; pPressed = &hBtnThemeP; }

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
        case WM_MOUSELEAVE:
            *pHovered = false;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case WM_LBUTTONDOWN:
            *pPressed = true;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case WM_LBUTTONUP:
            *pPressed = false;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Сабкласс специально для чекбоксов (исправляет клики и цвет текста)
LRESULT CALLBACK CheckboxSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (msg) {
        // Ловим внутреннюю отрисовку текста чекбокса WinAPI
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        if (isDarkTheme) {
            SetTextColor(hdc, RGB(220, 220, 220)); // Яркий белый/серый текст для темной темы
            return (LRESULT)hDarkBgBrush;
        }
        else {
            SetTextColor(hdc, RGB(0, 0, 0)); // Черный текст для светлой темы
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
    }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Главная оконная процедура
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hFontSmall = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        hDarkBgBrush = CreateSolidBrush(RGB(30, 30, 30));

        hTitle = CreateWindowW(L"STATIC", L"Управление Zapret", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 410, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial"), TRUE);

        hStaticDir = CreateWindowW(L"STATIC", L"Папка: не указана", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 45, 290, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(hStaticDir, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        hBtnSetDir = CreateWindowW(L"BUTTON", L"Обзор...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 320, 42, 90, 22, hwnd, (HMENU)IDC_BTN_SET_DIR, NULL, NULL);
        UpdateDirStatusText();

        hBtnMethod = CreateWindowW(L"BUTTON", L"Выбрать метод...  ▼", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 75, 390, 28, hwnd, (HMENU)IDC_BTN_METHOD_SELECT, NULL, NULL);
        FindBatFiles();

        // Создаем стандартные чекбоксы
        hCheckPin = CreateWindowW(L"BUTTON", L"Закрепить этот метод по умолчанию", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 115, 390, 20, hwnd, (HMENU)IDC_CHECK_PIN, NULL, NULL);
        hCheckAutorun = CreateWindowW(L"BUTTON", L"Запускать вместе с Windows", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 140, 390, 20, hwnd, (HMENU)IDC_CHECK_AUTORUN, NULL, NULL);

        SendMessageW(hCheckPin, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hCheckAutorun, WM_SETFONT, (WPARAM)hFont, TRUE);

        if (IsAutoRunEnabled()) SendMessageW(hCheckAutorun, BM_SETCHECK, BST_CHECKED, 0);
        if (!pinnedMethod.empty() && !batFiles.empty()) {
            SendMessageW(hCheckPin, BM_SETCHECK, BST_CHECKED, 0);
        }

        hBtnStart = CreateWindowW(L"BUTTON", L"🚀 ЗАПУСТИТЬ ВЫБРАННЫЙ МЕТОД", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 175, 390, 40, hwnd, (HMENU)IDC_BTN_START, NULL, NULL);
        hBtnService = CreateWindowW(L"BUTTON", L"🛠️ Настройка службы", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 225, 390, 40, hwnd, (HMENU)IDC_BTN_SERVICE, NULL, NULL);
        hBtnTheme = CreateWindowW(L"BUTTON", L"🎨 Смена темы...  ▼", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 275, 140, 25, hwnd, (HMENU)IDC_BTN_THEME_SELECT, NULL, NULL);

        // Назначаем сабклассы кнопкам
        SetWindowSubclass(hBtnStart, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnService, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnSetDir, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnMethod, NewButtonSubclassProc, 0, 0);
        SetWindowSubclass(hBtnTheme, NewButtonSubclassProc, 0, 0);

        // Назначаем сабклассы чекбоксам для изоляции их контекста отображения
        SetWindowSubclass(hCheckPin, CheckboxSubclassProc, 1, 0);
        SetWindowSubclass(hCheckAutorun, CheckboxSubclassProc, 2, 0);

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
            else { return DefWindowProcW(hwnd, msg, wParam, lParam); } // Пропускаем чекбоксы, их рисует система

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

            HFONT hBtnFont = CreateFontW((pDIS->CtlID == IDC_BTN_SET_DIR || pDIS->CtlID == IDC_BTN_THEME_SELECT) ? 14 : 16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
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
        else if (wmId == IDC_BTN_START) {
            if (!batFiles.empty() && selectedMethodIndex < (int)batFiles.size()) {
                RunProcess(batFiles[selectedMethodIndex], false);
            }
        }
        else if (wmId == IDC_BTN_SERVICE) {
            std::wstring checkPath = zapretDir.empty() ? L"service.bat" : zapretDir + L"\\service.bat";
            if (GetFileAttributesW(checkPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                RunProcess(L"service.bat", true);
            }
            else {
                MessageBoxW(hwnd, L"Файл service.bat не найден!", L"Ошибка", MB_ICONERROR);
            }
        }
        else if (wmId == IDC_BTN_SET_DIR) {
            std::wstring selected = BrowseForFolder(hwnd);
            if (!selected.empty()) {
                zapretDir = selected;
                SaveConfig();
                UpdateDirStatusText();
                FindBatFiles();
                SendMessageW(hCheckPin, BM_SETCHECK, BST_UNCHECKED, 0);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wmId == IDC_CHECK_PIN) {
            if (SendMessageW(hCheckPin, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                if (!batFiles.empty()) pinnedMethod = batFiles[selectedMethodIndex];
            }
            else {
                pinnedMethod = L"";
            }
            SaveConfig();
        }
        else if (wmId == IDC_CHECK_AUTORUN) {
            bool isChecked = (SendMessageW(hCheckAutorun, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SetAutoRun(isChecked);
            SaveConfig();
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
            else if (trackId == ID_TRAY_EXIT) { DestroyWindow(hwnd); }
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

// Точка входа приложения
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hMutex = CreateMutexW(NULL, TRUE, L"ZapretGUI_Mutex_Unique");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hWndExisting = FindWindowW(L"ZapretGUIClass", L"Управление Zapret");
        if (hWndExisting) {
            ShowWindow(hWndExisting, SW_SHOW);
            ShowWindow(hWndExisting, SW_RESTORE);
            SetForegroundWindow(hWndExisting);
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
    int windowHeight = 355;
    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = (screenHeight - windowHeight) / 2;

    HWND hwnd = CreateWindowExW(0, L"ZapretGUIClass", L"Управление Zapret",
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
