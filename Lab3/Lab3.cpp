// Lab3_WinApi.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "Lab3.h"

#include <algorithm>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

std::wstring currentInput;
std::vector<std::wstring> inputGroups;
std::vector<std::wstring> sortedGroups;
std::queue<wchar_t> pendingCharacters;
std::queue<std::wstring> pendingSort;
std::mutex dataMutex;

HANDLE hInputThread = nullptr;
HANDLE hSortThread = nullptr;
HANDLE hInputEvent = nullptr;
HANDLE hSortEvent = nullptr;
HANDLE hStopEvent = nullptr;

constexpr UINT WM_APP_REFRESH = WM_APP + 1;

// Forward declarations of functions included in this code module:
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI InputThreadProc(LPVOID param);
DWORD WINAPI SortThreadProc(LPVOID param);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_LAB3WINAPI, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_LAB3WINAPI));
    MSG msg;

    // Main message loop:
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!TranslateAcceleratorW(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
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
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAB3WINAPI));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_LAB3WINAPI);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

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
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
        return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

DWORD WINAPI InputThreadProc(LPVOID param)
{
    HWND hWnd = (HWND)param;
    HANDLE events[2] = { hStopEvent, hInputEvent };

    while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        while (true)
        {
            bool groupReady = false;

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                if (pendingCharacters.empty())
                    break;

                wchar_t ch = pendingCharacters.front();
                pendingCharacters.pop();

                if (ch == L'\r')
                {
                    if (!currentInput.empty())
                    {
                        inputGroups.push_back(currentInput);
                        pendingSort.push(currentInput);
                        currentInput.clear();
                        groupReady = true;
                    }
                }
                else if (ch == L'\b')
                {
                    if (!currentInput.empty())
                        currentInput.pop_back();
                }
                else if (ch >= 32)
                {
                    currentInput += ch;
                }
            }

            if (groupReady)
                SetEvent(hSortEvent);

            PostMessageW(hWnd, WM_APP_REFRESH, 0, 0);

            if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0)
                return 0;
        }
    }

    return 0;
}

DWORD WINAPI SortThreadProc(LPVOID param)
{
    HWND hWnd = (HWND)param;
    HANDLE events[2] = { hStopEvent, hSortEvent };

    while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        while (true)
        {
            std::wstring group;

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                if (pendingSort.empty())
                    break;

                group = pendingSort.front();
                pendingSort.pop();
            }

            std::sort(group.begin(), group.end());

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                sortedGroups.push_back(group);
            }

            PostMessageW(hWnd, WM_APP_REFRESH, 0, 0);

            if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0)
                return 0;
        }
    }

    return 0;
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
        hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        hInputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        hSortEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        if (!hStopEvent || !hInputEvent || !hSortEvent)
            return -1;

        hInputThread = CreateThread(nullptr, 0, InputThreadProc, hWnd, 0, nullptr);
        hSortThread = CreateThread(nullptr, 0, SortThreadProc, hWnd, 0, nullptr);

        if (!hInputThread || !hSortThread)
            return -1;

        return 0;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_CHAR:
        if (wParam == L'\r' || wParam == L'\b' || wParam >= 32)
        {
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                pendingCharacters.push((wchar_t)wParam);
            }
            SetEvent(hInputEvent);
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_APP_REFRESH:
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rect;
        GetClientRect(hWnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        int centerX = width / 2;

        HPEN linePen = CreatePen(PS_SOLID, 2, RGB(100, 100, 100));
        HPEN oldPen = (HPEN)SelectObject(hdc, linePen);
        MoveToEx(hdc, centerX, 0, nullptr);
        LineTo(hdc, centerX, height);
        SelectObject(hdc, oldPen);
        DeleteObject(linePen);

        RECT leftTitle = { 20, 20, centerX - 20, 60 };
        RECT rightTitle = { centerX + 20, 20, width - 20, 60 };
        DrawTextW(hdc, L"Потік 1 - Введення", -1, &leftTitle, DT_LEFT | DT_SINGLELINE);
        DrawTextW(hdc, L"Потік 2 - Сортування", -1, &rightTitle, DT_LEFT | DT_SINGLELINE);

        std::wstring text;
        std::vector<std::wstring> leftGroups;
        std::vector<std::wstring> rightGroups;

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            text = currentInput;
            leftGroups = inputGroups;
            rightGroups = sortedGroups;
        }

        text = L"Введення: " + text;
        RECT inputRect = { 20, 70, centerX - 20, 100 };
        DrawTextW(hdc, text.c_str(), -1, &inputRect,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        int y = 115;
        for (const auto& group : leftGroups)
        {
            if (y >= height)
                break;
            RECT groupRect = { 20, y, centerX - 20, y + 25 };
            DrawTextW(hdc, group.c_str(), -1, &groupRect,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 30;
        }

        y = 115;
        for (const auto& group : rightGroups)
        {
            if (y >= height)
                break;
            RECT groupRect = { centerX + 20, y, width - 20, y + 25 };
            DrawTextW(hdc, group.c_str(), -1, &groupRect,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 30;
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        if (hStopEvent)
            SetEvent(hStopEvent);

        if (hInputThread)
        {
            WaitForSingleObject(hInputThread, INFINITE);
            CloseHandle(hInputThread);
        }
        if (hSortThread)
        {
            WaitForSingleObject(hSortThread, INFINITE);
            CloseHandle(hSortThread);
        }

        if (hInputEvent) CloseHandle(hInputEvent);
        if (hSortEvent) CloseHandle(hSortEvent);
        if (hStopEvent) CloseHandle(hStopEvent);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}
