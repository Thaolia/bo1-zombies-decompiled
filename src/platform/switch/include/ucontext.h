#pragma once
// aarch64 newlib (Switch) ships no <ucontext.h>. win32_threads.cpp's Bo1_CaptureContext reads the aarch64 register
// set a signal hands it; this gives that type the Linux layout so the shared file compiles. The signal-based
// SuspendThread does not actually fire on libnx -- a real libnx suspend (svcGetThreadContext3) is a boot-milestone
// task (docs/PORTING.md, phase 5); this only unblocks the compile.
#include <stdint.h>

typedef struct
{
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
} mcontext_t;

typedef struct ucontext
{
    mcontext_t uc_mcontext;
} ucontext_t;
