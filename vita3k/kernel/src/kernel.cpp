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

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include <kernel/state.h>

#include <kernel/thread/thread_state.h>

#include <cpu/functions.h>
#include <mem/ptr.h>
#include <util/lock_and_find.h>
#include <util/log.h>

#include <SDL3/SDL_mutex.h>

#include <thread>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

int CorenumAllocator::new_corenum() {
    const std::lock_guard<std::mutex> guard(lock);

    uint32_t size = 1;
    return alloc.allocate_from(0, size);
}

void CorenumAllocator::free_corenum(const int num) {
    const std::lock_guard<std::mutex> guard(lock);
    alloc.free(num, 1);
}

void CorenumAllocator::set_max_core_count(const std::size_t max) {
    const std::lock_guard<std::mutex> guard(lock);
    alloc.set_maximum(max);
}

// TODO implement cross platform debug thread name setter and eliminate SDL thread
struct ThreadParams {
    KernelState *kernel = nullptr;
    SceUID thid = SCE_KERNEL_ERROR_ILLEGAL_THREAD_ID;
    SDL_Semaphore *host_may_destroy_params = nullptr;
};

// Pin the current host thread to a specific OS core.
// Threads pinned to the same core are naturally time-sliced by the OS kernel
// via hardware timer interrupt — no cycle counting or mutex needed.
static void pin_thread_to_core(int core_id) {
#if defined(__APPLE__)
    thread_affinity_policy_data_t policy = { core_id + 1 };
    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_AFFINITY_POLICY,
                      (thread_policy_t)&policy, 1);
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id % std::thread::hardware_concurrency(), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(_WIN32)
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << (core_id % 64));
#endif
}

static int SDLCALL thread_function(void *data) {
    assert(data != nullptr);
    const ThreadParams params = *static_cast<const ThreadParams *>(data);
    SDL_SignalSemaphore(params.host_may_destroy_params);
    const ThreadStatePtr thread = params.kernel->get_thread(params.thid);
#ifdef TRACY_ENABLE
    if (!thread->name.empty()) {
        tracy::SetThreadName(thread->name.c_str());
    } else {
        std::string th_name = "TID:" + std::to_string(thread->id);
        tracy::SetThreadName(th_name.c_str());
    }
#endif

    // Pin Mono threads to the same OS core so the OS kernel time-slices them.
    // This prevents the parallelism that causes race conditions in mono_class_init
    // without any Dynarmic cycle counting overhead.
    if (thread->name.find("Mono") != std::string::npos) {
        pin_thread_to_core(0);
    }

    try {
        thread->run_loop();
    } catch (const std::system_error &e) {
        LOG_ERROR("Thread {} (ID: {}) caught system_error: {}", thread->name, thread->id, e.what());
    } catch (const std::exception &e) {
        LOG_ERROR("Thread {} (ID: {}) caught exception: {}", thread->name, thread->id, e.what());
    }
    const uint32_t r0 = read_reg(*thread->cpu, 0);

    std::lock_guard<std::mutex> lock(params.kernel->mutex);
    params.kernel->threads.erase(thread->id);
    params.kernel->corenum_allocator.free_corenum(get_processor_id(*thread->cpu));

    return r0;
}

KernelState::KernelState()
    : debugger(*this) {
}

bool KernelState::init(MemState &mem, const CallImportFunc &call_import, bool cpu_opt) {
    constexpr std::size_t MAX_CORE_COUNT = 150;

    corenum_allocator.set_max_core_count(MAX_CORE_COUNT);
    exclusive_monitor = new_exclusive_monitor(MAX_CORE_COUNT);
    start_tick = rtc_get_ticks(rtc_base_ticks());
    base_tick = { rtc_base_ticks() };
    cpu_protocol = std::make_unique<CPUProtocol>(*this, mem, call_import);
    this->cpu_opt = cpu_opt;

    return true;
}

void KernelState::load_process_param(MemState &mem, Ptr<uint32_t> ptr) {
    const SceProcessParam *param = ptr.cast<SceProcessParam>().get(mem);
    if (param->version == 0) {
        // Homebrews built with old vitasdk
        process_param = nullptr;
        return;
    }
    process_param = ptr.cast<SceProcessParam>();
    // VAR_NID(__sce_libcparam, 0xDF084DFA)
    // no memory leak because we don't allocate memory for this variable intially
    export_nids[0xDF084DFA] = process_param.get(mem)->sce_libc_param.address();
}

void KernelState::set_memory_watch(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &thread : threads) {
        auto &cpu = *thread.second->cpu;
        if (enabled != get_log_mem(cpu)) {
            if (enabled)
                set_log_mem(cpu, true);
            else
                set_log_mem(cpu, false);
        }
    }
}

void KernelState::invalidate_jit_cache(Address start, size_t length) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &[_, thread] : threads) {
        ::invalidate_jit_cache(*thread->cpu, start, length);
    }
}

ThreadStatePtr KernelState::get_thread(SceUID thread_id) {
    return lock_and_find(thread_id, threads, mutex);
}

ThreadStatePtr KernelState::create_thread(MemState &mem, const char *name, Ptr<const void> entry_point) {
    return create_thread(mem, name, entry_point, SCE_KERNEL_DEFAULT_PRIORITY, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, SCE_KERNEL_STACK_SIZE_USER_MAIN, nullptr);
}

