// Лабораторна робота 5. Варіант 6. Обмін через mailslot.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>

const wchar_t MAILSLOT_NAME[] = L"\\\\.\\mailslot\\Lab5_Variant6";
const DWORD MAX_MESSAGE_SIZE = 16384;

const int ID_UPDATE_DATA = 101;
const int ID_SEND_DATA = 102;

HWND hDataEdit = nullptr;
HWND hStatusEdit = nullptr;

std::wstring GetErrorMessage(DWORD errorCode)
{
    if (errorCode == ERROR_FILE_NOT_FOUND)
    {
        return L"Сервер не запущено. Спочатку запустіть сервер.";
    }

    wchar_t description[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, errorCode, 0,
                   description, 512, nullptr);

    return L"Помилка " + std::to_wstring(errorCode) + L": " + description;
}

HWND CreateTextBox(HWND parent, int id, bool readOnly)
{
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                    (readOnly ? ES_READONLY : 0),
                                12, 60, 650, 340, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(edit, EM_SETLIMITTEXT, 200000, 0);
    return edit;
}

void CreateButton(HWND parent, const wchar_t *title, int id, int x, int width)
{
    HWND hButton =
        CreateWindowW(L"BUTTON", title, WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, 12, width, 32, parent,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hButton, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

std::wstring CollectSystemData()
{
    std::wostringstream data;
    data << L"Клієнт 1 - варіант 6\r\n";
    SYSTEM_POWER_STATUS power{};
    if (!GetSystemPowerStatus(&power))
    {
        data << GetErrorMessage(GetLastError());
    }
    else
    {
        if (power.BatteryFlag == 255)
        {
            data << L"Стан батареї: невідомий\r\n";
        }
        else if (power.BatteryFlag & 128)
        {
            data << L"Батарея відсутня\r\n";
        }
        else
        {
            data << L"Батарея: " << ((power.BatteryFlag & 8) ? L"заряджається" : L"не заряджається")
                 << L"\r\n";
            if (power.BatteryLifePercent != 255)
            {
                data << L"Заряд: " << static_cast<int>(power.BatteryLifePercent) << L" %\r\n";
            }
            else
            {
                data << L"Рівень заряду: невідомий\r\n";
            }
        }
        data << L"Живлення від мережі: ";
        if (power.ACLineStatus == 1)
        {
            data << L"так";
        }
        else if (power.ACLineStatus == 0)
        {
            data << L"ні";
        }
        else
        {
            data << L"невідомо";
        }
        data << L"\r\n";
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) &&
        (mode.dmFields & DM_DISPLAYFREQUENCY) && mode.dmDisplayFrequency > 1)
    {
        data << L"Частота основного екрана: " << mode.dmDisplayFrequency << L" Гц\r\n";
    }
    else
    {
        data << L"Частота екрана: не визначена драйвером\r\n";
    }
    HDC hDC = GetDC(nullptr);
    if (hDC)
    {
        data << L"Глибина кольору GDI: " << GetDeviceCaps(hDC, BITSPIXEL) * GetDeviceCaps(hDC, PLANES)
             << L" біт/піксель\r\n";
        ReleaseDC(nullptr, hDC);
    }
    else
    {
        data << L"Не вдалося отримати контекст дисплея\r\n";
    }
    return data.str();
}

void UpdateData(HWND hWnd)
{
    (void)hWnd;
    std::wstring data = CollectSystemData();
    SetWindowTextW(hDataEdit, data.c_str());
}

// Відкриваємо скриньку заново перед кожним надсиланням.
// Тому після перезапуску сервера клієнт не потрібно запускати повторно.
void SendData()
{
    int textLength = GetWindowTextLengthW(hDataEdit);
    if (textLength == 0)
    {
        SetWindowTextW(hStatusEdit, L"Введіть повідомлення або оновіть системні дані.");
        return;
    }

    std::wstring text(textLength + 1, L'\0');
    GetWindowTextW(hDataEdit, text.data(), textLength + 1);
    text.resize(textLength);

    DWORD messageSize = (DWORD)(text.size() * sizeof(wchar_t));
    if (messageSize > MAX_MESSAGE_SIZE)
    {
        SetWindowTextW(hStatusEdit, L"Повідомлення перевищує дозволений розмір.");
        return;
    }

    HANDLE hMailslot = CreateFileW(MAILSLOT_NAME, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hMailslot == INVALID_HANDLE_VALUE)
    {
        SetWindowTextW(hStatusEdit, GetErrorMessage(GetLastError()).c_str());
        return;
    }

    // Передаємо UTF-16 без завершального нуля. Одне надсилання — одне повідомлення.
    DWORD bytesWritten = 0;
    BOOL success = WriteFile(hMailslot, text.data(), messageSize, &bytesWritten, nullptr);
    DWORD errorCode = ERROR_SUCCESS;
    if (!success)
    {
        errorCode = GetLastError();
    }
    else if (bytesWritten != messageSize)
    {
        errorCode = ERROR_WRITE_FAULT;
    }

    CloseHandle(hMailslot);

    if (errorCode != ERROR_SUCCESS)
    {
        SetWindowTextW(hStatusEdit, GetErrorMessage(errorCode).c_str());
    }
    else
    {
        SetWindowTextW(hStatusEdit, L"Повідомлення записано у скриньку. Перевірте журнал сервера.");
    }
}

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateButton(window, L"Оновити дані", ID_UPDATE_DATA, 12, 170);
        CreateButton(window, L"Надіслати дані", ID_SEND_DATA, 192, 170);
        hDataEdit = CreateTextBox(window, 201, false);
        SendMessageW(hDataEdit, EM_SETLIMITTEXT, MAX_MESSAGE_SIZE / sizeof(wchar_t) - 100, 0);
        hStatusEdit = CreateTextBox(window, 202, true);
        UpdateData(window);
        SetWindowTextW(hStatusEdit, L"Спочатку створіть скриньку на сервері. Текст можна редагувати.");
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_UPDATE_DATA:
            UpdateData(window);
            return 0;
        case ID_SEND_DATA:
            SendData();
            return 0;
        }
        break;
    case WM_SIZE:
    {
        int width = std::max(100, static_cast<int>(LOWORD(lParam)) - 24);
        int height = std::max(80, static_cast<int>(HIWORD(lParam)) - 160);
        MoveWindow(hDataEdit, 12, 60, width, height, TRUE);
        MoveWindow(hStatusEdit, 12, 70 + height, width, 65, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO *>(lParam)->ptMinTrackSize = {600, 350};
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    SetProcessDPIAware();

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = L"Lab5Client1";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&windowClass))
    {
        MessageBoxW(nullptr, L"Не вдалося зареєструвати клас вікна.", L"Помилка", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowW(windowClass.lpszClassName, L"Lab5 - Client1 - Варіант 6", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 760, 510, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        MessageBoxW(nullptr, L"Не вдалося створити вікно.", L"Помилка", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);

    MSG message = {};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (!IsDialogMessageW(hWnd, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (result == -1)
    {
        return 1;
    }

    return (int)message.wParam;
}
