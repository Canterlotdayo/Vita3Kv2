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

#include "cpu/common.h"
#include <cpu/impl/dynarmic_cpu.h>
#include <cpu/state.h>
#include <util/bit_cast.h>
#include <util/log.h>

#include <mem/ptr.h>

#include <dynarmic/frontend/A32/a32_ir_emitter.h>
#include <dynarmic/interface/A32/coprocessor.h>
#include <dynarmic/interface/exclusive_monitor.h>

#include <memory>
#include <chrono>
#include <thread>
#include <optional>
#include <string>

class ArmDynarmicCP15 : public Dynarmic::A32::Coprocessor {
    uint32_t tpidruro;

public:
    using CoprocReg = Dynarmic::A32::CoprocReg;

    explicit ArmDynarmicCP15()
        : tpidruro(0) {
    }

    ~ArmDynarmicCP15() override = default;

    std::optional<Callback> CompileInternalOperation(bool two, unsigned opc1, CoprocReg CRd,
        CoprocReg CRn, CoprocReg CRm,
        unsigned opc2) override {
        return std::nullopt;
    }

    CallbackOrAccessOneWord CompileSendOneWord(bool two, unsigned opc1, CoprocReg CRn,
        CoprocReg CRm, unsigned opc2) override {
        return CallbackOrAccessOneWord{};
    }

    CallbackOrAccessTwoWords CompileSendTwoWords(bool two, unsigned opc, CoprocReg CRm) override {
        return CallbackOrAccessTwoWords{};
    }

    CallbackOrAccessOneWord CompileGetOneWord(bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm,
        unsigned opc2) override {
        if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc1 == 0 && opc2 == 3) {
            return &tpidruro;
        }

        return CallbackOrAccessOneWord{};
    }

    CallbackOrAccessTwoWords CompileGetTwoWords(bool two, unsigned opc, CoprocReg CRm) override {
        return CallbackOrAccessTwoWords{};
    }

    std::optional<Callback> CompileLoadWords(bool two, bool long_transfer, CoprocReg CRd,
        std::optional<std::uint8_t> option) override {
        return std::nullopt;
    }

    std::optional<Callback> CompileStoreWords(bool two, bool long_transfer, CoprocReg CRd,
        std::optional<std::uint8_t> option) override {
        return std::nullopt;
    }

    void set_tpidruro(uint32_t tpidruro) {
        this->tpidruro = tpidruro;
    }

    uint32_t get_tpidruro() const {
        return tpidruro;
    }
};

class ArmDynarmicCallback : public Dynarmic::A32::UserCallbacks {
    friend class DynarmicCPU;

    CPUState *parent;
    DynarmicCPU *cpu;

public:
    explicit ArmDynarmicCallback(CPUState &parent, DynarmicCPU &cpu)
        : parent(&parent)
        , cpu(&cpu) {}

    ~ArmDynarmicCallback() override = default;

