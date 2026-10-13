// Lab4_Server.cpp : Defines the entry point for the application.
//

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>

#include <array>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

#define MAX_LOADSTRING 100
#define SERVER_PORT 5000

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

constexpr UINT WM_SOCKET_EVENT = WM_APP + 1;
constexpr int ID_SERVER_START = 101;
constexpr int ID_SERVER_STOP = 102;
constexpr int IDM_EXIT = 40001;
constexpr int IDM_ABOUT = 40002;

HWND hwndEdit = nullptr;
HWND hwndStatus = nullptr;
HWND hwndStart = nullptr;
HWND hwndStop = nullptr;

SOCKET serverSocket = INVALID_SOCKET;
std::array<SOCKET, 2> clientSockets = { INVALID_SOCKET, INVALID_SOCKET };
std::array<std::string, 2> clientBuffers;
std::wstring logText;
bool winsockReady = false;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void                ServerStart(HWND hWnd);
void                ServerStop();

void AppendLog(const std::wstring& line)
{
    logText += line + L"\r\n";
    if (logText.size() > 16000)
        logText.erase(0, 4000);

    if (hwndEdit)
    {
        SetWindowTextW(hwndEdit, logText.c_str());
        SendMessageW(hwndEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(hwndEdit, EM_SCROLLCARET, 0, 0);
    }
}

std::wstring FromUtf8(const std::string& value)
{
    if (value.empty())
        return L"";

    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), (int)value.size(), nullptr, 0);
    if (length == 0)
        return L"[помилка кодування UTF-8]";

    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), (int)value.size(), &result[0], length);
    return result;
}

void UpdateStatus()
{
    bool running = serverSocket != INVALID_SOCKET;
    if (hwndStatus)
        SetWindowTextW(hwndStatus, running ? L"Статус: сервер працює, порт 5000"
            : L"Статус: сервер зупинено");
    if (hwndStart)
        EnableWindow(hwndStart, !running);
    if (hwndStop)
        EnableWindow(hwndStop, running);
}

void DisconnectClient(int index)
{
    if (clientSockets[index] == INVALID_SOCKET)
        return;

    closesocket(clientSockets[index]);
    clientSockets[index] = INVALID_SOCKET;
    clientBuffers[index].clear();
    AppendLog(L"Клієнт у слоті " + std::to_wstring(index + 1) + L" відключився.");
}

void ServerStop()
{
    for (int i = 0; i < 2; ++i)
        DisconnectClient(i);

    if (serverSocket != INVALID_SOCKET)
    {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
        AppendLog(L"Сервер зупинено.");
    }
    UpdateStatus();
}

void ServerStart(HWND hWnd)
{
    if (!winsockReady || serverSocket != INVALID_SOCKET)
        return;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        AppendLog(L"Помилка socket(): " + std::to_wstring(WSAGetLastError()));
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(s, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
    {
        AppendLog(L"Помилка bind(): " + std::to_wstring(WSAGetLastError()));
        closesocket(s);
        return;
    }

    if (listen(s, 2) == SOCKET_ERROR)
    {
        AppendLog(L"Помилка listen(): " + std::to_wstring(WSAGetLastError()));
        closesocket(s);
        return;
    }

    if (WSAAsyncSelect(s, hWnd, WM_SOCKET_EVENT, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR)
    {
        AppendLog(L"Помилка WSAAsyncSelect(): " + std::to_wstring(WSAGetLastError()));
        closesocket(s);
        return;
    }

    serverSocket = s;
    AppendLog(L"Сервер запущено на 127.0.0.1:5000. Очікування двох клієнтів...");
    UpdateStatus();
}

void AcceptClient(HWND hWnd)
{
    SOCKET s = accept(serverSocket, nullptr, nullptr);
    if (s == INVALID_SOCKET)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            AppendLog(L"Помилка accept(): " + std::to_wstring(WSAGetLastError()));
        return;
    }

    int index = -1;
    for (int i = 0; i < 2; ++i)
    {
        if (clientSockets[i] == INVALID_SOCKET)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        AppendLog(L"Додаткове підключення відхилено: максимум 2 клієнти.");
        closesocket(s);
        return;
    }

    if (WSAAsyncSelect(s, hWnd, WM_SOCKET_EVENT, FD_READ | FD_CLOSE) == SOCKET_ERROR)
    {
        AppendLog(L"Не вдалося зареєструвати події клієнта.");
        closesocket(s);
        return;
    }

    clientSockets[index] = s;
    clientBuffers[index].clear();
    AppendLog(L"Підключився клієнт (слот " + std::to_wstring(index + 1) + L").");
}

void ReceiveFromClient(int index)
{
    char data[1024];
    SOCKET s = clientSockets[index];

    while (true)
    {
        int count = recv(s, data, sizeof(data), 0);
        if (count > 0)
        {
            clientBuffers[index].append(data, count);

            size_t pos;
            while ((pos = clientBuffers[index].find('\n')) != std::string::npos)
            {
                std::string message = clientBuffers[index].substr(0, pos);
                clientBuffers[index].erase(0, pos + 1);
                if (!message.empty() && message.back() == '\r')
                    message.pop_back();
                AppendLog(L"Отримано: " + FromUtf8(message));
            }

            if (clientBuffers[index].size() > 8192)
            {
                AppendLog(L"Завелике повідомлення, клієнт відключений.");
                DisconnectClient(index);
                return;
            }
        }
        else if (count == 0)
        {
            DisconnectClient(index);
            return;
        }
        else
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                DisconnectClient(index);
            return;
        }
    }
}

void OnSocketEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    SOCKET s = (SOCKET)wParam;
    int event = WSAGETSELECTEVENT(lParam);
    int error = WSAGETSELECTERROR(lParam);

    if (s == serverSocket)
    {
        if (error != 0)
        {
            AppendLog(L"Помилка мережевого повідомлення сервера: " + std::to_wstring(error));
            ServerStop();
        }
        else if (event == FD_ACCEPT)
            AcceptClient(hWnd);
        else if (event == FD_CLOSE)
            ServerStop();
        return;
    }

    for (int i = 0; i < 2; ++i)
    {
        if (clientSockets[i] != s)
            continue;

        if (error != 0 || event == FD_CLOSE)
            DisconnectClient(i);
        else if (event == FD_READ)
            ReceiveFromClient(i);
        return;
    }
}

void BuildMenu(HWND hWnd)
{
    HMENU menu = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_EXIT, L"Вихід");
    AppendMenuW(helpMenu, MF_STRING, IDM_ABOUT, L"Про програму");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)fileMenu, L"File");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)helpMenu, L"Help");
    SetMenu(hWnd, menu);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    wcscpy_s(szTitle, L"ЛБ4 - Сервер");
    wcscpy_s(szWindowClass, L"Lab4ServerWindowClass");
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    MSG msg;

    // Main message loop:
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        80, 140, 620, 430, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
        return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            MessageBoxW(hWnd, L"Помилка ініціалізації Winsock.", L"Error", MB_ICONERROR);
            return -1;
        }
        winsockReady = true;
        BuildMenu(hWnd);

        hwndStart = CreateWindowW(L"BUTTON", L"Запустити сервер", WS_CHILD | WS_VISIBLE,
            10, 10, 150, 30, hWnd, (HMENU)(INT_PTR)ID_SERVER_START, hInst, nullptr);
        hwndStop = CreateWindowW(L"BUTTON", L"Зупинити сервер", WS_CHILD | WS_VISIBLE,
            170, 10, 150, 30, hWnd, (HMENU)(INT_PTR)ID_SERVER_STOP, hInst, nullptr);
        hwndStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            10, 50, 560, 24, hWnd, nullptr, hInst, nullptr);
        hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_READONLY,
            10, 80, 580, 260, hWnd, nullptr, hInst, nullptr);

        UpdateStatus();
        AppendLog(L"Натисніть «Запустити сервер» для очікування підключень.");
        return 0;
    }

    case WM_SIZE:
        if (hwndEdit)
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            MoveWindow(hwndEdit, 10, 80, width > 20 ? width - 20 : 1,
                height > 90 ? height - 90 : 1, TRUE);
        }
        return 0;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case ID_SERVER_START:
            ServerStart(hWnd);
            return 0;
        case ID_SERVER_STOP:
            ServerStop();
            return 0;
        case IDM_ABOUT:
            MessageBoxW(hWnd, L"Лабораторна робота №4, варіант 3.\nTCP-сервер Winsock.",
                L"Про програму", MB_OK);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_SOCKET_EVENT:
        OnSocketEvent(hWnd, wParam, lParam);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code here...
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        ServerStop();
        if (winsockReady)
        {
            WSACleanup();
            winsockReady = false;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}
