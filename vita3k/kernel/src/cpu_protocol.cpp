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

    // Debug: trace the pthread function that converts SceUID to exception table key.
    // NID 0x23D5CB94 is called by the Mono exception callback to look up the
    // faulting thread. Logging its input/output reveals what the search key is.
    bool trace_nid = (nid == 0x23D5CB94);
    uint32_t trace_r0_in = 0, trace_r1_in = 0;
    if (trace_nid) {
        trace_r0_in = read_reg(cpu, 0);
        trace_r1_in = read_reg(cpu, 1);
    }

    // TODO: just supply ThreadStatePtr to call_import
    // the only benefit of using thread_id instead--namely less locking-- has been gone for long
    call_import(cpu, nid, thread.id);

    if (trace_nid) {
        uint32_t trace_r0_out = read_reg(cpu, 0);
        // Read the output value written to [r1_in]
        uint32_t output_val = 0;
        if (trace_r1_in) {
            Ptr<uint32_t> out_ptr(trace_r1_in);
            if (out_ptr.valid(*mem)) {
                output_val = *out_ptr.get(*mem);
            }
        }
        LOG_WARN("NID 0x23D5CB94: r0_in=0x{:X} r1_in=0x{:X} → r0_out=0x{:X} output_val=0x{:X}",
                 trace_r0_in, trace_r1_in, trace_r0_out, output_val);
    }

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

    // One-time: patch callback invoker stubs created by mono-vita's module_start.
    // mono-vita creates ~200 fallback stubs ("mvn r0, #0; bx lr") at runtime for
    // functions normally provided by SceLibMonoBridge. Some of these are callback
    // INVOKERS that receive a function pointer in r0 and should CALL it. Without
    // patching, the exception callback is never invoked.
    // This must run AFTER module_start (the stubs don't exist at load time).
    if (!kernel->mono_callback_invokers_patched) {
        kernel->mono_callback_invokers_patched = true;
        Address code_start = kernel->mono_code_start;
        size_t code_size = kernel->mono_code_end - kernel->mono_code_start;
        uint32_t *code = Ptr<uint32_t>(code_start).get(*mem);
        if (code && code_size > 32) {
            size_t nwords = code_size / 4;
            int patched = 0;
            for (size_t i = 8; i < nwords; i++) {
                if (code[i] != 0xE12FFF3C) // blx ip
                    continue;
                uint32_t movw_val = 0, movt_val = 0;
                bool found_ldr = false;
                for (size_t j = (i >= 8 ? i - 8 : 0); j < i; j++) {
                    uint32_t w = code[j];
                    if ((w & 0xFFF0F000) == 0xE300C000)
                        movw_val = ((w >> 4) & 0xF000) | (w & 0xFFF);
                    if ((w & 0xFFF0F000) == 0xE340C000)
                        movt_val = ((w >> 4) & 0xF000) | (w & 0xFFF);
                    if ((w & 0xFFF0FFFF) == 0xE5900028)
                        found_ldr = true;
                }
                if (!found_ldr || movw_val == 0 || movt_val == 0)
                    continue;
                uint32_t stub_addr = (movt_val << 16) | movw_val;
                if (stub_addr < code_start || stub_addr >= code_start + code_size - 8)
                    continue;
                uint32_t stub_off = stub_addr - code_start;
                uint32_t *stub = &code[stub_off / 4];
                if (stub[0] == 0xE3E00000 && stub[1] == 0xE12FFF1E) {
                    stub[0] = 0xE52DE004; // push {lr}
                    stub[1] = 0xE12FFF30; // blx r0
                    stub[2] = 0xE49DF004; // pop {pc}
                    kernel->invalidate_jit_cache(code_start + stub_off, 12);
                    patched++;
                }
            }
            if (patched > 0)
                LOG_INFO("Mono: patched {} callback invoker stubs (lazy)", patched);
        }
    }

    // Don't signal for threads killed by double-fault (like real Vita)
    {
        std::lock_guard<std::mutex> lock(kernel->mono_exception_mutex);
        if (kernel->mono_exception_dead_threads.count(thread_id))
            return false;
    }

    SceUID sema = 0;
    {
        std::lock_guard<std::mutex> lock(kernel->mono_exception_mutex);

        // Only signal if no exception is already pending and handler is registered
        if (kernel->mono_exception_pending || kernel->mono_exception_handler_thread == 0
            || kernel->mono_exception_sema == 0) {
            kernel->mono_exception_blocked_count++;
            if (kernel->mono_exception_blocked_count <= 3 || (kernel->mono_exception_blocked_count % 10000) == 0) {
                LOG_WARN("signal_mono_exception BLOCKED (x{}): pending={}, handler={}, sema={}",
                         kernel->mono_exception_blocked_count, kernel->mono_exception_pending,
                         kernel->mono_exception_handler_thread, kernel->mono_exception_sema);
            }
            return false;
        }

        // Don't re-signal the same thread that was just processed.
        // The wait-retry loop can re-signal after the handler finishes,
        // but by then the thread's PC is stale (0x0). The original signal
        // had the correct PC. Skip re-signaling to preserve the correct context.
        if (kernel->mono_exception_last_thread == thread_id && fault_pc == 0) {
            return false;
        }

        kernel->mono_exception_blocked_count = 0;
        kernel->mono_exception_pending = true;
        kernel->mono_exception_thread_id = thread_id;
        kernel->mono_exception_fault_addr = fault_addr;
        kernel->mono_exception_fault_pc = fault_pc;
        kernel->mono_exception_last_thread = thread_id;
        sema = kernel->mono_exception_sema;
    }

    LOG_WARN("signal_mono_exception: thread {}, fault_pc=0x{:08X}", thread_id, fault_pc);

    // Use the LIVE context from jit->Regs().
    // Dynarmic ARM64 backend stores guest registers directly in JitState.regs[]
    // (via STR on every A32SetRegister IR op), so r0-r14 are accurate during
    // MemoryRead/MemoryReadCode callbacks. Only r15 (PC) is stale — it's set
    // at block exit, not per-instruction. We override it with fault_pc.
    //
    // Previous approach used pre_run_context (saved before run()) which was
    // potentially hundreds of basic blocks stale — that caused the re-fault
    // because the Mono callback received wrong r0/SP/LR values.
    auto faulting_thread = kernel->get_thread(thread_id);
    if (faulting_thread) {
        {
            std::lock_guard<std::mutex> lock(kernel->mono_exception_mutex);
            kernel->mono_exception_saved_context = save_context(*faulting_thread->cpu);
            kernel->mono_exception_saved_context.cpu_registers[15] = fault_pc;

            auto &ctx = kernel->mono_exception_saved_context;
            LOG_WARN("signal_mono_exception: saved LIVE context r0=0x{:08X} SP=0x{:08X} LR=0x{:08X}",
                     ctx.cpu_registers[0], ctx.cpu_registers[13], ctx.cpu_registers[14]);

            // Dump all registers for debugging vtable null entry
            LOG_WARN("  regs: r0={:08X} r1={:08X} r2={:08X} r3={:08X} r4={:08X} r5={:08X}",
                     ctx.cpu_registers[0], ctx.cpu_registers[1], ctx.cpu_registers[2],
                     ctx.cpu_registers[3], ctx.cpu_registers[4], ctx.cpu_registers[5]);
            LOG_WARN("  regs: r6={:08X} r7={:08X} r8={:08X} r9={:08X} r10={:08X} r11(fp)={:08X} r12={:08X}",
                     ctx.cpu_registers[6], ctx.cpu_registers[7], ctx.cpu_registers[8],
                     ctx.cpu_registers[9], ctx.cpu_registers[10], ctx.cpu_registers[11], ctx.cpu_registers[12]);

            // If fp (r11) is valid, dump the stack frame (the object and args)
            MemState &mstate = *this->mem;
            Address fp_addr = ctx.cpu_registers[11];
            if (fp_addr > 0x1000 && fp_addr < 0xF0000000) {
                Ptr<uint32_t> fp_ptr(fp_addr);
                if (fp_ptr.valid(mstate)) {
                    uint32_t *fp_data = fp_ptr.get(mstate);
                    LOG_WARN("  [fp+0]={:08X} [fp+4]={:08X} [fp+8]={:08X} [fp+C]={:08X}",
                             fp_data[0], fp_data[1], fp_data[2], fp_data[3]);

                    // fp+4 is the object pointer in the faulting code at 0x8236FE*
                    uint32_t obj_addr = fp_data[1];
                    if (obj_addr > 0x1000 && obj_addr < 0xF0000000) {
                        Ptr<uint32_t> obj_ptr(obj_addr);
                        if (obj_ptr.valid(mstate)) {
                            uint32_t *obj_data = obj_ptr.get(mstate);
                            LOG_WARN("  object@{:08X}: [{:08X} {:08X} {:08X} {:08X}]",
                                     obj_addr, obj_data[0], obj_data[1], obj_data[2], obj_data[3]);

                            // obj_data[0] is the vtable pointer
                            uint32_t vtable_addr = obj_data[0];
                            if (vtable_addr > 0x1000 && vtable_addr < 0xF0000000) {
                                Ptr<uint32_t> vt_ptr(vtable_addr);
                                if (vt_ptr.valid(mstate)) {
                                    uint32_t *vt = vt_ptr.get(mstate);
                                    LOG_WARN("  vtable@{:08X}: [0]={:08X} [4]={:08X} [8]={:08X} [C]={:08X}",
                                             vtable_addr, vt[0], vt[1], vt[2], vt[3]);
                                    LOG_WARN("  vtable: [10]={:08X} [14]={:08X} [18]={:08X} [1C]={:08X}",
                                             vt[4], vt[5], vt[6], vt[7]);
                                    LOG_WARN("  vtable: [20]={:08X} [24]={:08X} [28]={:08X} [2C]={:08X}",
                                             vt[8], vt[9], vt[10], vt[11]);
                                    LOG_WARN("  vtable: [30]={:08X} [34]={:08X} [38]={:08X} [3C]={:08X}",
                                             vt[12], vt[13], vt[14], vt[15]);
                                }
                            }
                        }
                    }
                }
            }
        }
        faulting_thread->suspend();
    }

    // Wake up the ExceptionHandlerThread by signaling the semaphore
    semaphore_signal(*kernel, "signal_mono_exception", thread_id, sema, 1);

    return true;
}