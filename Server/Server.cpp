#include "../PipeCommon.h"
#include <algorithm>
#include <thread>
#include <new>

const int ID_START = 101;
const int ID_STOP = 102;
const int ID_CLEAR = 103;
const UINT WM_LOG = WM_APP + 1;

HWND hMainWindow = nullptr;
HWND hLogEdit = nullptr;
HANDLE hStopEvent = nullptr;
HANDLE pipes[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
std::thread workers[2];

void AddToLog(const std::wstring &text)
{
    if (GetWindowTextLengthW(hLogEdit) > 150000)
    {
        SetWindowTextW(hLogEdit, L"");
    }
    SendMessageW(hLogEdit, EM_SETSEL, (WPARAM)-1, -1);
    SendMessageW(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

// Робочий потік передає текст головному потоку, а не змінює EDIT напряму.
bool PostLog(const std::wstring &text)
{
    std::wstring *message = new (std::nothrow) std::wstring(text);
    if (!message)
    {
        return false;
    }
    if (!PostMessageW(hMainWindow, WM_LOG, 0, (LPARAM)message))
    {
        delete message;
        return false;
    }
    return true;
}

bool WaitForClient(HANDLE pipe, DWORD &error)
{
    OVERLAPPED operation = {};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent)
    {
        error = GetLastError();
        return false;
    }
    BOOL connected = ConnectNamedPipe(pipe, &operation);
    DWORD startError = connected ? ERROR_SUCCESS : GetLastError();
    bool success;
    if (!connected && startError == ERROR_PIPE_CONNECTED)
    {
        // Клієнт підключився між CreateNamedPipe і ConnectNamedPipe — це успіх.
        error = ERROR_SUCCESS;
        success = true;
    }
    else
    {
        DWORD bytes = 0;
        success = WaitForIo(pipe, operation, connected, startError, hStopEvent, INFINITE, bytes, error);
    }
    CloseHandle(operation.hEvent);
    return success;
}

bool IsValidPacket(const MessagePacket &packet, DWORD bytes)
{
    return bytes == sizeof(MessagePacket) && (packet.clientNumber == 1 || packet.clientNumber == 2) &&
           packet.textLength > 0 && packet.textLength < TEXT_CAPACITY &&
           packet.text[packet.textLength] == L'\0';
}

void ServeClient(int index)
{
    HANDLE pipe = pipes[index];
    while (WaitForSingleObject(hStopEvent, 0) != WAIT_OBJECT_0)
    {
        DWORD error = 0;
        if (!WaitForClient(pipe, error))
        {
            if (error == ERROR_OPERATION_ABORTED)
            {
                break;
            }
            if (error != ERROR_NO_DATA && error != ERROR_BROKEN_PIPE)
            {
                PostLog(ErrorMessage(error));
                break;
            }
            DisconnectNamedPipe(pipe);
            continue;
        }

        MessagePacket packet = {};
        DWORD bytes = 0;
        if (ReadFromPipe(pipe, &packet, sizeof(packet), bytes, error, hStopEvent, 5000))
        {
            DWORD reply = REPLY_INVALID;
            if (IsValidPacket(packet, bytes))
            {
                std::wstring text(packet.text, packet.textLength);
                if (PostLog(L"\r\nКлієнт " + std::to_wstring(packet.clientNumber) + L":\r\n" + text +
                            L"\r\n"))
                {
                    reply = REPLY_OK;
                }
            }
            else
            {
                PostLog(L"Відхилено пакет неправильного формату.\r\n");
            }

            if (WriteToPipe(pipe, &reply, sizeof(reply), error, hStopEvent))
            {
                // Не від'єднуємо канал раніше, ніж клієнт прочитає відповідь.
                DWORD receipt = 0;
                DWORD received = 0;
                ReadFromPipe(pipe, &receipt, sizeof(receipt), received, error, hStopEvent);
            }
        }
        else if (error != ERROR_OPERATION_ABORTED && error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA)
        {
            PostLog(ErrorMessage(error));
        }

        DisconnectNamedPipe(pipe);
    }
}

void StopServer()
{
    if (!hStopEvent)
    {
        return;
    }
    SetEvent(hStopEvent);
    for (int i = 0; i < 2; i++)
    {
        if (workers[i].joinable())
        {
            workers[i].join();
        }
        if (pipes[i] != INVALID_HANDLE_VALUE)
        {
            CloseHandle(pipes[i]);
        }
        pipes[i] = INVALID_HANDLE_VALUE;
    }
    CloseHandle(hStopEvent);
    hStopEvent = nullptr;

    // Усі потоки завершені: дочитуємо вже прийняті записи перед повідомленням про зупинку.
    MSG message = {};
    while (PeekMessageW(&message, hMainWindow, WM_LOG, WM_LOG, PM_REMOVE))
    {
        std::wstring *text = (std::wstring *)message.lParam;
        AddToLog(*text);
        delete text;
    }
    AddToLog(L"Сервер зупинено. Канали й потоки закрито.\r\n");
}

bool StartServer()
{
    if (hStopEvent)
    {
        AddToLog(L"Сервер уже працює.\r\n");
        return false;
    }
    hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hStopEvent)
    {
        AddToLog(ErrorMessage(GetLastError()));
        return false;
    }

    for (int i = 0; i < 2; i++)
    {
        DWORD access = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (i == 0)
        {
            access |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        }
        pipes[i] = CreateNamedPipeW(PIPE_NAME, access,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                                        PIPE_REJECT_REMOTE_CLIENTS,
                                    2, sizeof(MessagePacket), sizeof(MessagePacket), 2000, nullptr);
        if (pipes[i] == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            StopServer();
            AddToLog(ErrorMessage(error));
            return false;
        }
    }

    try
    {
        workers[0] = std::thread(ServeClient, 0);
        workers[1] = std::thread(ServeClient, 1);
    }
    catch (const std::exception &)
    {
        StopServer();
        AddToLog(L"Не вдалося створити робочі потоки.\r\n");
        return false;
    }
    AddToLog(L"Сервер запущено. Доступні два екземпляри каналу.\r\n");
    return true;
}

void CreateButton(HWND hWnd, const wchar_t *title, int id, int x)
{
    HWND button = CreateWindowW(L"BUTTON", title, WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, 12, 175, 32, hWnd,
                                (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(button, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        hMainWindow = hWnd;
        CreateButton(hWnd, L"Запустити сервер", ID_START, 12);
        CreateButton(hWnd, L"Зупинити сервер", ID_STOP, 197);
        CreateButton(hWnd, L"Очистити журнал", ID_CLEAR, 382);
        hLogEdit =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            12, 60, 700, 400, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hLogEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SendMessageW(hLogEdit, EM_SETLIMITTEXT, 200000, 0);
        AddToLog(L"Лабораторна робота 7. Варіант 6. Іменовані канали.\r\n");
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_START:
            StartServer();
            return 0;
        case ID_STOP:
            StopServer();
            return 0;
        case ID_CLEAR:
            SetWindowTextW(hLogEdit, L"");
            return 0;
        }
        break;
    case WM_LOG:
    {
        std::wstring *text = (std::wstring *)lParam;
        AddToLog(*text);
        delete text;
        return 0;
    }
    case WM_SIZE:
        MoveWindow(hLogEdit, 12, 60, std::max(100, (int)LOWORD(lParam) - 24),
                   std::max(60, (int)HIWORD(lParam) - 72), TRUE);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize = {620, 350};
        return 0;
    case WM_DESTROY:
        StopServer();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine,
                    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    SetProcessDPIAware();
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = L"Lab7Server";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&windowClass))
    {
        return 1;
    }
    HWND hWnd = CreateWindowW(windowClass.lpszClassName, L"Lab7 - Server - Варіант 6", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 850, 580, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd)
    {
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
    return result == -1 ? 1 : (int)message.wParam;
}
