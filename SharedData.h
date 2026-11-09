#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Усі три програми повинні використовувати однакові імена та структури.
const wchar_t MAPPING_NAME[] = L"Local\\Lab6_Variant3_Memory";
const wchar_t MUTEX_NAME[] = L"Local\\Lab6_Variant3_Mutex";
const wchar_t EVENT_NAME[] = L"Local\\Lab6_Variant3_Changed";
const DWORD TEXT_CAPACITY = 2048;

struct ClientMessage
{
    DWORD number; // Номер останнього запису цього клієнта.
    DWORD length; // Кількість символів, без завершального нуля.
    wchar_t text[TEXT_CAPACITY];
};

struct SharedData
{
    BOOL serverRunning;
    ClientMessage clients[2]; // Окреме місце для кожного клієнта.
};

// Це останні значення, а не черга: новий запис замінює попередній запис клієнта.
// У спільній пам'яті немає вказівників або std::wstring.