    std::optional<std::uint32_t> MemoryReadCode(Dynarmic::A32::VAddr addr) override {
        if (cpu->log_mem)
            LOG_TRACE("Instruction fetch at address 0x{:X}", addr);

        // Handle null function pointer calls (PC in first page = null pointer deref)
        if (addr < parent->mem->page_size) {
            auto lr = cpu->get_lr();

            // If we already signaled a Mono exception for this thread during this
            // run() call, just return NOP. Don't redirect PC — the exception handler
            // will set the correct PC via SetThreadContextForMono after we're suspended.
            // HaltExecution was already called; we're just waiting for Dynarmic to
            // finish the current basic block and return from run().
            if (mono_exception_signaled) {
                return 0xE320F000;
            }

            // Try to signal the Mono exception handler. If Mono is loaded and
            // the handler thread is waiting, this suspends the current thread
            // and wakes the handler. Mono will modify our CPU context to jump
            // to the C# exception handler and resume us.
            if (parent->protocol &&
                parent->protocol->signal_mono_exception(parent->thread_id, addr, lr)) {
                mono_exception_signaled = true;
                cpu->jit->HaltExecution();
                return 0xE320F000;
            }

            // If Mono is loaded but signal was BLOCKED (another exception pending),
            // just return NOP and let the thread continue. Don't halt — that causes
            // a busy-loop in run_loop. The fallback path below (PC=LR, r0=0) will
            // handle it: the null call returns 0 to the caller, and the C# code
            // handles it via its own null checks or re-faults later when the
            // handler is free.
            if (parent->protocol) {
                // Fall through to the PC=LR fallback below
            }

            // Fallback for non-Mono games or when handler isn't ready:
            // return to LR with r0=0, throttle logging
            if (lr == last_null_caller_lr) {
                null_call_count++;
            } else {
                last_null_caller_lr = lr;
                null_call_count = 1;
            }
            if (null_call_count <= 4) {
                LOG_WARN("Null function pointer call (PC=0x{:X}, LR=0x{:X}) - returning to caller", addr, lr);
            }

            cpu->jit->Regs()[0] = 0;
            cpu->set_pc(lr);
            cpu->jit->HaltExecution();
            return 0xE320F000;
        }

        return MemoryRead32(addr);
    }

    // Track null function pointer call loops (fallback only)
    Dynarmic::A32::VAddr last_null_caller_lr = 0;
    uint32_t null_call_count = 0;

    // Set when signal_mono_exception succeeds during a run() call.
    // Prevents the fallback path from redirecting PC on subsequent
    // MemoryReadCode callbacks in the same basic block.
    bool mono_exception_signaled = false;
    int mono_suspend_read_count = 0;

    static void TraceInstruction(uint64_t self_, uint64_t address, uint64_t is_thumb) {
        ArmDynarmicCallback &self = *reinterpret_cast<ArmDynarmicCallback *>(self_);

        std::string disassembly = [&]() -> std::string {
            if (!address || !Ptr<uint32_t>{ (uint32_t)address }.valid(*self.parent->mem)) {
                return "invalid address";
            }
            return disassemble(*self.parent, address);
        }();
        LOG_TRACE("{} ({}): {} {}", log_hex(self_), self.parent->thread_id, log_hex(address), disassembly);
    }

    void PreCodeTranslationHook(bool is_thumb, Dynarmic::A32::VAddr pc, Dynarmic::A32::IREmitter &ir) override {
        if (cpu->log_code) {
            ir.CallHostFunction(&TraceInstruction, ir.Imm64((uint64_t)this), ir.Imm64(pc), ir.Imm64(is_thumb));
        }
    }

