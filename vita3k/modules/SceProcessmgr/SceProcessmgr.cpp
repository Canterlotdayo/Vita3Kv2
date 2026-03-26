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
    // compiler didn't emit valid code after the call to abort(). Returning would execute
    // garbage instructions and crash.
    //
    // We can't just kill the thread with exit(0) either, because the JIT compilation work
    // it was doing never completes, leaving NULL function pointers that cause infinite
    // invalid-read loops later (the "red circle freeze").
    //
    // Solution: walk the ARM frame pointer chain to unwind the stack past the assertion
    // and the insert function. This returns control to the caller of
    // mono_internal_hash_table_insert() as if the insert simply returned, which is safe
    // because the entry already exists in the table (the duplicate is identical).
    //
    // Call chain when assertion fires:
    //   mono code -> mono_internal_hash_table_insert() -> g_assertion_message_expr()
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
        LOG_WARN("Mono abort handler: thread {} (ID: {}), params: 0x{:X}, 0x{:X} - attempting stack unwind",
                 tname, thread_id, param1, param2);

        // Walk the ARM frame pointer chain.
        // ARM calling convention with frame pointer:
        //   prologue: push {r4-rN, r11, lr}; add r11, sp, #offset
        //   [r11+0] = saved r11 (previous frame pointer)
        //   [r11+4] = saved lr  (return address)
        //
        // We unwind until we find a return address pointing into the Mono code segment.
        // That frame is mono_internal_hash_table_insert() or its immediate caller.
        // We then go one more frame up to skip the insert entirely.

        uint32_t fp = read_reg(*thread->cpu, 11); // r11 = frame pointer
        uint32_t target_pc = 0;
        uint32_t target_sp = 0;
        uint32_t target_fp = 0;

        for (int i = 0; i < 8; i++) {
            if (fp == 0 || fp < emuenv.mem.page_size)
                break;

            Ptr<uint32_t> fp_ptr(fp);
            if (!fp_ptr.valid(emuenv.mem))
                break;

            uint32_t saved_fp = *Ptr<uint32_t>(fp).get(emuenv.mem);
            uint32_t saved_lr = *Ptr<uint32_t>(fp + 4).get(emuenv.mem);

            LOG_DEBUG("  Frame {}: fp=0x{:08X}, saved_fp=0x{:08X}, saved_lr=0x{:08X}", i, fp, saved_fp, saved_lr);

            // Look for the first frame whose return address points into Mono code.
            // This is the frame that called into the assertion/abort chain.
            if (saved_lr >= emuenv.kernel.mono_code_start && saved_lr < emuenv.kernel.mono_code_end) {
                // Go one MORE frame up to skip mono_internal_hash_table_insert itself
                if (saved_fp != 0 && saved_fp >= emuenv.mem.page_size) {
                    Ptr<uint32_t> next_fp_ptr(saved_fp);
                    if (next_fp_ptr.valid(emuenv.mem)) {
                        uint32_t next_saved_fp = *Ptr<uint32_t>(saved_fp).get(emuenv.mem);
                        uint32_t next_saved_lr = *Ptr<uint32_t>(saved_fp + 4).get(emuenv.mem);
                        target_pc = next_saved_lr;
                        target_sp = saved_fp + 8;
                        target_fp = next_saved_fp;
                        LOG_DEBUG("  Target (skip insert): PC=0x{:08X}, SP=0x{:08X}, FP=0x{:08X}",
                                  target_pc, target_sp, target_fp);
                    }
                }
                // Fallback: return to this frame if we can't go one more up
                if (target_pc == 0) {
                    target_pc = saved_lr;
                    target_sp = fp + 8;
                    target_fp = saved_fp;
                }
                break;
            }
            fp = saved_fp;
        }

        if (target_pc != 0 && target_pc > emuenv.mem.page_size) {
            LOG_WARN("Mono abort handler: unwinding to PC=0x{:08X}, SP=0x{:08X}, FP=0x{:08X}",
                     target_pc, target_sp, target_fp);
            write_pc(*thread->cpu, target_pc);
            write_sp(*thread->cpu, target_sp);
            write_reg(*thread->cpu, 11, target_fp);
            write_reg(*thread->cpu, 0, 0); // return value = 0 (success)
            return 0;
        }

        // Stack unwind failed - fall through to kill the thread as last resort
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