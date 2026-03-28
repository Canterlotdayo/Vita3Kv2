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

#include <kernel/cpu_protocol.h>

#include <cpu/functions.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <util/log.h>

CPUProtocol::CPUProtocol(KernelState &kernel, MemState &mem, const CallImportFunc &func)
    : call_import(func)
    , kernel(&kernel)
    , mem(&mem) {
}

void CPUProtocol::call_svc(CPUState &cpu, uint32_t svc, Address pc, ThreadState &thread) {
    // Handle trampoline
    // 1. Handle trampoline jumper
    // to save the space we use interrupt to implement jumper
    // as thumb instructions require total three instructions to jump to any pc without limitations
    if (svc == TRAMPOLINE_JUMPER_SVC) {
        // find thumb16 trampoline
        Trampoline *tr = kernel->debugger.get_trampoline(pc - 2);
        // find thumb32 or arm trampoline
        if (!tr)
            tr = kernel->debugger.get_trampoline(pc - 4);
        write_pc(cpu, tr->trampoline_addr);
        return;
    }

    // 2. Call trampoline callback
    // this interrupt is made inside trampoline body
    if (svc == TRAMPOLINE_HANDLER_SVC) {
        Trampoline *tr = *Ptr<Trampoline *>(pc).get(*mem);
        tr->callback(cpu, *mem, tr->lr);
        return;
    }

    // This is usual service call
    uint32_t nid = *Ptr<uint32_t>(pc + 4).get(*mem);
    // TODO: just supply ThreadStatePtr to call_import
    // the only benefit of using thread_id instead--namely less locking-- has been gone for long
    call_import(cpu, nid, thread.id);

    // ARM recommends clearing exclusive state inside interrupt handler
    clear_exclusive(kernel->exclusive_monitor, get_processor_id(cpu));
}

Address CPUProtocol::get_watch_memory_addr(Address addr) {
    return kernel->debugger.get_watch_memory_addr(addr);
}

ExclusiveMonitorPtr CPUProtocol::get_exclusive_monitor() {
    return kernel->exclusive_monitor;
}

bool CPUProtocol::signal_mono_exception(int thread_id, Address fault_addr, Address fault_pc) {
    // Only signal if Mono is loaded
    if (kernel->mono_code_start == 0)
        return false;

    SceUID sema = 0;
    {
        std::lock_guard<std::mutex> lock(kernel->mono_exception_mutex);

        // Only signal if no exception is already pending and handler is registered
        if (kernel->mono_exception_pending || kernel->mono_exception_handler_thread == 0
            || kernel->mono_exception_sema == 0) {
            LOG_WARN("signal_mono_exception BLOCKED: pending={}, handler={}, sema={}",
                     kernel->mono_exception_pending, kernel->mono_exception_handler_thread, kernel->mono_exception_sema);
            return false;
        }

        kernel->mono_exception_pending = true;
        kernel->mono_exception_thread_id = thread_id;
        kernel->mono_exception_fault_addr = fault_addr;
        kernel->mono_exception_fault_pc = fault_pc;
        sema = kernel->mono_exception_sema;
    }

    LOG_WARN("signal_mono_exception: thread {}, fault_pc=0x{:08X}", thread_id, fault_pc);

    // Use the pre-run context (saved before run() in the run_loop).
    // This is a clean snapshot from before the faulting JIT block executed.
    // The live context is corrupted because Dynarmic partially executed the block
    // (NOPs, zero reads) before the callback could stop it.
    auto faulting_thread = kernel->get_thread(thread_id);
    if (faulting_thread) {
        {
            std::lock_guard<std::mutex> lock(kernel->mono_exception_mutex);
            kernel->mono_exception_saved_context = faulting_thread->cpu->pre_run_context;
            // Override PC with the actual fault PC
            kernel->mono_exception_saved_context.cpu_registers[15] = fault_pc;
        }
        faulting_thread->suspend();
    }

    // Wake up the ExceptionHandlerThread by signaling the semaphore
    semaphore_signal(*kernel, "signal_mono_exception", thread_id, sema, 1);

    return true;
}