    template <typename T>
    T MemoryRead(Dynarmic::A32::VAddr addr) {
        Ptr<T> ptr{ addr };
        if (!ptr || !ptr.valid(*parent->mem) || ptr.address() < parent->mem->page_size) {
            // If a Mono exception was already signaled, count invalid reads.
            // After the first exception is handled and the thread resumes,
            // it may re-fault. The flag stays true from the JIT block finishing.
            // After enough invalid reads, reset and try to signal again.
            if (mono_exception_signaled) {
                mono_suspend_read_count++;
                if (mono_suspend_read_count > 64) {
                    // Thread was resumed but re-faulted. Try to re-signal,
                    // but only if the handler has finished processing the
                    // previous exception (pending == false).
                    auto pc = this->cpu->get_pc();
                    mono_suspend_read_count = 0;
                    
                    // Check if handler is ready for a new signal
                    bool handler_ready = false;
                    if (parent->protocol) {
                        // signal_mono_exception checks pending internally —
                        // if pending is true, it returns false (BLOCKED).
                        // We just try and reset only on success.
                        mono_exception_signaled = false;
                        if (parent->protocol->signal_mono_exception(parent->thread_id, addr, pc)) {
                            LOG_WARN("Re-signaling Mono exception for thread (PC=0x{:X}, addr=0x{:X})", pc, addr);
                            mono_exception_signaled = true;
                            cpu->jit->HaltExecution();
                            return 0;
                        }
                        // Handler not ready yet — keep waiting
                        mono_exception_signaled = true;
                    }
                }
                return 0;
            }

            auto pc = this->cpu->get_pc();

            // Null pointer data read (addr in first page) — signal Mono exception.
            // But don't re-signal if already signaled in this run().
            if (addr < parent->mem->page_size && parent->protocol && !mono_exception_signaled) {
                auto lr = cpu->get_lr();
                if (parent->protocol->signal_mono_exception(parent->thread_id, addr, pc)) {
                    mono_exception_signaled = true;
                    cpu->jit->HaltExecution();
                    return 0;
                }
                // Signal BLOCKED — don't halt (causes busy-loop in run_loop).
                // Just return 0: the null read returns a zero value to the guest,
                // which will either be handled by C# null checks or cause another
                // fault later when the exception handler is free.
                return 0;
            }

            // If the PC itself is in invalid/unmapped memory, halt immediately.
            // But don't re-signal if a Mono exception was already signaled
            // in this run() — the first signal has the correct fault data.
            {
                Ptr<uint32_t> pc_check{ static_cast<uint32_t>(pc) };
                if (pc && !pc_check.valid(*parent->mem)) {
                    if (!mono_exception_signaled) {
                        if (parent->protocol &&
                            parent->protocol->signal_mono_exception(parent->thread_id, addr, pc)) {
                            LOG_WARN("Thread at unmapped PC=0x{:X} — signaled Mono exception handler", pc);
                            mono_exception_signaled = true;
                            cpu->jit->HaltExecution();
                            return 0;
                        }
                        LOG_WARN("Thread executing in unmapped memory (PC=0x{:X}) - halting", pc);
                    }
                    cpu->jit->HaltExecution();
                    return 0;
                }
            }

            // Detect infinite loop: if the same PC keeps reading invalid addresses,
            // the thread is stuck in a loop reading from a null object (e.g., iterating
            // a null IEnumerator's vtable). After enough attempts, try to signal the
            // Mono exception handler (which can redirect to a C# catch handler).
            // If Mono isn't loaded or the handler isn't ready, fall back to the
            // stack escape mechanism.
            if (pc == last_invalid_read_pc) {
                invalid_read_count++;
            } else {
                last_invalid_read_pc = pc;
                invalid_read_count = 1;
            }

            if (invalid_read_count <= 4) {
                LOG_ERROR("Invalid read of uint{}_t at address: 0x{:x}\n{}", sizeof(T) * 8, addr, this->cpu->save_context().description());
                if (pc < parent->mem->page_size)
                    LOG_CRITICAL("PC is 0x{:x}", pc);
                else
                    LOG_ERROR("Executing: {}", disassemble(*parent, pc, nullptr));
            }

            if (invalid_read_count > 8) {
                // Try to signal Mono exception handler first — it can properly
                // redirect execution to a C# catch block.
                if (parent->protocol &&
                    parent->protocol->signal_mono_exception(parent->thread_id, addr, pc)) {
                    LOG_WARN("Invalid read loop at PC=0x{:X} — signaled Mono exception handler", pc);
                    mono_exception_signaled = true;
                    cpu->jit->HaltExecution();
                    invalid_read_count = 0;
                    last_invalid_read_pc = 0;
                    return 0;
                }

                // Fallback: scan the stack for a return address and force a return.
                auto sp = cpu->jit->Regs()[13];
                for (int i = 0; i < 32; i++) {
                    uint32_t stack_val_addr = sp + i * 4;
                    Ptr<uint32_t> sptr(stack_val_addr);
                    if (!sptr.valid(*parent->mem))
                        break;
                    uint32_t val = *sptr.get(*parent->mem);
                    if (val > parent->mem->page_size && val < 0x90000000 && val != pc) {
                        LOG_WARN("Invalid read loop at PC=0x{:X} (count={}). "
                                 "Escaping to 0x{:X} via stack[SP+0x{:X}]",
                                 pc, invalid_read_count, val, i * 4);
                        cpu->jit->Regs()[0] = 0; // return 0
                        cpu->jit->Regs()[13] = stack_val_addr + 4; // pop stack
                        cpu->set_pc(val);
                        cpu->jit->HaltExecution();
                        invalid_read_count = 0;
                        last_invalid_read_pc = 0;
                        return 0;
                    }
                }
                // No escape found, just throttle the logging
                if ((invalid_read_count & 0xFFF) == 0) {
                    LOG_WARN("Invalid read loop at PC=0x{:X}, count={}, no escape found", pc, invalid_read_count);
                }
            }

            return 0;
        }

        T ret = *ptr.get(*parent->mem);
        if (cpu->log_mem) {
            LOG_TRACE("Read uint{}_t at address: 0x{:x}, val = 0x{:x}", sizeof(T) * 8, addr, ret);
        }
        return ret;
    }

