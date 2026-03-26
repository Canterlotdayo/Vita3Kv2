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

#include "SceProcessmgr.h"

#include <io/functions.h>
#include <cpu/functions.h>
#include <kernel/state.h>
#include <rtc/rtc.h>

#include <util/safe_time.h>

#include <util/tracy.h>

#include <string>
#include <vector>
TRACY_MODULE_NAME(SceProcessmgr);

template <>
std::string to_debug_str<SceKernelPowerTickType>(const MemState &mem, SceKernelPowerTickType type) {
    switch (type) {
    case SCE_KERNEL_POWER_TICK_DEFAULT: return "SCE_KERNEL_POWER_TICK_DEFAULT";
    case SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND: return "SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND";
    case SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF: return "SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF";
    case SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING: return "SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING";
    }
    return std::to_string(type);
}

struct VitaTimeval {
    uint32_t tv_sec;
    uint32_t tv_usec;
};
struct VitaTimezone {
    int tz_minuteswest;
    int tz_dsttime;
};

using VitaTime = uint32_t;
struct VitaTM {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

static_assert(sizeof(VitaTM) == 36);
static_assert(sizeof(VitaTM) <= sizeof(struct tm));

struct SceLibkernelAddresses {
    uint32_t size;
    Ptr<void> sceKernelExitThread;
    Ptr<void> sceKernelExitDeleteThread;
    Ptr<void> _sceKernelExitCallback;
    Ptr<void> field_0x10;
    Ptr<void> field_0x14;
    Ptr<void> field_0x18;
};

EXPORT(int, _sceKernelExitProcessForUser) {
    TRACY_FUNC(_sceKernelExitProcessForUser);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceKernelGetTimer5Reg, Ptr<uint64_t> *timer) {
    TRACY_FUNC(_sceKernelGetTimer5Reg, timer);
    *timer = alloc<uint64_t>(emuenv.mem, "timer5reg");
    *(*timer).get(emuenv.mem) = rtc_get_ticks(emuenv.kernel.base_tick.tick);
    return SCE_KERNEL_OK;
}

EXPORT(int, _sceKernelRegisterLibkernelAddresses, SceLibkernelAddresses *addresses) {
    TRACY_FUNC(_sceKernelRegisterLibkernelAddresses, addresses);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCDialogSessionClose) {
    TRACY_FUNC(sceKernelCDialogSessionClose);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCDialogSetLeaseLimit) {
    TRACY_FUNC(sceKernelCDialogSetLeaseLimit);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCallAbortHandler, uint32_t param1, uint32_t param2) {
    TRACY_FUNC(sceKernelCallAbortHandler, param1, param2);

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    const char *tname = thread ? thread->name.c_str() : "unknown";

    // Mono JIT race condition workaround:
    // Mono calls abort() when a debug assertion fails in mono_internal_hash_table_insert().
    // This is a benign race condition: two threads try to JIT-compile the same method and
    // the second finds the hash slot already occupied. The assertion fires and calls abort().
    //
    // We can't just return 0 from abort() because it's __attribute__((noreturn)) - the
    // compiler didn't emit valid code after the call to abort().
    //
    // We can't just kill the thread with exit(0) either, because the JIT compilation work
    // it was doing never completes, leaving NULL function pointers that cause infinite
    // invalid-read loops later (the "red circle freeze").
    //
    // Solution: scan the thread's stack for return addresses pointing into the Mono code
    // segment. The code is compiled without frame pointers (-fomit-frame-pointer), so we
    // can't walk a frame chain. Instead we scan every aligned word on the stack looking
    // for values in the Mono code range. We need to find at least 2 such addresses:
    // one for the assertion helper and one for mono_internal_hash_table_insert.
    // We skip past both and return to the caller of the insert function.
    //
    // Call chain when assertion fires:
    //   [caller] -> mono_internal_hash_table_insert() -> g_assertion_message_expr()
    //     -> abort() [SceLibc] -> sceKernelCallAbortHandler [we are here]

    bool is_mono_abort = false;
    if (thread && emuenv.kernel.mono_code_start != 0) {
        std::string thread_name(tname);
        if (thread_name.find("Mono") != std::string::npos
            || thread_name.find("mono") != std::string::npos
            || thread_name.find("MONO") != std::string::npos) {
            is_mono_abort = true;
        }
    }

    if (is_mono_abort && thread && thread->cpu) {
        LOG_WARN("Mono abort handler: thread {} (ID: {}), params: 0x{:X}, 0x{:X} - attempting stack scan unwind",
                 tname, thread_id, param1, param2);

        uint32_t sp = read_sp(*thread->cpu);
        uint32_t lr = read_lr(*thread->cpu);
        uint32_t r11 = read_reg(*thread->cpu, 11);

        LOG_WARN("  SP=0x{:08X}, LR=0x{:08X}, R11=0x{:08X}, PC=0x{:08X}",
                 sp, lr, r11, read_pc(*thread->cpu));

        // Scan the stack for return addresses in the Mono code segment.
        // On ARM, saved LR values are pushed on the stack by each function.
        // We scan upward from SP, collecting addresses that fall in Mono's code range.
        // We need at least 2: the first is likely in g_assertion_message / abort helpers,
        // the second (or third) is in mono_internal_hash_table_insert or its caller.

        const Address mono_start = emuenv.kernel.mono_code_start;
        const Address mono_end = emuenv.kernel.mono_code_end;

        struct StackRetAddr {
            uint32_t sp_offset;
            uint32_t addr;
        };
        std::vector<StackRetAddr> mono_addrs;

        // Scan up to 512 bytes (128 words) on the stack
        const int scan_words = 128;
        for (int i = 0; i < scan_words; i++) {
            uint32_t stack_addr = sp + i * 4;
            Ptr<uint32_t> ptr(stack_addr);
            if (!ptr.valid(emuenv.mem))
                break;

            uint32_t val = *ptr.get(emuenv.mem);

            // Check if this value looks like a return address in Mono code
            // Return addresses are usually odd (Thumb) or even (ARM) but always > page_size
            // and within the Mono code segment
            if (val >= mono_start && val < mono_end) {
                mono_addrs.push_back({ static_cast<uint32_t>(i * 4), val });
                LOG_DEBUG("  Stack[SP+0x{:X}] = 0x{:08X} (Mono code)", i * 4, val);
            }
        }

        LOG_WARN("  Found {} Mono return addresses on stack", mono_addrs.size());

        // We want to skip past the assertion code and the hash table insert.
        // Heuristic: pick the 3rd Mono return address if available (skips assert helper,
        // the insert function, and lands in the caller), otherwise the 2nd, then 1st.
        int target_idx = -1;
        if (mono_addrs.size() >= 3)
            target_idx = 2;
        else if (mono_addrs.size() >= 2)
            target_idx = 1;
        else if (mono_addrs.size() >= 1)
            target_idx = 0;

        if (target_idx >= 0) {
            uint32_t target_pc = mono_addrs[target_idx].addr;
            // Set SP to just past the return address we picked (clean up the stack)
            uint32_t target_sp = sp + mono_addrs[target_idx].sp_offset + 4;

            LOG_WARN("Mono abort handler: unwinding to PC=0x{:08X}, SP=0x{:08X} (idx={})",
                     target_pc, target_sp, target_idx);

            write_pc(*thread->cpu, target_pc);
            write_sp(*thread->cpu, target_sp);
            write_reg(*thread->cpu, 0, 0); // return value = 0
            return 0;
        }

        // Stack scan also failed - extend scan range
        LOG_WARN("  Short scan failed, trying extended scan (2048 bytes)...");
        const int extended_scan = 512;
        for (int i = scan_words; i < extended_scan; i++) {
            uint32_t stack_addr = sp + i * 4;
            Ptr<uint32_t> ptr(stack_addr);
            if (!ptr.valid(emuenv.mem))
                break;

            uint32_t val = *ptr.get(emuenv.mem);
            if (val >= mono_start && val < mono_end) {
                mono_addrs.push_back({ static_cast<uint32_t>(i * 4), val });
                LOG_DEBUG("  Stack[SP+0x{:X}] = 0x{:08X} (Mono code, extended)", i * 4, val);
                // Use the first one found in extended range
                if (mono_addrs.size() >= 2) {
                    uint32_t target_pc = val;
                    uint32_t target_sp = sp + i * 4 + 4;
                    LOG_WARN("Mono abort handler: unwinding (extended) to PC=0x{:08X}, SP=0x{:08X}",
                             target_pc, target_sp);
                    write_pc(*thread->cpu, target_pc);
                    write_sp(*thread->cpu, target_sp);
                    write_reg(*thread->cpu, 0, 0);
                    return 0;
                }
            }
        }

        // All unwind attempts failed - kill the thread as last resort
        LOG_ERROR("Mono abort handler: stack unwind failed for thread {} (ID: {}), killing thread",
                  tname, thread_id);
        thread->exit(0);
        return 0;
    }

    // Non-Mono abort: log and return 0 (best effort)
    LOG_WARN("Abort handler called on thread {} (ID: {}), params: 0x{:X}, 0x{:X}",
             tname, thread_id, param1, param2);
    return 0;
}

EXPORT(int, sceKernelGetCurrentProcess) {
    TRACY_FUNC(sceKernelGetCurrentProcess);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetExtraTty) {
    TRACY_FUNC(sceKernelGetExtraTty);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetProcessName, char *process_name, uint32_t len) {
    TRACY_FUNC(sceKernelGetProcessName, process_name, len);
    if (!process_name || len > 32 || len == 0)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    strncpy(process_name, emuenv.kernel.process_param.get(emuenv.mem)->process_name.get(emuenv.mem), len);
    return 0;
}

EXPORT(Ptr<SceProcessParam>, sceKernelGetProcessParam, void *args) {
    TRACY_FUNC(sceKernelGetProcessParam, args);
    return emuenv.kernel.process_param;
}

EXPORT(int, sceKernelGetProcessTimeCore) {
    TRACY_FUNC(sceKernelGetProcessTimeCore);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetProcessTimeLowCore) {
    TRACY_FUNC(sceKernelGetProcessTimeLowCore);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetProcessTimeWideCore) {
    TRACY_FUNC(sceKernelGetProcessTimeWideCore);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetProcessTitleId, char *title_id, uint32_t len) {
    TRACY_FUNC(sceKernelGetProcessTitleId, title_id, len);
    if (!title_id || len > 32 || len == 0)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    strncpy(title_id, emuenv.io.title_id.c_str(), len);
    return 0;
}

EXPORT(int, sceKernelGetRemoteProcessTime) {
    TRACY_FUNC(sceKernelGetRemoteProcessTime);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetStderr) {
    TRACY_FUNC(sceKernelGetStderr);
    return open_file(emuenv.io, "tty0:", SCE_O_WRONLY, emuenv.pref_path, export_name);
}

EXPORT(int, sceKernelGetStdin) {
    TRACY_FUNC(sceKernelGetStdin);
    return open_file(emuenv.io, "tty0:", SCE_O_RDONLY, emuenv.pref_path, export_name);
}

EXPORT(int, sceKernelGetStdout) {
    TRACY_FUNC(sceKernelGetStdout);
    return open_file(emuenv.io, "tty0:", SCE_O_WRONLY, emuenv.pref_path, export_name);
}

EXPORT(int, sceKernelIsCDialogAvailable) {
    TRACY_FUNC(sceKernelIsCDialogAvailable);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelIsGameBudget) {
    TRACY_FUNC(sceKernelIsGameBudget);
    return UNIMPLEMENTED();
}

EXPORT(VitaTime, sceKernelLibcClock) {
    TRACY_FUNC(sceKernelLibcClock);
    return static_cast<VitaTime>(rtc_get_ticks(emuenv.kernel.base_tick.tick) - emuenv.kernel.start_tick);
}

EXPORT(int, sceKernelLibcGettimeofday, VitaTimeval *timeAddr, VitaTimezone *tzAddr) {
    TRACY_FUNC(sceKernelLibcGettimeofday, timeAddr, tzAddr);
    const auto ticks = rtc_get_ticks(emuenv.kernel.base_tick.tick) - RTC_OFFSET;
    if (timeAddr != nullptr) {
        timeAddr->tv_sec = static_cast<std::uint32_t>(ticks / VITA_CLOCKS_PER_SEC);
        timeAddr->tv_usec = ticks % VITA_CLOCKS_PER_SEC;
    }
    if (tzAddr != nullptr) {
        std::time_t t = std::time(nullptr);

        tm localtime_tm = {};

        SAFE_LOCALTIME(&t, &localtime_tm);
        std::time_t lt = mktime(&localtime_tm);
        tzAddr->tz_minuteswest = static_cast<int>((lt - t) / 60);
    }
    return 0;
}

EXPORT(Ptr<VitaTM>, sceKernelLibcGmtime_r, const VitaTime *time, Ptr<VitaTM> date) {
    TRACY_FUNC(sceKernelLibcGmtime_r, time, date);
    const time_t plat_time = *time;

    auto dateIn = date.get(emuenv.mem);

    tm host_tm = {};
    SAFE_GMTIME(&plat_time, &host_tm);
    memcpy(dateIn, &host_tm, sizeof(VitaTM));

    return date;
}

EXPORT(Ptr<VitaTM>, sceKernelLibcLocaltime_r, const VitaTime *time, Ptr<VitaTM> date) {
    TRACY_FUNC(sceKernelLibcLocaltime_r, time, date);
    const time_t plat_time = *time;
    auto dateIn = date.get(emuenv.mem);

    tm host_tm = {};
    SAFE_LOCALTIME(&plat_time, &host_tm);
    memcpy(dateIn, &host_tm, sizeof(VitaTM));

    return date;
}

EXPORT(int, sceKernelLibcMktime, VitaTM *date, VitaTime *time, uint64_t *param_3) {
    TRACY_FUNC(sceKernelLibcMktime, date, time);
    // param_3 - result, 8 bytes, unused
    if (!date) {
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    }
    bool year_1900 = false;
    if (date->tm_year >= 1900) {
        date->tm_year -= 1900;
        year_1900 = true;
    }
    tm host_tm = {};
    // Copy the input date to host_tm and use that on mktime instead of the input directly
    // to avoid stack corruption on systems where tm size is different
    memcpy(&host_tm, date, sizeof(VitaTM));
    auto time_local = mktime(&host_tm);
    memcpy(date, &host_tm, sizeof(VitaTM));
    if (year_1900) {
        date->tm_year += 1900;
    }
    if (time)
        *time = static_cast<VitaTime>(time_local);
    if (param_3)
        *param_3 = time_local;
    return 0;
}

EXPORT(VitaTime, sceKernelLibcTime, VitaTime *time) {
    TRACY_FUNC(sceKernelLibcTime, time);
    const auto secs = (rtc_get_ticks(emuenv.kernel.base_tick.tick) - RTC_OFFSET) / VITA_CLOCKS_PER_SEC;

    if (time) {
        *time = static_cast<VitaTime>(secs);
    }

    return static_cast<VitaTime>(secs);
}

EXPORT(int, sceKernelPowerLock) {
    TRACY_FUNC(sceKernelPowerLock);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelPowerTick, SceKernelPowerTickType type) {
    TRACY_FUNC(sceKernelPowerTick, type);
    return SCE_KERNEL_OK;
}

EXPORT(int, sceKernelPowerUnlock) {
    TRACY_FUNC(sceKernelPowerUnlock);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelRegisterProcessTerminationCallback) {
    TRACY_FUNC(sceKernelRegisterProcessTerminationCallback);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelUnregisterProcessTerminationCallback) {
    TRACY_FUNC(sceKernelUnregisterProcessTerminationCallback);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetMainModuleSdkVersion) {
    TRACY_FUNC(sceKernelGetMainModuleSdkVersion);
    SceProcessParam *process_param = emuenv.kernel.process_param.get(emuenv.mem);
    if (process_param && (process_param->magic == '2PSP') && (process_param->version != 0)) {
        return process_param->fw_version;
    } else {
        return 0;
    }
}