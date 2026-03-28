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

    // If the exception callback failed (r0=0), don't resume the faulting thread.
    // On real Vita, the double-fault kills the thread. We emulate this by
    // leaving it suspended forever.
    if (emuenv.kernel.mono_exception_skip_resume) {
        LOG_WARN("sceKernelResumeThreadForMono: NOT resuming thread {} (dead by double-fault)", threadId);
        emuenv.kernel.mono_exception_skip_resume = false;
        return SCE_KERNEL_OK;
    }

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
    {
        std::lock_guard<std::mutex> lock(emuenv.kernel.mono_exception_mutex);
        faulting_tid = emuenv.kernel.mono_exception_thread_id;
        fault_addr = emuenv.kernel.mono_exception_fault_addr;
        fault_pc = emuenv.kernel.mono_exception_fault_pc;
        emuenv.kernel.mono_exception_pending = false;
    }

    // Wait for the faulting thread to actually reach suspend state
    auto faulting_thread = emuenv.kernel.get_thread(faulting_tid);
    if (faulting_thread) {
        for (int i = 0; i < 1000; i++) {
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
    }

    // Write exception info into the output structure.
    // The structure layout (from Ghidra disassembly):
    //   +0x00: size (set by caller to 0x18 = 24 bytes)
    //   +0x04: faulting thread ID
    //   +0x08: fault address
    //   +0x0C: fault PC
    //   +0x10: exception type
    //   +0x14: reserved
    if (pInfo) {
        uint32_t *info = pInfo.get(emuenv.mem);
        // info[0] = size, already set by caller (0x18)
        info[1] = static_cast<uint32_t>(faulting_tid);
        info[2] = fault_addr;
        info[3] = fault_pc;
        info[4] = 0x101;  // exception type (same as the type parameter)
        info[5] = 0;
    }

    // Ensure the faulting thread is in the Mono exception table so the
    // callback (FUN_84DC94FC) can find it by pthread_t. Without this,
    // the callback returns r0=0 → thread killed → game freezes.
    // We register HERE (not at create_thread) because:
    // - Mono's counter is initialized by now
    // - The thread has a valid pthread_t
    // - We can check for duplicates
    if (emuenv.kernel.mono_data_start != 0 && faulting_thread) {
        constexpr uint32_t EXCEPTION_TABLE_OFFSET = 0x66A10;
        constexpr uint32_t EXCEPTION_COUNTER_OFFSET = 0x4F34;

        // Get the faulting thread's pthread_t from its name (hex prefix)
        uint32_t pthread_id = static_cast<uint32_t>(faulting_tid);
        const std::string &tname = faulting_thread->name;
        if (!tname.empty()) {
            char *end = nullptr;
            unsigned long parsed = strtoul(tname.c_str(), &end, 16);
            if (end != tname.c_str() && *end == ' ' && parsed > 0x80000000) {
                pthread_id = static_cast<uint32_t>(parsed);
            }
        }

        Address table_addr = emuenv.kernel.mono_data_start + EXCEPTION_TABLE_OFFSET;
        Address counter_addr = emuenv.kernel.mono_data_start + EXCEPTION_COUNTER_OFFSET;
        uint32_t *counter = Ptr<uint32_t>(counter_addr).get(emuenv.mem);
        uint32_t count = *counter;

        // Check if already registered
        bool found = false;
        if (count < 256) {
            uint32_t *table = Ptr<uint32_t>(table_addr).get(emuenv.mem);
            for (uint32_t i = 0; i < count && !found; i++) {
                if (table[i]) {
                    uint32_t *entry = Ptr<uint32_t>(table[i]).get(emuenv.mem);
                    if (entry && entry[0] == pthread_id) {
                        found = true;
                    }
                }
            }
        }

        if (!found && count < 256) {
            Address exc_entry = alloc(emuenv.mem, 8, "GC_exc_entry");
            if (exc_entry) {
                uint32_t *exc = Ptr<uint32_t>(exc_entry).get(emuenv.mem);
                exc[0] = pthread_id;
                exc[1] = 0;

                uint32_t *table = Ptr<uint32_t>(table_addr).get(emuenv.mem);
                table[count] = exc_entry;
                *counter = count + 1;

                LOG_WARN("Mono exception table: registered faulting thread '{}' (pthread_t=0x{:08X}) at idx={}",
                         tname, pthread_id, count);
            }
        }
    }

    LOG_WARN("sceKernelWaitExceptionForMono: woke up! Faulting thread ID: {}, addr: 0x{:08X}, PC: 0x{:08X}",
             faulting_tid, fault_addr, fault_pc);

    return SCE_KERNEL_OK;
}

EXPORT(int, sceKernelWaitExceptionCBForMono, int type, Ptr<uint32_t> pInfo, int flags) {
    TRACY_FUNC(sceKernelWaitExceptionCBForMono, type, pInfo, flags);
    return CALL_EXPORT(sceKernelWaitExceptionForMono, type, pInfo, flags);
}