    // Track invalid read loops
    Dynarmic::A32::VAddr last_invalid_read_pc = 0;
    uint32_t invalid_read_count = 0;

    uint8_t MemoryRead8(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint8_t>(addr);
    }

    uint16_t MemoryRead16(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint16_t>(addr);
    }

    uint32_t MemoryRead32(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint32_t>(addr);
    }

    uint64_t MemoryRead64(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint64_t>(addr);
    }

    template <typename T>
    void MemoryWrite(Dynarmic::A32::VAddr addr, T value) {
        Ptr<T> ptr{ addr };
        if (!ptr || !ptr.valid(*parent->mem) || ptr.address() < parent->mem->page_size) {
            LOG_ERROR("Invalid write of uint{}_t at addr: 0x{:x}, val = 0x{:x}\n{}", sizeof(T) * 8, addr, value, this->cpu->save_context().description());

            auto pc = this->cpu->get_pc();
            if (pc < parent->mem->page_size)
                LOG_CRITICAL("PC is 0x{:x}", pc);
            else
                LOG_ERROR("Executing: {}", disassemble(*parent, pc, nullptr));

            // On real Vita, an invalid write triggers a DATA ABORT → the kernel
            // signals the exception handler → thread is suspended/killed.
            // Without this, the thread continues with corrupted state and
            // writes garbage to shared heap, causing cascading crashes.
            if (parent->protocol && !mono_exception_signaled) {
                auto lr = cpu->get_lr();
                if (parent->protocol->signal_mono_exception(parent->thread_id, addr, pc)) {
                    mono_exception_signaled = true;
                    cpu->jit->HaltExecution();
                    return;
                }
                // Signal BLOCKED — don't halt, just drop the write silently.
            }
            return;
        }

        *ptr.get(*parent->mem) = value;
        if (cpu->log_mem) {
            LOG_TRACE("Write uint{}_t at addr: 0x{:x}, val = 0x{:x}", sizeof(T) * 8, addr, value);
        }
    }

    void MemoryWrite8(Dynarmic::A32::VAddr addr, uint8_t value) override {
        MemoryWrite<uint8_t>(addr, value);
    }

    void MemoryWrite16(Dynarmic::A32::VAddr addr, uint16_t value) override {
        MemoryWrite<uint16_t>(addr, value);
    }

    void MemoryWrite32(Dynarmic::A32::VAddr addr, uint32_t value) override {
        MemoryWrite<uint32_t>(addr, value);
    }

    void MemoryWrite64(Dynarmic::A32::VAddr addr, uint64_t value) override {
        MemoryWrite<uint64_t>(addr, value);
    }

