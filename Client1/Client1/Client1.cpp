// Lab4_Client1.cpp : Defines the entry point for the application.
//

#include <winsock2.h>
#include <windows.h>
#include <winspool.h>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Winspool.lib")

#define MAX_LOADSTRING 100
#define SERVER_PORT 5000

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

constexpr int ID_CLIENT_CONNECT = 101;
constexpr int ID_CLIENT_SEND = 102;
constexpr int ID_CLIENT_DISCONNECT = 103;
constexpr int IDM_EXIT = 40001;
constexpr int IDM_ABOUT = 40002;

HWND hwndEdit = nullptr;
HWND hwndStatus = nullptr;
HWND hwndConnect = nullptr;
HWND hwndSend = nullptr;
HWND hwndDisconnect = nullptr;

SOCKET clientSocket = INVALID_SOCKET;
std::wstring logText;
bool winsockReady = false;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

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

void UpdateStatus()
{
    bool connected = clientSocket != INVALID_SOCKET;
    if (hwndStatus)
        SetWindowTextW(hwndStatus, connected ? L"Статус: підключено до 127.0.0.1:5000"
            : L"Статус: немає з'єднання");
    if (hwndConnect)
        EnableWindow(hwndConnect, !connected);
    if (hwndSend)
        EnableWindow(hwndSend, connected);
    if (hwndDisconnect)
        EnableWindow(hwndDisconnect, connected);
}

void ClientDisconnect()
{
    if (clientSocket != INVALID_SOCKET)
    {
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        AppendLog(L"З'єднання закрито.");
    }
    UpdateStatus();
}

void ClientConnect()
{
    if (!winsockReady || clientSocket != INVALID_SOCKET)
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

    if (connect(s, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
    {
        AppendLog(L"Помилка connect(): " + std::to_wstring(WSAGetLastError()) +
            L". Спочатку запустіть сервер.");
        closesocket(s);
        return;
    }

    clientSocket = s;
    AppendLog(L"Підключено до сервера 127.0.0.1:5000.");
    UpdateStatus();
}

std::string ToUtf8(const std::wstring& text)
{
    int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
        nullptr, 0, nullptr, nullptr);
    if (length == 0)
        return {};
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
        &result[0], length, nullptr, nullptr);
    return result;
}

bool GetPrinterCount(DWORD& count)
{
    DWORD needed = 0;
    count = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;

    BOOL firstCall = EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &count);
    if (needed == 0)
        return firstCall != FALSE;

    std::vector<BYTE> buffer(needed);
    return EnumPrintersW(flags, nullptr, 4, buffer.data(), needed, &needed, &count) != FALSE;
}

void SendInfo(HWND hWnd)
{
    if (clientSocket == INVALID_SOCKET)
        return;

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    DWORD printerCount = 0;
    bool printersKnown = GetPrinterCount(printerCount);
    std::wstring printerText = printersKnown
        ? (printerCount > 0 ? L"так (" + std::to_wstring(printerCount) + L" черг)" : L"ні")
        : L"не вдалося визначити";

    HDC hdc = GetDC(hWnd);
    int colorDepth = hdc ? GetDeviceCaps(hdc, BITSPIXEL) : 0;
    if (hdc)
        ReleaseDC(hWnd, hdc);

    std::wstring text = L"Клієнт 1 | Роздільна здатність: " +
        std::to_wstring(screenWidth) + L"x" + std::to_wstring(screenHeight) +
        L" px | Встановлений принтер: " + printerText +
        L" | Глибина кольору (BITSPIXEL): " + std::to_wstring(colorDepth) + L" біт";

    AppendLog(L"Підготовлені дані: " + text);
    std::string bytes = ToUtf8(text + L"\n");
    int sentTotal = 0;
    while (sentTotal < (int)bytes.size())
    {
        int result = send(clientSocket, bytes.data() + sentTotal,
            (int)bytes.size() - sentTotal, 0);
        if (result == SOCKET_ERROR || result == 0)
        {
            AppendLog(L"Помилка send(): " + std::to_wstring(WSAGetLastError()));
            ClientDisconnect();
            return;
        }
        sentTotal += result;
    }
    AppendLog(L"Дані надіслано серверу.");
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
    wcscpy_s(szTitle, L"ЛБ4 - Клієнт 1");
    wcscpy_s(szWindowClass, L"Lab4Client1WindowClass");
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
        740, 100, 630, 360,
        nullptr, nullptr, hInstance, nullptr);

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

        hwndConnect = CreateWindowW(L"BUTTON", L"Підключитися", WS_CHILD | WS_VISIBLE,
            10, 10, 155, 30, hWnd, (HMENU)(INT_PTR)ID_CLIENT_CONNECT, hInst, nullptr);
        hwndSend = CreateWindowW(L"BUTTON", L"Надіслати дані", WS_CHILD | WS_VISIBLE,
            175, 10, 155, 30, hWnd, (HMENU)(INT_PTR)ID_CLIENT_SEND, hInst, nullptr);
        hwndDisconnect = CreateWindowW(L"BUTTON", L"Відключитися", WS_CHILD | WS_VISIBLE,
            340, 10, 155, 30, hWnd, (HMENU)(INT_PTR)ID_CLIENT_DISCONNECT, hInst, nullptr);
        hwndStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            10, 50, 580, 24, hWnd, nullptr, hInst, nullptr);
        hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_READONLY,
            10, 80, 590, 210, hWnd, nullptr, hInst, nullptr);

        UpdateStatus();
        AppendLog(L"Спочатку запустіть сервер, потім натисніть «Підключитися».");
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
        case ID_CLIENT_CONNECT:
            ClientConnect();
            return 0;
        case ID_CLIENT_SEND:
            SendInfo(hWnd);
            return 0;
        case ID_CLIENT_DISCONNECT:
            ClientDisconnect();
            return 0;
        case IDM_ABOUT:
            MessageBoxW(hWnd, L"Лабораторна робота №4, варіант 3.\nTCP-клієнт 1.",
                L"Про програму", MB_OK);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code here...
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        ClientDisconnect();
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
