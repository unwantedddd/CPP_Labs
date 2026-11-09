#include "../SharedData.h"
#include <algorithm>
#include <string>

const int ID_START = 101;
const int ID_STOP = 102;
const int ID_READ = 103;
const int ID_AUTO = 104;

HWND hLogEdit = nullptr;
HWND hAutoCheck = nullptr;
HANDLE hMapping = nullptr;
HANDLE hMutex = nullptr;
HANDLE hChangedEvent = nullptr;
SharedData *sharedData = nullptr;
DWORD lastRead[2] = {};
bool autoRead = true;

std::wstring ErrorMessage(DWORD code)
{
    wchar_t description[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, description,
                   512, nullptr);
    return L"Помилка " + std::to_wstring(code) + L": " + description;
}

void AddToLog(const std::wstring &text)
{
    if (GetWindowTextLengthW(hLogEdit) > 150000)
    {
        SetWindowTextW(hLogEdit, L"");
    }
    SendMessageW(hLogEdit, EM_SETSEL, (WPARAM)-1, -1);
    SendMessageW(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

void CloseResources()
{
    if (sharedData)
    {
        UnmapViewOfFile(sharedData);
    }
    if (hMapping)
    {
        CloseHandle(hMapping);
    }
    if (hChangedEvent)
    {
        CloseHandle(hChangedEvent);
    }
    if (hMutex)
    {
        CloseHandle(hMutex);
    }
    sharedData = nullptr;
    hMapping = nullptr;
    hChangedEvent = nullptr;
    hMutex = nullptr;
}

void StartServer()
{
    if (hMapping)
    {
        AddToLog(L"Сервер уже запущено.\r\n");
        return;
    }

    hMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (!hMutex)
    {
        AddToLog(ErrorMessage(GetLastError()));
        return;
    }

    DWORD waitResult = WaitForSingleObject(hMutex, 1000);
    if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED)
    {
        AddToLog(L"Не вдалося отримати доступ до спільної пам'яті.\r\n");
        CloseResources();
        return;
    }

    // INVALID_HANDLE_VALUE: пам'ять підтримує файл підкачки, окремий файл не потрібний.
    hMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedData),
                                  MAPPING_NAME);
    DWORD errorCode = GetLastError();

    if (!hMapping || errorCode == ERROR_ALREADY_EXISTS)
    {
        if (errorCode == ERROR_ALREADY_EXISTS)
        {
            AddToLog(L"Проекція вже існує. Закрийте інший сервер або дочекайтеся завершення клієнтів.\r\n");
        }
        else
        {
            AddToLog(ErrorMessage(errorCode));
        }
        ReleaseMutex(hMutex);
        CloseResources();
        return;
    }

    sharedData = (SharedData *)MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!sharedData)
    {
        AddToLog(ErrorMessage(GetLastError()));
        ReleaseMutex(hMutex);
        CloseResources();
        return;
    }

    // Подія з ручним скиданням зберігає сигнал до читання сервером.
    hChangedEvent = CreateEventW(nullptr, TRUE, FALSE, EVENT_NAME);
    if (!hChangedEvent)
    {
        AddToLog(ErrorMessage(GetLastError()));
        ReleaseMutex(hMutex);
        CloseResources();
        return;
    }

    ZeroMemory(sharedData, sizeof(SharedData));
    sharedData->serverRunning = TRUE;
    lastRead[0] = lastRead[1] = 0;
    ResetEvent(hChangedEvent);
    ReleaseMutex(hMutex);
    AddToLog(L"Сервер запущено. Проекцію пам'яті створено.\r\n");
}

bool StopServer()
{
    if (!hMapping)
    {
        return true;
    }

    DWORD waitResult = WaitForSingleObject(hMutex, 1000);
    if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED)
    {
        AddToLog(L"Пам'ять зайнята. Повторіть зупинення.\r\n");
        return false;
    }

    sharedData->serverRunning = FALSE;
    ReleaseMutex(hMutex);
    CloseResources();
    AddToLog(L"Сервер зупинено. Подання пам'яті та дескриптори закрито.\r\n");
    return true;
}