    template <typename T>
    bool MemoryWriteExclusive(Dynarmic::A32::VAddr addr, T value, T expected) {
        Ptr<T> ptr{ addr };
        if (!ptr || !ptr.valid(*parent->mem) || ptr.address() < parent->mem->page_size) {
            LOG_ERROR("Invalid exclusive write of uint{}_t at addr: 0x{:x}, val = 0x{:x}, expected = 0x{:x}\n{}", sizeof(T) * 8, addr, value, expected, this->cpu->save_context().description());

            auto pc = this->cpu->get_pc();
            if (pc < parent->mem->page_size)
                LOG_CRITICAL("PC is 0x{:x}", pc);
            else
                LOG_ERROR("Executing: {}", disassemble(*parent, pc, nullptr));
            return false;
        }

        auto result = Ptr<T>(addr).atomic_compare_and_swap(*parent->mem, value, expected);
        if (cpu->log_mem) {
            LOG_TRACE("Write uint{}_t at addr: 0x{:x}, val = 0x{:x}, expected = 0x{:x}", sizeof(T) * 8, addr, value, expected);
        }
        return result;
    }

    bool MemoryWriteExclusive8(Dynarmic::A32::VAddr addr, uint8_t value, uint8_t expected) override {
        return MemoryWriteExclusive(addr, value, expected);
    }

    bool MemoryWriteExclusive16(Dynarmic::A32::VAddr addr, uint16_t value, uint16_t expected) override {
        return MemoryWriteExclusive(addr, value, expected);
    }

    bool MemoryWriteExclusive32(Dynarmic::A32::VAddr addr, uint32_t value, uint32_t expected) override {
        return MemoryWriteExclusive(addr, value, expected);
    }

    bool MemoryWriteExclusive64(Dynarmic::A32::VAddr addr, uint64_t value, uint64_t expected) override {
        return MemoryWriteExclusive(addr, value, expected); // Ptr<uint64_t>(addr).atomic_compare_and_swap(*parent->mem, value, expected);
    }

    void InterpreterFallback(Dynarmic::A32::VAddr addr, size_t num_insts) override {
        LOG_ERROR("Unimplemented instruction at address {}:\n{}", log_hex(addr), save_context(*parent).description());
    }

    void ExceptionRaised(uint32_t pc, Dynarmic::A32::Exception exception) override {
        switch (exception) {
        case Dynarmic::A32::Exception::Breakpoint: {
            cpu->break_ = true;
            cpu->jit->HaltExecution();
            if (cpu->is_thumb_mode())
                cpu->set_pc(pc | 1);
            else
                cpu->set_pc(pc);
            break;
        }
        case Dynarmic::A32::Exception::WaitForInterrupt: {
            cpu->halted = true;
            cpu->jit->HaltExecution();
            break;
        }
        case Dynarmic::A32::Exception::PreloadDataWithIntentToWrite:
        case Dynarmic::A32::Exception::PreloadData:
        case Dynarmic::A32::Exception::PreloadInstruction:
        case Dynarmic::A32::Exception::SendEvent:
        case Dynarmic::A32::Exception::SendEventLocal:
        case Dynarmic::A32::Exception::WaitForEvent:
            break;
        case Dynarmic::A32::Exception::Yield:
            break;
        case Dynarmic::A32::Exception::UndefinedInstruction:
            LOG_WARN("Undefined instruction at address 0x{:X}, instruction 0x{:X} ({})", pc, MemoryReadCode(pc).value(), disassemble(*parent, pc, nullptr));
            InterpreterFallback(pc, 1);
            break;
        case Dynarmic::A32::Exception::UnpredictableInstruction:
            LOG_WARN("Unpredictable instruction at address 0x{:X}, instruction 0x{:X} ({})", pc, MemoryReadCode(pc).value(), disassemble(*parent, pc, nullptr));
            InterpreterFallback(pc, 1);
            break;
        case Dynarmic::A32::Exception::DecodeError: {
            LOG_WARN("Decode error at address 0x{:X}, instruction 0x{:X} ({})", pc, MemoryReadCode(pc).value(), disassemble(*parent, pc, nullptr));
            InterpreterFallback(pc, 1);
            break;
        }
        default:
            LOG_WARN("Unknown exception {} Raised at pc = 0x{:x}", static_cast<size_t>(exception), pc);
            LOG_TRACE("at address 0x{:X}, instruction 0x{:X} ({})", pc, MemoryReadCode(pc).value(), disassemble(*parent, pc, nullptr));
        }
    }

