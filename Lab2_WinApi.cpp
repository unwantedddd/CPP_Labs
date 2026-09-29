// Lab2_WinApi.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "Lab2_WinApi.h"

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_LAB2WINAPI, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_LAB2WINAPI));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
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

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAB2WINAPI));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_LAB2WINAPI);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

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
   {
      return FALSE;
   }

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
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
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
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rect;
            GetClientRect(hWnd, &rect);

            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            int centerX = width / 2;
            int centerY = height / 2;

            HPEN axisPen = CreatePen(PS_SOLID, 2, RGB(0, 128, 0));
            HPEN oldPen = (HPEN)SelectObject(hdc, axisPen);

            // Вісь X
            MoveToEx(hdc, 0, centerY, nullptr);
            LineTo(hdc, width, centerY);

            // Вісь Y
            MoveToEx(hdc, centerX, 0, nullptr);
            LineTo(hdc, centerX, height);

            SelectObject(hdc, oldPen);
            DeleteObject(axisPen);

            int amplitude = height / 3;
            int offset = (int)(amplitude * 0.5236);

            // Синус
            POINT sinPoints[13] =
            {
                { 0, centerY },

                { width / 12, centerY - offset },
                { width / 6, centerY - amplitude },
                { width / 4, centerY - amplitude },

                { width / 3, centerY - amplitude },
                { 5 * width / 12, centerY - offset },
                { width / 2, centerY },

                { 7 * width / 12, centerY + offset },
                { 2 * width / 3, centerY + amplitude },
                { 3 * width / 4, centerY + amplitude },

                { 5 * width / 6, centerY + amplitude },
                { 11 * width / 12, centerY + offset },
                { width, centerY }
            };

            HPEN sinPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
            oldPen = (HPEN)SelectObject(hdc, sinPen);

            PolyBezier(hdc, sinPoints, 13);

            SelectObject(hdc, oldPen);
            DeleteObject(sinPen);

            // Косинус
            POINT cosPoints[13] =
            {
                { 0, centerY - amplitude },

                { width / 12, centerY - amplitude },
                { width / 6, centerY - offset },
                { width / 4, centerY },

                { width / 3, centerY + offset },
                { 5 * width / 12, centerY + amplitude },
                { width / 2, centerY + amplitude },

                { 7 * width / 12, centerY + amplitude },
                { 2 * width / 3, centerY + offset },
                { 3 * width / 4, centerY },

                { 5 * width / 6, centerY - offset },
                { 11 * width / 12, centerY - amplitude },
                { width, centerY - amplitude }
            };

            HPEN cosPen = CreatePen(PS_SOLID, 3, RGB(0, 0, 255));
            oldPen = (HPEN)SelectObject(hdc, cosPen);

            PolyBezier(hdc, cosPoints, 13);

            SelectObject(hdc, oldPen);
            DeleteObject(cosPen);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
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
