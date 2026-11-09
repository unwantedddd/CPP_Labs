#include "../SharedData.h"
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>
#include <winspool.h>
#pragma comment(lib, "Winspool.lib")

const int CLIENT_INDEX = 0;
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
    data << L"Роздільна здатність основного екрана: " << GetSystemMetrics(SM_CXSCREEN) << L" x "
         << GetSystemMetrics(SM_CYSCREEN) << L" пікселів\r\n";

    // Перелічуємо встановлені черги друку. Серед них можуть бути віртуальні принтери.
    DWORD bytesNeeded = 0;
    DWORD printerCount = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    BOOL success = EnumPrintersW(flags, nullptr, 4, nullptr, 0, &bytesNeeded, &printerCount);
    DWORD errorCode = success ? ERROR_SUCCESS : GetLastError();

    if (!success && errorCode == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<BYTE> buffer(bytesNeeded);
        success = EnumPrintersW(flags, nullptr, 4, buffer.data(), (DWORD)buffer.size(), &bytesNeeded,
                                &printerCount);
        if (!success)
        {
            errorCode = GetLastError();
        }
    }

    if (success)
    {
        data << L"Встановлені принтери: " << printerCount << L"\r\n";
        if (printerCount == 0)
        {
            data << L"Принтери не знайдено.\r\n";
        }
        else
        {
            data << L"Є встановлені принтери (можливі віртуальні).\r\n";
        }
    }
    else
    {
        data << L"Не вдалося отримати список принтерів. " << GetErrorMessage(errorCode);
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
        data << L"Не вдалося отримати контекст екрана.\r\n";
    }
    return data.str();
}

void UpdateData()
{
    std::wstring text = CollectSystemData();
    SetWindowTextW(hDataEdit, text.c_str());
}

void SendData()
{
    int length = GetWindowTextLengthW(hDataEdit);
    if (length == 0 || length >= (int)TEXT_CAPACITY)
    {
        SetWindowTextW(hStatusEdit, L"Введіть від 1 до 2047 символів.");
        return;
    }
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hDataEdit, text.data(), length + 1);

    // Відкриваємо лише об'єкти, які вже створив сервер.
    HANDLE hMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAPPING_NAME);
    if (!hMapping)
    {
        SetWindowTextW(hStatusEdit, GetErrorMessage(GetLastError()).c_str());
        return;
    }

    SharedData *data = (SharedData *)MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!data)
    {
        SetWindowTextW(hStatusEdit, GetErrorMessage(GetLastError()).c_str());
        CloseHandle(hMapping);
        return;
    }

    HANDLE hMutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, MUTEX_NAME);
    HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, EVENT_NAME);
    if (!hMutex || !hEvent)
    {
        SetWindowTextW(hStatusEdit, L"Об'єкти синхронізації недоступні. Перевірте сервер.");
        if (hMutex)
        {
            CloseHandle(hMutex);
        }
        if (hEvent)
        {
            CloseHandle(hEvent);
        }
        UnmapViewOfFile(data);
        CloseHandle(hMapping);
        return;
    }

    DWORD waitResult = WaitForSingleObject(hMutex, 1000);
    if (waitResult == WAIT_OBJECT_0)
    {
        if (data->serverRunning)
        {
            ClientMessage &message = data->clients[CLIENT_INDEX];
            // У кожного клієнта власна комірка, але доступ захищає спільний м'ютекс.
            CopyMemory(message.text, text.c_str(), (length + 1) * sizeof(wchar_t));
            message.length = length;
            message.number++;

            if (SetEvent(hEvent))
            {
                SetWindowTextW(hStatusEdit, L"Дані записано. Сервер отримав сигнал про зміну.");
            }
            else
            {
                SetWindowTextW(hStatusEdit,
                               L"Дані записано, але подію не встановлено. Прочитайте їх кнопкою на сервері.");
            }
        }
        else
        {
            SetWindowTextW(hStatusEdit, L"Сервер зупинено. Запустіть його повторно.");
        }
        ReleaseMutex(hMutex);
    }
    else if (waitResult == WAIT_ABANDONED)
    {
        // Попередній власник м'ютекса аварійно завершився: запис міг бути частковим.
        data->serverRunning = FALSE;
        SetEvent(hEvent);
        ReleaseMutex(hMutex);
        SetWindowTextW(hStatusEdit, L"Попередній запис перервано. Перезапустіть сервер.");
    }
    else
    {
        SetWindowTextW(hStatusEdit, L"Не вдалося отримати м'ютекс. Повторіть надсилання.");
    }

    CloseHandle(hEvent);
    CloseHandle(hMutex);
    UnmapViewOfFile(data);
    CloseHandle(hMapping);
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
        UpdateData();
        SetWindowTextW(hStatusEdit, L"Спочатку запустіть сервер FileMapping. Текст можна редагувати.");
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_UPDATE_DATA:
            UpdateData();
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
    windowClass.lpszClassName = L"Lab6Client1";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&windowClass))
    {
        MessageBoxW(nullptr, L"Не вдалося зареєструвати клас вікна.", L"Помилка", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowW(windowClass.lpszClassName, L"Lab6 - Client1 - Варіант 3", WS_OVERLAPPEDWINDOW,
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