    void CallSVC(uint32_t svc) override {
        parent->svc_called = true;
        parent->svc = svc;
        cpu->jit->HaltExecution(Dynarmic::HaltReason::UserDefined8);
    }

    void AddTicks(uint64_t ticks) override {}

    uint64_t GetTicksRemaining() override {
        return 1ull << 60;
    }
};

std::unique_ptr<Dynarmic::A32::Jit> DynarmicCPU::make_jit() {
    Dynarmic::A32::UserConfig config{};
    config.arch_version = Dynarmic::A32::ArchVersion::v7;
    config.callbacks = cb.get();
    if (parent->mem->use_page_table) {
        config.page_table = (log_mem || !cpu_opt) ? nullptr : reinterpret_cast<decltype(config.page_table)>(parent->mem->page_table.get());
        config.absolute_offset_page_table = true;
    } else if (!log_mem && cpu_opt) {
        config.fastmem_pointer = std::bit_cast<uintptr_t>(parent->mem->memory.get());
    }
    config.hook_hint_instructions = true;
    config.enable_cycle_counting = false;
    config.global_monitor = monitor;
    config.coprocessors[15] = cp15;
    config.processor_id = core_id % 4; // ExclusiveMonitor sees 4 slots (real Vita = 4 cores)
    config.optimizations = cpu_opt ? Dynarmic::all_safe_optimizations : Dynarmic::no_optimizations;

    return std::make_unique<Dynarmic::A32::Jit>(config);
}

DynarmicCPU::DynarmicCPU(CPUState *state, std::size_t processor_id, Dynarmic::ExclusiveMonitor *monitor, bool cpu_opt)
    : parent(state)
    , cb(std::make_unique<ArmDynarmicCallback>(*state, *this))
    , cp15(std::make_shared<ArmDynarmicCP15>())
    , monitor(monitor)
    , core_id(processor_id)
    , cpu_opt(cpu_opt) {
    jit = make_jit();
}

DynarmicCPU::~DynarmicCPU() = default;

int DynarmicCPU::run() {
    halted = false;
    break_ = false;
    exit_request = false;
    parent->svc_called = false;
    cb->mono_exception_signaled = false;
    cb->mono_suspend_read_count = 0;
    Dynarmic::HaltReason halt_reason;
    do {
        halt_reason = jit->Run();
    } while ((halt_reason == Dynarmic::HaltReason::Step) || (halt_reason == Dynarmic::HaltReason::CacheInvalidation));

    return halted;
}

int DynarmicCPU::step() {
    parent->svc_called = false;
    jit->Step();
    return 0;
}

bool DynarmicCPU::hit_breakpoint() {
    return break_;
}

void DynarmicCPU::trigger_breakpoint() {
    break_ = true;
    stop();
}

void DynarmicCPU::set_log_code(bool log) {
    if (log_code == log)
        return;

    log_code = log;
    jit = make_jit();
}

void DynarmicCPU::set_log_mem(bool log) {
    if (log_mem == log)
        return;

    log_mem = log;
    jit = make_jit();
}

bool DynarmicCPU::get_log_code() {
    return log_code;
}

bool DynarmicCPU::get_log_mem() {
    return log_mem;
}

void DynarmicCPU::stop() {
    exit_request = true;
}

void DynarmicCPU::halt_execution() {
    jit->HaltExecution();
}

uint32_t DynarmicCPU::get_reg(uint8_t idx) {
    return jit->Regs()[idx];
}

