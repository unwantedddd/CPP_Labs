#include "../PipeCommon.h"
#include <algorithm>
#include <sstream>

const DWORD CLIENT_NUMBER = 2;
const int ID_UPDATE_DATA = 101;
const int ID_SEND_DATA = 102;
HWND hDataEdit = nullptr;
HWND hStatusEdit = nullptr;

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

BOOL CALLBACK CountVisibleWindows(HWND window, LPARAM param)
{
    // Рахуємо видимі вікна верхнього рівня, які мають заголовок.
    if (IsWindowVisible(window) && GetWindowTextLengthW(window) > 0)
    {
        ++*reinterpret_cast<unsigned *>(param);
    }
    return TRUE;
}

std::wstring CollectSystemData(HWND window)
{
    std::wostringstream data;
    unsigned count = 0;
    data << L"Клієнт 2 - варіант 6\r\n";
    if (EnumWindows(CountVisibleWindows, reinterpret_cast<LPARAM>(&count)))
    {
        data << L"Видимі вікна верхнього рівня із заголовком: " << count << L"\r\n";
    }
    else
    {
        data << L"Не вдалося перелічити вікна\r\n";
    }
    RECT windowRect{};
    if (GetWindowRect(window, &windowRect))
    {
        data << L"Ширина власного вікна з рамкою: " << windowRect.right - windowRect.left << L" пікселів\r\n";
    }
    else
    {
        data << ErrorMessage(GetLastError());
    }
    HDC hDC = GetDC(window);
    if (hDC)
    {
        data << L"Вертикальний логічний DPI (LOGPIXELSY): " << GetDeviceCaps(hDC, LOGPIXELSY) << L"\r\n";
        ReleaseDC(window, hDC);
    }
    else
    {
        data << L"Не вдалося отримати контекст вікна\r\n";
    }
    return data.str();
}

void UpdateData(HWND hWnd)
{
    std::wstring data = CollectSystemData(hWnd);
    SetWindowTextW(hDataEdit, data.c_str());
}

// Одна відправка: підключення, пакет, відповідь сервера, закриття каналу.
bool SendToServer(const std::wstring &text, DWORD &error)
{
    if (text.empty() || text.size() >= TEXT_CAPACITY)
    {
        error = ERROR_INVALID_PARAMETER;
        return false;
    }
    HANDLE pipe = OpenServerPipe(error);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    MessagePacket packet = {};
    packet.clientNumber = CLIENT_NUMBER;
    packet.textLength = (DWORD)text.size();
    CopyMemory(packet.text, text.c_str(), (text.size() + 1) * sizeof(wchar_t));

    bool success = WriteToPipe(pipe, &packet, sizeof(packet), error);
    if (success)
    {
        DWORD reply = 0;
        DWORD bytes = 0;
        success = ReadFromPipe(pipe, &reply, sizeof(reply), bytes, error);
        if (success)
        {
            // Сервер чекає квитанцію, щоб не закрити канал раніше читання відповіді.
            DWORD receipt = RECEIPT;
            DWORD receiptError = 0;
            WriteToPipe(pipe, &receipt, sizeof(receipt), receiptError);
            success = bytes == sizeof(reply) && reply == REPLY_OK;
            if (!success)
            {
                error = ERROR_INVALID_DATA;
            }
        }
    }
    CloseHandle(pipe);
    return success;
}

void SendData()
{
    int length = GetWindowTextLengthW(hDataEdit);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hDataEdit, text.data(), length + 1);
    text.resize(length);

    DWORD error = 0;
    if (SendToServer(text, error))
    {
        SetWindowTextW(hStatusEdit, L"Сервер прийняв повідомлення і надіслав підтвердження.");
    }
    else
    {
        SetWindowTextW(hStatusEdit, ErrorMessage(error).c_str());
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
        SendMessageW(hDataEdit, EM_SETLIMITTEXT, TEXT_CAPACITY - 1, 0);
        hStatusEdit = CreateTextBox(window, 202, true);
        UpdateData(window);
        SetWindowTextW(hStatusEdit, L"Спочатку запустіть сервер каналів. Текст можна редагувати.");
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

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine,
                    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    SetProcessDPIAware();

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = L"Lab7Client2";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&windowClass))
    {
        MessageBoxW(nullptr, L"Не вдалося зареєструвати клас вікна.", L"Помилка", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowW(windowClass.lpszClassName, L"Lab7 - Client2 - Варіант 6", WS_OVERLAPPEDWINDOW,
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
