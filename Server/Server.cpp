// Лабораторна робота 5. Варіант 6. Обмін через mailslot.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>

const wchar_t MAILSLOT_NAME[] = L"\\\\.\\mailslot\\Lab5_Variant6";
const DWORD MAX_MESSAGE_SIZE = 16384;

const int ID_START_SERVER = 101;
const int ID_STOP_SERVER = 102;
const int ID_CLEAR_LOG = 103;
const UINT_PTR READ_TIMER = 1;
const UINT READ_INTERVAL = 1000;

HANDLE hMailslot = INVALID_HANDLE_VALUE;
HWND hLogEdit = nullptr;

std::wstring GetErrorMessage(DWORD errorCode)
{
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

void AddToLog(HWND edit, const std::wstring &text)
{
    if (GetWindowTextLengthW(edit) > 150000)
    {
        SetWindowTextW(edit, L"");
    }
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), -1);
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void StopServer(HWND hWnd)
{
    KillTimer(hWnd, READ_TIMER);
    if (hMailslot != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hMailslot);
        hMailslot = INVALID_HANDLE_VALUE;
        AddToLog(hLogEdit, L"Скриньку закрито. Сервер зупинено.\r\n");
    }
}

void StartServer(HWND hWnd)
{
    if (hMailslot != INVALID_HANDLE_VALUE)
    {
        AddToLog(hLogEdit, L"Сервер уже працює.\r\n");
        return;
    }

    // Тайм-аут 0: читання порожньої скриньки не має блокувати вікно.
    hMailslot = CreateMailslotW(MAILSLOT_NAME, MAX_MESSAGE_SIZE, 0, nullptr);
    if (hMailslot == INVALID_HANDLE_VALUE)
    {
        AddToLog(hLogEdit, GetErrorMessage(GetLastError()));
        return;
    }

    if (!SetTimer(hWnd, READ_TIMER, READ_INTERVAL, nullptr))
    {
        DWORD errorCode = GetLastError();
        StopServer(hWnd);
        AddToLog(hLogEdit, GetErrorMessage(errorCode));
        return;
    }

    AddToLog(hLogEdit, std::wstring(L"Сервер запущено: ") + MAILSLOT_NAME + L"\r\n");
}

void ReadMessages()
{
    if (hMailslot == INVALID_HANDLE_VALUE)
    {
        return;
    }

    // Обмежуємо одну порцію, щоб вікно реагувало на кнопки під час потоку даних.
    for (int i = 0; i < 100; i++)
    {
        DWORD messageSize = 0;
        DWORD messageCount = 0;
        if (!GetMailslotInfo(hMailslot, nullptr, &messageSize, &messageCount, nullptr))
        {
            AddToLog(hLogEdit, GetErrorMessage(GetLastError()));
            return;
        }

        if (messageCount == 0 || messageSize == MAILSLOT_NO_MESSAGE)
        {
            return;
        }

        // Буфер має вмістити все повідомлення, а не лише фіксовані 512 байтів.
        std::vector<wchar_t> buffer(messageSize / sizeof(wchar_t) + 1);
        DWORD bytesRead = 0;
        if (!ReadFile(hMailslot, buffer.data(), messageSize, &bytesRead, nullptr))
        {
            AddToLog(hLogEdit, GetErrorMessage(GetLastError()));
            return;
        }

        if (bytesRead % sizeof(wchar_t) != 0 || bytesRead > MAX_MESSAGE_SIZE)
        {
            AddToLog(hLogEdit, L"Отримано повідомлення неправильного формату.\r\n");
            continue;
        }

        std::wstring text(buffer.data(), bytesRead / sizeof(wchar_t));
        AddToLog(hLogEdit, L"\r\n" + text + L"\r\n");
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateButton(hWnd, L"Створити скриньку", ID_START_SERVER, 12, 170);
        CreateButton(hWnd, L"Закрити скриньку", ID_STOP_SERVER, 192, 170);
        CreateButton(hWnd, L"Очистити журнал", ID_CLEAR_LOG, 372, 170);
        hLogEdit = CreateTextBox(hWnd, 201, true);
        AddToLog(hLogEdit, L"Лабораторна робота 5. Сервер. Варіант 6.\r\n"
                           L"Створіть скриньку. Інтервал читання: 1000 мс.\r\n");
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_START_SERVER:
            StartServer(hWnd);
            return 0;
        case ID_STOP_SERVER:
            StopServer(hWnd);
            return 0;
        case ID_CLEAR_LOG:
            SetWindowTextW(hLogEdit, L"");
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == READ_TIMER)
        {
            ReadMessages();
        }
        return 0;

    case WM_SIZE:
    {
        int width = std::max(100, (int)LOWORD(lParam) - 24);
        int height = std::max(60, (int)HIWORD(lParam) - 72);
        MoveWindow(hLogEdit, 12, 60, width, height, TRUE);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *windowSizes = (MINMAXINFO *)lParam;
        windowSizes->ptMinTrackSize = {600, 300};
        return 0;
    }

    case WM_DESTROY:
        StopServer(hWnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    SetProcessDPIAware();

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = L"Lab5Server";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&windowClass))
    {
        MessageBoxW(nullptr, L"Не вдалося зареєструвати клас вікна.", L"Помилка", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowW(windowClass.lpszClassName, L"Lab5 - Server - Варіант 6", WS_OVERLAPPEDWINDOW,
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