void ReadData(bool showAll)
{
    if (!sharedData)
    {
        AddToLog(L"Спочатку запустіть сервер.\r\n");
        return;
    }

    DWORD waitResult = WaitForSingleObject(hMutex, 1000);
    if (waitResult == WAIT_ABANDONED)
    {
        // Інший процес завершився всередині запису: частковим даним не довіряємо.
        sharedData->serverRunning = FALSE;
        ReleaseMutex(hMutex);
        CloseResources();
        AddToLog(L"Запис був перерваний. Сервер зупинено; запустіть його повторно.\r\n");
        return;
    }
    if (waitResult != WAIT_OBJECT_0)
    {
        autoRead = false;
        SendMessageW(hAutoCheck, BM_SETCHECK, BST_UNCHECKED, 0);
        AddToLog(L"Пам'ять зайнята. Авточитання вимкнено; повторіть читання кнопкою.\r\n");
        return;
    }

    SharedData snapshot = *sharedData;
    // Скидаємо сигнал під тим самим м'ютексом: новий запис після цього знову його встановить.
    ResetEvent(hChangedEvent);
    ReleaseMutex(hMutex);

    if (!snapshot.serverRunning)
    {
        CloseResources();
        AddToLog(L"Виявлено перерваний запис. Запустіть сервер повторно.\r\n");
        return;
    }

    bool displayed = false;
    for (int i = 0; i < 2; i++)
    {
        const ClientMessage &message = snapshot.clients[i];
        if (message.length == 0)
        {
            continue;
        }
        if (!showAll && message.number == lastRead[i])
        {
            continue;
        }
        if (message.length >= TEXT_CAPACITY)
        {
            AddToLog(L"Неправильна довжина повідомлення.\r\n");
            continue;
        }

        std::wstring text(message.text, message.length);
        AddToLog(L"\r\nКлієнт " + std::to_wstring(i + 1) + L", запис №" + std::to_wstring(message.number) +
                 L"\r\n" + text + L"\r\n");
        lastRead[i] = message.number;
        displayed = true;
    }
    if (!displayed && showAll)
    {
        AddToLog(L"Клієнти ще не записали дані.\r\n");
    }
}

void CreateButton(HWND hWnd, const wchar_t *text, int id, int x, int width)
{
    HWND button = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, 12, width, 32, hWnd,
                                (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(button, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateButton(hWnd, L"Запустити сервер", ID_START, 12, 160);
        CreateButton(hWnd, L"Зупинити сервер", ID_STOP, 182, 160);
        CreateButton(hWnd, L"Прочитати дані", ID_READ, 352, 160);
        hAutoCheck =
            CreateWindowW(L"BUTTON", L"Авточитання", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                          530, 12, 160, 32, hWnd, (HMENU)(INT_PTR)ID_AUTO, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hAutoCheck, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(hAutoCheck, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        hLogEdit =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            12, 60, 700, 400, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hLogEdit, EM_SETLIMITTEXT, 200000, 0);
        SendMessageW(hLogEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        AddToLog(L"Лабораторна робота 6. Варіант 3. File mapping.\r\nЗапустіть сервер.\r\n");
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
        case ID_READ:
            ReadData(true);
            return 0;
        case ID_AUTO:
            autoRead = SendMessageW(hAutoCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            return 0;
        }
        break;
    case WM_SIZE:
        MoveWindow(hLogEdit, 12, 60, std::max(100, (int)LOWORD(lParam) - 24),
                   std::max(60, (int)HIWORD(lParam) - 72), TRUE);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize = {740, 350};
        return 0;
    case WM_CLOSE:
        if (StopServer())
        {
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_DESTROY:
        CloseResources();
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
    windowClass.lpszClassName = L"Lab6Server";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&windowClass))
    {
        return 1;
    }
    HWND hWnd = CreateWindowW(windowClass.lpszClassName, L"Lab6 - Server - Варіант 3", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 860, 580, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd)
    {
        return 1;
    }
    ShowWindow(hWnd, nCmdShow);

    // Очікуємо і подію клієнта, і повідомлення вікна. Окремий потік або таймер не потрібні.
    MSG message = {};
    while (true)
    {
        DWORD count = (autoRead && hChangedEvent) ? 1 : 0;
        DWORD result = MsgWaitForMultipleObjectsEx(count, count ? &hChangedEvent : nullptr, INFINITE,
                                                   QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_FAILED)
        {
            MessageBoxW(hWnd, ErrorMessage(GetLastError()).c_str(), L"Помилка очікування", MB_ICONERROR);
            StopServer();
            return 1;
        }
        if (count == 1 && result == WAIT_OBJECT_0)
        {
            ReadData(false);
        }

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                return (int)message.wParam;
            }
            if (!IsDialogMessageW(hWnd, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
}