uint32_t DynarmicCPU::get_sp() {
    return jit->Regs()[13];
}

uint32_t DynarmicCPU::get_pc() {
    return jit->Regs()[15];
}

void DynarmicCPU::set_reg(uint8_t idx, uint32_t val) {
    jit->Regs()[idx] = val;
}

void DynarmicCPU::set_cpsr(uint32_t val) {
    jit->SetCpsr(val);
}

uint32_t DynarmicCPU::get_tpidruro() {
    return cp15->get_tpidruro();
}

void DynarmicCPU::set_tpidruro(uint32_t val) {
    cp15->set_tpidruro(val);
}

void DynarmicCPU::set_pc(uint32_t val) {
    if (val & 1) {
        set_cpsr(get_cpsr() | 0x20);
        val = val & 0xFFFFFFFE;
    } else {
        set_cpsr(get_cpsr() & 0xFFFFFFDF);
        val = val & 0xFFFFFFFC;
    }
    jit->Regs()[15] = val;
}

void DynarmicCPU::set_lr(uint32_t val) {
    jit->Regs()[14] = val;
}

void DynarmicCPU::set_sp(uint32_t val) {
    jit->Regs()[13] = val;
}

uint32_t DynarmicCPU::get_cpsr() {
    return jit->Cpsr();
}

uint32_t DynarmicCPU::get_fpscr() {
    return jit->Fpscr();
}

void DynarmicCPU::set_fpscr(uint32_t val) {
    jit->SetFpscr(val);
}

CPUContext DynarmicCPU::save_context() {
    CPUContext ctx;
    ctx.cpu_registers = jit->Regs();
    static_assert(sizeof(ctx.fpu_registers) == sizeof(jit->ExtRegs()));
    memcpy(ctx.fpu_registers.data(), jit->ExtRegs().data(), sizeof(ctx.fpu_registers));
    ctx.fpscr = jit->Fpscr();
    ctx.cpsr = jit->Cpsr();

    return ctx;
}

void DynarmicCPU::load_context(const CPUContext &ctx) {
    jit->Regs() = ctx.cpu_registers;
    static_assert(sizeof(ctx.fpu_registers) == sizeof(jit->ExtRegs()));
    memcpy(jit->ExtRegs().data(), ctx.fpu_registers.data(), sizeof(ctx.fpu_registers));
    jit->SetCpsr(ctx.cpsr);
    jit->SetFpscr(ctx.fpscr);
}

uint32_t DynarmicCPU::get_lr() {
    return jit->Regs()[14];
}

float DynarmicCPU::get_float_reg(uint8_t idx) {
    return std::bit_cast<float>(jit->ExtRegs()[idx]);
}

void DynarmicCPU::set_float_reg(uint8_t idx, float val) {
    jit->ExtRegs()[idx] = std::bit_cast<uint32_t>(val);
}

bool DynarmicCPU::is_thumb_mode() {
    return jit->Cpsr() & 0x20;
}

std::size_t DynarmicCPU::processor_id() const {
    return core_id;
}

void DynarmicCPU::invalidate_jit_cache(Address start, size_t length) {
    jit->InvalidateCacheRange(start, length);
}

// TODO: proper abstraction
ExclusiveMonitorPtr new_exclusive_monitor(int max_num_cores) {
    return new Dynarmic::ExclusiveMonitor(max_num_cores);
}

void free_exclusive_monitor(ExclusiveMonitorPtr monitor) {
    Dynarmic::ExclusiveMonitor *monitor_ = static_cast<Dynarmic::ExclusiveMonitor *>(monitor);
    delete monitor_;
}

void clear_exclusive(ExclusiveMonitorPtr monitor, std::size_t core_num) {
    Dynarmic::ExclusiveMonitor *monitor_ = static_cast<Dynarmic::ExclusiveMonitor *>(monitor);
    monitor_->ClearProcessor(core_num);
}