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

EXPORT(int, sceKernelWaitExceptionForMono, int type, Ptr<uint32_t> pInfo, int flags) {
    TRACY_FUNC(sceKernelWaitExceptionForMono, type, pInfo, flags);

    LOG_INFO("sceKernelWaitExceptionForMono: ExceptionHandlerThread (ID: {}) waiting for exceptions...", thread_id);

    // Check if the PREVIOUS faulting thread is still suspended.
    // If so, Mono's callback didn't handle the exception (no SetContext/Resume).
    // On real Vita, an unhandled exception terminates the thread.
    // Here, we resume the thread so it returns from the null call with r0=0
    // (the NOP fallback from MemoryReadCode). This prevents the thread from
    // being suspended forever and causing a deadlock.
    {
        std::lock_guard<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
        SceUID prev_tid = emuenv.kernel.mono_exception_thread_id;
        if (prev_tid != 0) {
            auto prev_thread = emuenv.kernel.get_thread(prev_tid);
            if (prev_thread && prev_thread->cpu) {
                // Check if the previous faulting thread is stuck at PC=0
                // (Mono's callback didn't call SetThreadContext to fix the PC).
                // This happens when the exception is in eboot code, not JIT code —
                // Mono can't find JIT metadata and skips handling.
                // Fix: set PC=LR to return from the null function call with r0=0.
                Address pc = read_pc(*prev_thread->cpu);
                if (pc < emuenv.mem.page_size) {
                    Address lr = read_lr(*prev_thread->cpu);
                    LOG_WARN("WaitExceptionForMono: thread {} stuck at PC=0x{:08X} (unhandled) — setting PC=LR=0x{:08X}, r0=0",
                             prev_tid, pc, lr);
                    write_pc(*prev_thread->cpu, lr);
                    write_reg(*prev_thread->cpu, 0, 0); // r0 = 0 (null return)
                }

                // Also force-resume if still suspended
                bool needs_resume = false;
                {
                    std::lock_guard<std::mutex> tlock(prev_thread->mutex);
                    needs_resume = (prev_thread->status == ThreadStatus::suspend);
                }
                if (needs_resume) {
                    LOG_WARN("WaitExceptionForMono: previous faulting thread {} still suspended — forcing resume", prev_tid);
                    prev_thread->resume();
                }
            }
            emuenv.kernel.mono_exception_thread_id = 0;
        }

        emuenv.kernel.mono_exception_handler_thread = thread_id;

        if (emuenv.kernel.mono_exception_sema == 0) {
            emuenv.kernel.mono_exception_sema = semaphore_create(emuenv.kernel, export_name,
                "MonoExceptionSema", thread_id, 0, 0, 1);
            LOG_INFO("Created Mono exception semaphore: {}", emuenv.kernel.mono_exception_sema);
        }
    }

    SceUID sema = emuenv.kernel.mono_exception_sema;

wait_again:
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

    // Spurious wakeup or stale data — thread ID 0 is invalid.
    // Mono would call psp2_get_thread_jit(0) → "thread not found" → assertion → handler dies.
    // Skip processing and re-enter the wait loop.
    if (faulting_tid == 0) {
        LOG_WARN("WaitExceptionForMono: spurious wakeup (faulting_tid=0), re-entering wait");
        goto wait_again;
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
            Address lr = emuenv.kernel.mono_exception_saved_context.cpu_registers[14];

            // Determine the best PC to put in the saved context.
            // Mono uses this PC (via GetThreadContextForVM) AND info[3] (in pInfo)
            // to look up JIT metadata and find the C# catch handler.
            // The PC MUST be a valid code address in a loaded module or JIT region,
            // otherwise Mono can't find metadata and skips handling.
            //
            // For PREFETCH abort (null function pointer call):
            //   fault_addr = target that failed to fetch (e.g. 0x0)
            //   fault_pc = LR from MemoryReadCode = caller's return address
            //   Use fault_pc (LR) — it points to the caller which has JIT metadata.
            //
            // For DATA abort (null dereference read/write):
            //   fault_addr = address that was read/written
            //   fault_pc = stale get_pc() from MemoryRead callback (often garbage)
            //   committed_pc = PC after run() returned (may or may not be valid)
            //   Use committed_pc if valid, else fault_pc if valid, else LR as fallback.

            Address best_pc;
            if (is_prefetch) {
                // PREFETCH abort: null function pointer call.
                // The pattern is: "mov lr, pc; ldr pc, [rN, #offset]"
                // "mov lr, pc" sets LR = address_of_next_instruction (the call site).
                // "ldr pc, [rN]" loads PC from a null vtable entry → PC=0 → fault.
                //
                // fault_pc = stale LR from get_lr() during MemoryReadCode callback.
                //   Dynarmic hasn't committed the "mov lr, pc" yet → wrong value.
                // committed LR (from save_context after run()) = the CORRECT LR
                //   set by "mov lr, pc" → points to the exact call site.
                //
                // Mono uses this PC to find JIT metadata for the calling method.
                // The call site address is what Mono needs to locate the catch handler.
                best_pc = lr; // = committed LR from save_context (accurate after run())
            } else {
                if (committed_pc >= 0x80000000 && committed_pc < 0x90000000) {
                    best_pc = committed_pc;
                } else if (fault_pc >= 0x80000000 && fault_pc < 0x90000000) {
                    best_pc = fault_pc;
                } else if (lr >= 0x80000000 && lr < 0x90000000) {
                    best_pc = lr;
                } else {
                    best_pc = committed_pc; // last resort
                }
            }

            emuenv.kernel.mono_exception_saved_context.cpu_registers[15] = best_pc;
            fault_pc = best_pc; // update for pInfo below

            auto &ctx = emuenv.kernel.mono_exception_saved_context;
            LOG_WARN("WaitExceptionForMono: thread {} committed_pc=0x{:08X} fault_pc=0x{:08X} final_pc=0x{:08X} SP=0x{:08X} LR=0x{:08X} type={}",
                     faulting_tid, committed_pc, fault_pc, best_pc, ctx.cpu_registers[13], ctx.cpu_registers[14],
                     is_prefetch ? "PREFETCH" : "DATA");
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

EXPORT(int, sceKernelWaitExceptionCBForMono, int type, Ptr<uint32_t> pInfo, int flags) {
    TRACY_FUNC(sceKernelWaitExceptionCBForMono, type, pInfo, flags);
    return CALL_EXPORT(sceKernelWaitExceptionForMono, type, pInfo, flags);
}