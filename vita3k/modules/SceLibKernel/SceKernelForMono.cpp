// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <module/module.h>

#include "../SceKernelThreadMgr/SceThreadmgr.h"
#include <cpu/functions.h>
#include <kernel/state.h>
#include <util/tracy.h>
TRACY_MODULE_NAME(SceKernelForMono);

EXPORT(int, sceKernelGetThreadContextForMono, SceUID threadId, Ptr<SceKernelThreadCpuRegisterInfo> pCpuRegisterInfo, Ptr<SceKernelThreadVfpRegisterInfo> pVfpRegisterInfo) {
    TRACY_FUNC(sceKernelGetThreadContextForMono, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
    return CALL_EXPORT(_sceKernelGetThreadContextForVM, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
}

EXPORT(int, sceKernelResumeThreadForMono, SceUID threadId) {
    TRACY_FUNC(sceKernelResumeThreadForMono, threadId);
    return CALL_EXPORT(sceKernelResumeThreadForVM, threadId);
}

EXPORT(int, sceKernelSetThreadContextForMono, SceUID threadId, Ptr<SceKernelThreadCpuRegisterInfo> pCpuRegisterInfo, Ptr<SceKernelThreadVfpRegisterInfo> pVfpRegisterInfo) {
    TRACY_FUNC(sceKernelSetThreadContextForMono, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
    return CALL_EXPORT(_sceKernelSetThreadContextForVM, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
}

EXPORT(int, sceKernelSuspendThreadForMono, SceUID threadId) {
    TRACY_FUNC(sceKernelSuspendThreadForMono, threadId);
    return CALL_EXPORT(sceKernelSuspendThreadForVM, threadId);
}

EXPORT(int, sceKernelWaitExceptionForMono) {
    TRACY_FUNC(sceKernelWaitExceptionForMono);

    // On real Vita, this function blocks until another thread triggers a
    // hardware exception (null pointer dereference, illegal instruction, etc.).
    // The kernel then wakes this thread with info about the faulting thread.
    // Mono uses this to implement C# exception handling:
    // 1. This function returns the faulting thread ID
    // 2. Mono suspends the faulting thread
    // 3. Mono reads the faulting thread's CPU context
    // 4. Mono modifies PC to point to the C# exception handler
    // 5. Mono resumes the faulting thread
    //
    // In Vita3K, Dynarmic detects null accesses in MemoryReadCode/MemoryRead
    // and signals us via the mono_exception_* fields in KernelState.

    LOG_INFO("sceKernelWaitExceptionForMono: ExceptionHandlerThread (ID: {}) waiting for exceptions...", thread_id);

    // Wait on the condition variable until an exception is signaled
    std::unique_lock<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
    emuenv.kernel.mono_exception_cond.wait(lock, [&] {
        return emuenv.kernel.mono_exception_pending;
    });

    // Exception received
    SceUID faulting_tid = emuenv.kernel.mono_exception_thread_id;
    Address fault_addr = emuenv.kernel.mono_exception_fault_addr;
    Address fault_pc = emuenv.kernel.mono_exception_fault_pc;
    emuenv.kernel.mono_exception_pending = false;

    lock.unlock();

    LOG_WARN("sceKernelWaitExceptionForMono: exception received! Faulting thread ID: {}, addr: 0x{:08X}, PC: 0x{:08X}",
             faulting_tid, fault_addr, fault_pc);

    // Return the faulting thread ID
    // On real Vita, the return value format may be more complex,
    // but the thread ID is the essential piece Mono needs to call
    // SuspendThreadForMono/GetThreadContextForMono etc.
    return faulting_tid;
}

EXPORT(int, sceKernelWaitExceptionCBForMono) {
    TRACY_FUNC(sceKernelWaitExceptionCBForMono);
    return CALL_EXPORT(sceKernelWaitExceptionForMono);
}