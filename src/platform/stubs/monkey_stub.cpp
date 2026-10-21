// The monkey automated-test harness (random input, remote control by a "zookeeper" over TCP) is not part of the
// offline build: it never starts, so in_monkey is never registered and nothing is sent anywhere.

#include <monkey/monkey.h>

void __cdecl Monkey_Start()
{
}

bool __cdecl Monkey_IsRunning()
{
    return false;
}

bool __cdecl Monkey_UseRandomInput()
{
    return false;
}

void __cdecl Monkey_Frame()
{
}

void __cdecl Monkey_KeepAlive()
{
}

void __cdecl Monkey_Event(const char *)
{
}

void __cdecl Monkey_Error(const char *)
{
}

void __cdecl Monkey_GrabComPrints(bool)
{
}

void __cdecl Monkey_ComPrintHook(const char *)
{
}
