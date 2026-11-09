#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

const wchar_t PIPE_NAME[] = L"\\\\.\\pipe\\Lab7_Variant6";
const DWORD TEXT_CAPACITY = 2048;
const DWORD IO_TIMEOUT = 3000;
const DWORD REPLY_OK = 1;
const DWORD REPLY_INVALID = 2;
const DWORD RECEIPT = 3;

struct MessagePacket
{
    DWORD clientNumber;
    DWORD textLength;
    wchar_t text[TEXT_CAPACITY];
};

// Власні повідомлення не залежать від мови Windows.
inline std::wstring ErrorMessage(DWORD code)
{
    std::wstring text;
    switch (code)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        text = L"Сервер не запущено. Спочатку запустіть сервер.";
        break;
    case ERROR_ACCESS_DENIED:
        text = L"Доступ заборонено. Перевірте, чи не працює інший сервер.";
        break;
    case ERROR_PIPE_BUSY:
        text = L"Усі канали зайняті. Повторіть надсилання.";
        break;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
    case ERROR_PIPE_NOT_CONNECTED:
        text = L"З'єднання із сервером закрито.";
        break;
    case ERROR_TIMEOUT:
    case ERROR_SEM_TIMEOUT:
        text = L"Час очікування минув. Перевірте сервер і повторіть дію.";
        break;
    case ERROR_OPERATION_ABORTED:
        text = L"Операцію скасовано.";
        break;
    case ERROR_INVALID_DATA:
    case ERROR_MORE_DATA:
        text = L"Отримано повідомлення неправильного формату.";
        break;
    case ERROR_INVALID_PARAMETER:
        text = L"Некоректні дані. Дозволено від 1 до 2047 символів.";
        break;
    default:
        text = L"Не вдалося виконати операцію з каналом.";
        break;
    }
    return text + L" Код: " + std::to_wstring(code) + L".\r\n";
}

// OVERLAPPED і буфер мають існувати до завершення або повного скасування операції.
inline bool WaitForIo(HANDLE pipe, OVERLAPPED &operation, BOOL completed, DWORD startError, HANDLE stopEvent,
                      DWORD timeout, DWORD &bytes, DWORD &error)
{
    if (!completed && startError != ERROR_IO_PENDING)
    {
        error = startError;
        return false;
    }
    if (!completed)
    {
        HANDLE events[2] = {operation.hEvent, stopEvent};
        DWORD result = WaitForMultipleObjects(stopEvent ? 2 : 1, events, FALSE, timeout);
        if (result != WAIT_OBJECT_0)
        {
            error = ERROR_OPERATION_ABORTED;
            if (result == WAIT_TIMEOUT)
            {
                error = ERROR_TIMEOUT;
            }
            if (result == WAIT_FAILED)
            {
                error = GetLastError();
            }
            CancelIoEx(pipe, &operation);
            DWORD ignored = 0;
            GetOverlappedResult(pipe, &operation, &ignored, TRUE);
            return false;
        }
    }
    if (!GetOverlappedResult(pipe, &operation, &bytes, FALSE))
    {
        error = GetLastError();
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

inline bool ReadFromPipe(HANDLE pipe, void *buffer, DWORD size, DWORD &bytes, DWORD &error,
                         HANDLE stopEvent = nullptr, DWORD timeout = IO_TIMEOUT)
{
    OVERLAPPED operation = {};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent)
    {
        error = GetLastError();
        return false;
    }
    BOOL completed = ReadFile(pipe, buffer, size, nullptr, &operation);
    DWORD startError = completed ? ERROR_SUCCESS : GetLastError();
    bool success = WaitForIo(pipe, operation, completed, startError, stopEvent, timeout, bytes, error);
    CloseHandle(operation.hEvent);
    return success;
}

inline bool WriteToPipe(HANDLE pipe, const void *buffer, DWORD size, DWORD &error, HANDLE stopEvent = nullptr)
{
    OVERLAPPED operation = {};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent)
    {
        error = GetLastError();
        return false;
    }
    BOOL completed = WriteFile(pipe, buffer, size, nullptr, &operation);
    DWORD startError = completed ? ERROR_SUCCESS : GetLastError();
    DWORD written = 0;
    bool success = WaitForIo(pipe, operation, completed, startError, stopEvent, IO_TIMEOUT, written, error);
    CloseHandle(operation.hEvent);
    if (success && written != size)
    {
        error = ERROR_WRITE_FAULT;
        return false;
    }
    return success;
}

inline HANDLE OpenServerPipe(DWORD &error)
{
    ULONGLONG deadline = GetTickCount64() + 2000;
    while (true)
    {
        HANDLE pipe = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
        {
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
            {
                error = GetLastError();
                CloseHandle(pipe);
                return INVALID_HANDLE_VALUE;
            }
            error = ERROR_SUCCESS;
            return pipe;
        }
        error = GetLastError();
        if (error != ERROR_PIPE_BUSY)
        {
            return INVALID_HANDLE_VALUE;
        }
        ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            error = ERROR_PIPE_BUSY;
            return INVALID_HANDLE_VALUE;
        }
        if (!WaitNamedPipeW(PIPE_NAME, (DWORD)(deadline - now)))
        {
            error = GetLastError();
            return INVALID_HANDLE_VALUE;
        }
    }
}
