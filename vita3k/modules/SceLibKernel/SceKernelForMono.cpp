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

    LOG_INFO("sceKernelWaitExceptionForMono: ExceptionHandlerThread (ID: {}) waiting for exceptions...", thread_id);

    // Register this thread as the Mono exception handler thread.
    // When a null pointer fault is detected in MemoryReadCode, the faulting thread
    // will be suspended and this thread will be resumed with the fault info.
    {
        std::lock_guard<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
        emuenv.kernel.mono_exception_handler_thread = thread_id;
    }

    // Suspend this guest thread using the normal guest thread mechanism.
    // It will be woken up by signal_mono_exception() calling resume() on us.
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread)
        return -1;

    thread->suspend();

    // When we wake up, the exception info is in KernelState
    std::lock_guard<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
    SceUID faulting_tid = emuenv.kernel.mono_exception_thread_id;
    Address fault_addr = emuenv.kernel.mono_exception_fault_addr;
    Address fault_pc = emuenv.kernel.mono_exception_fault_pc;
    emuenv.kernel.mono_exception_pending = false;

    LOG_WARN("sceKernelWaitExceptionForMono: woke up! Faulting thread ID: {}, addr: 0x{:08X}, PC: 0x{:08X}",
             faulting_tid, fault_addr, fault_pc);

    return faulting_tid;
}

EXPORT(int, sceKernelWaitExceptionCBForMono) {
    TRACY_FUNC(sceKernelWaitExceptionCBForMono);
    return CALL_EXPORT(sceKernelWaitExceptionForMono);
}