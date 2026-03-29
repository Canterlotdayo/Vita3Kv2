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
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <mem/functions.h>
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

EXPORT(int, sceKernelWaitExceptionForMono, Ptr<uint32_t> pInfo, int type, int flags) {
    TRACY_FUNC(sceKernelWaitExceptionForMono, pInfo, type, flags);

    LOG_INFO("sceKernelWaitExceptionForMono: ExceptionHandlerThread (ID: {}) waiting for exceptions...", thread_id);

    {
        std::lock_guard<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
        emuenv.kernel.mono_exception_handler_thread = thread_id;

        if (emuenv.kernel.mono_exception_sema == 0) {
            emuenv.kernel.mono_exception_sema = semaphore_create(emuenv.kernel, export_name,
                "MonoExceptionSema", thread_id, 0, 0, 1);
            LOG_INFO("Created Mono exception semaphore: {}", emuenv.kernel.mono_exception_sema);
        }
    }

    SceUID sema = emuenv.kernel.mono_exception_sema;
    semaphore_wait(emuenv.kernel, export_name, thread_id, sema, 1, nullptr);

    SceUID faulting_tid;
    Address fault_addr;
    Address fault_pc;
    bool is_prefetch;
    {
        std::lock_guard<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
        faulting_tid = emuenv.kernel.mono_exception_thread_id;
        fault_addr = emuenv.kernel.mono_exception_fault_addr;
        fault_pc = emuenv.kernel.mono_exception_fault_pc;
        is_prefetch = emuenv.kernel.mono_exception_is_prefetch;
        emuenv.kernel.mono_exception_pending = false;
    }

    // Wait for the faulting thread to actually reach suspend state.
    // This is CRITICAL: we must wait until Dynarmic's run() has returned
    // and the thread's run_loop has reached the suspend wait point. Only then
    // has Dynarmic committed all guest registers to JitState, making
    // save_context() return accurate values.
    auto faulting_thread = emuenv.kernel.get_thread(faulting_tid);
    if (faulting_thread) {
        for (int i = 0; i < 10000; i++) {
            {
                std::lock_guard<std::mutex> tlock(faulting_thread->mutex);
                if (faulting_thread->status == ThreadStatus::suspend ||
                    faulting_thread->status == ThreadStatus::wait ||
                    faulting_thread->status == ThreadStatus::dormant) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        // Save the context NOW, after run() has returned and registers are committed.
        {
            std::lock_guard<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
            emuenv.kernel.mono_exception_saved_context = save_context(*faulting_thread->cpu);

            Address committed_pc = emuenv.kernel.mono_exception_saved_context.cpu_registers[15];

            if (is_prefetch) {
                // PREFETCH abort: fault_pc is LR (set by MemoryReadCode), which is the
                // address of the call instruction that jumped to NULL. This is correct.
                emuenv.kernel.mono_exception_saved_context.cpu_registers[15] = fault_pc;
            } else {
                // DATA abort: fault_pc comes from get_pc() during the MemoryRead callback,
                // which is STALE (Dynarmic doesn't update PC per-instruction during JIT
                // execution). The committed PC from save_context() is what Dynarmic set
                // when HaltExecution was processed — it's near the faulting instruction.
                // If the committed PC looks valid (in a loaded module), use it.
                // Otherwise fall back to fault_pc, then LR.
                Address lr = emuenv.kernel.mono_exception_saved_context.cpu_registers[14];
                if (committed_pc >= 0x80000000 && committed_pc < 0x90000000) {
                    // Committed PC is in the guest address range — use it as-is
                    // (don't override, save_context already set it)
                } else if (fault_pc >= 0x80000000 && fault_pc < 0x90000000) {
                    // fault_pc looks valid — use it
                    emuenv.kernel.mono_exception_saved_context.cpu_registers[15] = fault_pc;
                } else if (lr >= 0x80000000 && lr < 0x90000000) {
                    // Use LR as last resort — it's the return address
                    emuenv.kernel.mono_exception_saved_context.cpu_registers[15] = lr;
                }
                // else: leave committed_pc as-is, even if it looks bad
            }

            Address final_pc = emuenv.kernel.mono_exception_saved_context.cpu_registers[15];
            auto &ctx = emuenv.kernel.mono_exception_saved_context;
            LOG_WARN("WaitExceptionForMono: thread {} committed_pc=0x{:08X} fault_pc=0x{:08X} final_pc=0x{:08X} SP=0x{:08X} LR=0x{:08X} type={}",
                     faulting_tid, committed_pc, fault_pc, final_pc, ctx.cpu_registers[13], ctx.cpu_registers[14],
                     is_prefetch ? "PREFETCH" : "DATA");

            // Also update fault_pc for the pInfo struct below
            fault_pc = final_pc;
        }
    }

    // Write exception info into the output structure.
    // Layout as expected by Unity's mono-vita exception handler:
    //   +0x00 (info[0]): size (0x18, already set by caller)
    //   +0x04 (info[1]): faulting thread SceUID
    //   +0x08 (info[2]): fault address
    //   +0x0C (info[3]): fault PC (program counter at fault instruction)
    //   +0x10 (info[4]): exception type (0x10=prefetch abort, 0x20=data abort)
    //   +0x14 (info[5]): DFSR/IFSR status register (0)
    if (pInfo) {
        uint32_t *info = pInfo.get(emuenv.mem);
        // info[0] = size, already set by caller (0x18)
        info[1] = static_cast<uint32_t>(faulting_tid);      // faulting thread ID
        info[2] = fault_addr;                               // fault address
        info[3] = fault_pc;                                 // PC at fault
        info[4] = is_prefetch ? 0x10 : 0x20;               // exception type
        info[5] = 0;                                        // DFSR/IFSR (not emulated)
    }

    LOG_WARN("sceKernelWaitExceptionForMono: woke up! Faulting thread ID: {}, addr: 0x{:08X}, PC: 0x{:08X}",
             faulting_tid, fault_addr, fault_pc);

    return SCE_KERNEL_OK;
}

EXPORT(int, sceKernelWaitExceptionCBForMono, Ptr<uint32_t> pInfo, int type, int flags) {
    TRACY_FUNC(sceKernelWaitExceptionCBForMono, pInfo, type, flags);
    return CALL_EXPORT(sceKernelWaitExceptionForMono, pInfo, type, flags);
}