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

EXPORT(int, sceKernelWaitExceptionCBForMono) {
    TRACY_FUNC(sceKernelWaitExceptionCBForMono);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelWaitExceptionForMono) {
    TRACY_FUNC(sceKernelWaitExceptionForMono);
    
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (thread && thread->cpu) {
        // Dump full CPU context at the time of call to understand what the
        // caller expects. The LR tells us where to return, and the stack
        // contains the calling function's saved state.
        uint32_t lr = read_lr(*thread->cpu);
        uint32_t sp = read_sp(*thread->cpu);
        uint32_t pc = read_pc(*thread->cpu);
        
        LOG_WARN("=== sceKernelWaitExceptionForMono called ===");
        LOG_WARN("  Thread: {} (ID: {})", thread->name, thread_id);
        LOG_WARN("  PC=0x{:08X}  LR=0x{:08X}  SP=0x{:08X}", pc, lr, sp);
        for (int i = 0; i < 13; i++) {
            LOG_WARN("  r{}=0x{:08X}", i, read_reg(*thread->cpu, i));
        }
        
        // Dump stack (first 64 words = 256 bytes)
        LOG_WARN("  Stack dump:");
        for (int i = 0; i < 64; i++) {
            uint32_t addr = sp + i * 4;
            Ptr<uint32_t> ptr(addr);
            if (ptr.valid(emuenv.mem)) {
                uint32_t val = *ptr.get(emuenv.mem);
                // Mark values that look like code addresses
                const char *note = "";
                if (val >= 0x84CF4000 && val < 0x84F48000) note = " <- mono code";
                else if (val >= 0x80010000 && val < 0x83CB855C) note = " <- eboot";
                else if (val >= 0x84518000 && val < 0x84570000) note = " <- libc";
                LOG_WARN("  [SP+0x{:03X}] = 0x{:08X}{}", i * 4, val, note);
            }
        }
        LOG_WARN("=== end WaitExceptionForMono context ===");
    }
    
    STUBBED("Infinite wait - Mono exception handler will not function");
    thread->suspend();
    return 0;
}