ThreadStatePtr KernelState::create_thread(MemState &mem, const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option) {
    ThreadStatePtr thread = std::make_shared<ThreadState>(get_next_uid(), *this, mem);
    if (thread->init(name, entry_point, init_priority, affinity_mask, stack_size, option) < 0)
        return nullptr;

    // Detect Mono worker threads by name. Only threads with "Mono" in
    // their name are actual Mono managed workers that need GC registration.
    // System threads (SceFios, FMOD, avAudio, Unity*) must NOT be registered
    // even if their entry points are in the eboot range.
    bool is_mono_worker = (name && std::string(name).find("Mono") != std::string::npos);
    bool is_exception_handler = (name && std::string(name).find("ExceptionHandler") != std::string::npos);

    if (is_mono_worker || is_exception_handler) {
        thread->cpu->use_mono_scheduling = true;
    }

    // Register actual Mono workers in the exception table only.
    // The GC hash table is managed by Mono's own GC_register_my_thread —
    // we must NOT write to it (duplicates cause infinite loops during GC).
    // We only need the exception table so the Mono exception callback
    // finds the thread and returns r0≠0 (enabling NullReferenceException dispatch).
    if (is_mono_worker && !is_exception_handler && mono_data_start != 0) {
        constexpr uint32_t EXCEPTION_TABLE_OFFSET = 0x66A10;
        constexpr uint32_t EXCEPTION_COUNTER_OFFSET = 0x4F34;

        Address ep = entry_point.address();

        // The Mono exception callback searches by pthread_self return value,
        // which is the pthread_t handle (pointer to pthread struct).
        // The game names threads "8BC449B0 Mono" where the hex prefix IS
        // the pthread_t. Extract it; fall back to SceUID if parsing fails.
        uint32_t thread_id_val = static_cast<uint32_t>(thread->id);
        if (name) {
            char *end = nullptr;
            unsigned long parsed = strtoul(name, &end, 16);
            if (end != name && *end == ' ' && parsed > 0x80000000) {
                thread_id_val = static_cast<uint32_t>(parsed);
            }
        }

        // --- Exception Table Registration ---
        Address counter_addr = mono_data_start + EXCEPTION_COUNTER_OFFSET;
        Address table_addr = mono_data_start + EXCEPTION_TABLE_OFFSET;
        uint32_t *counter = Ptr<uint32_t>(counter_addr).get(mem);
        uint32_t idx = *counter;
        if (idx < 256) {
            Address exc_entry = alloc(mem, 8, "GC_exc_entry");
            if (exc_entry) {
                uint32_t *exc = Ptr<uint32_t>(exc_entry).get(mem);
                exc[0] = thread_id_val;
                exc[1] = 0;

                uint32_t *table = Ptr<uint32_t>(table_addr).get(mem);
                table[idx] = exc_entry;
                *counter = idx + 1;
            }
            LOG_INFO("Mono GC: registered '{}' (ID:{}, pthread_t=0x{:08X}) in exception table (idx={})",
                     name, thread->id, thread_id_val, idx);
        } else {
            LOG_WARN("Mono GC: skipping exception table for '{}' (ID:{}) — counter={} (not initialized yet)",
                     name, thread->id, idx);
        }
    }

    const auto lock = std::lock_guard(mutex);
    threads.emplace(thread->id, thread);

    ThreadParams params;
    params.kernel = this;
    params.thid = thread->id;

    params.host_may_destroy_params = SDL_CreateSemaphore(0);
    SDL_DetachThread(SDL_CreateThread(&thread_function, thread->name.c_str(), &params));
    SDL_WaitSemaphore(params.host_may_destroy_params);
    SDL_DestroySemaphore(params.host_may_destroy_params);
    return thread;
}

Ptr<Ptr<void>> KernelState::get_thread_tls_addr(MemState &mem, SceUID thread_id, int key) {
    Ptr<Ptr<void>> address(0);
    // magic numbers taken from decompiled source. There is 0x400 unused bytes of unknown usage
    if (key <= 0x100 && key >= 0) {
        const ThreadStatePtr thread = get_thread(thread_id);
        address = thread->tls.get_ptr<Ptr<void>>() + key;
    } else {
        LOG_ERROR("Wrong tls slot index. TID:{} index:{}", thread_id, key);
    }
    return address;
}

void KernelState::exit_delete_all_threads() {
    const std::lock_guard<std::mutex> lock(mutex);
    for (auto &[_, thread] : threads)
        // Skip end callbacks; running guest code can access torn-down state
        thread->exit_delete(false);
}

void KernelState::pause_threads() {
    const std::lock_guard<std::mutex> lock(mutex);
    for (auto &[_, thread] : threads) {
        paused_threads_status[thread->id] = thread->status;
        if (thread->status == ThreadStatus::run)
            thread->suspend();
    }
}

void KernelState::resume_threads() {
    const std::lock_guard<std::mutex> lock(mutex);
    for (auto &[_, thread] : threads) {
        if (paused_threads_status[thread->id] == ThreadStatus::run)
            thread->resume();
    }
    paused_threads_status.clear();
}

SceKernelModuleInfo *KernelState::find_module_by_addr(Address address) {
    const auto lock = std::lock_guard(mutex);
    for (auto &[_, mod] : loaded_modules) {
        for (auto &seg : mod->info.segments) {
            if (!seg.size)
                continue;
            if (seg.vaddr.address() <= address && address <= seg.vaddr.address() + seg.memsz) {
                return &mod->info;
            }
        }
    }
    return nullptr;
}