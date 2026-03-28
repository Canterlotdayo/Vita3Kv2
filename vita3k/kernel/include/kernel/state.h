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

#pragma once

#include <kernel/callback.h>
#include <kernel/cpu_protocol.h>
#include <cpu/common.h>
#include <kernel/debugger.h>
#include <kernel/object_store.h>
#include <kernel/sync_primitives.h>
#include <kernel/types.h>
#include <mem/allocator.h>
#include <mem/ptr.h>
#include <mem/util.h>
#include <rtc/rtc.h>
#include <util/containers.h>
#include <util/types.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct ThreadState;

struct SDL_Thread;

struct CodecEngineBlock;

struct KernelModule {
    SceKernelModuleInfo info;
    Ptr<const uint8_t> info_segment_address;
    uint32_t info_offset;
};
typedef std::shared_ptr<KernelModule> SceKernelModulePtr;

typedef std::shared_ptr<ThreadState> ThreadStatePtr;
typedef std::map<SceUID, CodecEngineBlock> CodecEngineBlocks;
typedef std::map<SceUID, Ptr<Ptr<void>>> SlotToAddress;
typedef std::map<SceUID, ThreadStatePtr> ThreadStatePtrs;
typedef std::shared_ptr<SDL_Thread> ThreadPtr;
typedef std::map<SceUID, ThreadPtr> ThreadPtrs;
typedef std::map<SceUID, SceKernelModulePtr> SceKernelModuleInfoPtrs;
typedef std::map<SceUID, CallbackPtr> CallbackPtrs;
typedef unordered_map_fast<uint32_t, Address> ExportNids;

typedef std::map<Address, uint32_t> NotFoundVars;
typedef std::unique_ptr<CPUProtocol> CPUProtocolPtr;

struct CodecEngineBlock {
    uint32_t size;
    int32_t vaddr;
};

using LoadedSysmodules = std::map<SceSysmoduleModuleId, std::vector<SceUID>>;
using LoadedInternalSysmodules = std::vector<SceSysmoduleInternalModuleId>;

struct CorenumAllocator {
    BitmapAllocator alloc;
    std::mutex lock;

    void set_max_core_count(const std::size_t max);

    int new_corenum();
    void free_corenum(const int num);
};

struct VarBindingInfo {
    void *entries;
    uint32_t size;
    uint32_t module_nid;
};

typedef std::multimap<uint32_t, VarBindingInfo> VarBindingInfos;
typedef std::multimap<uint32_t, Address> FuncBindingInfos;

typedef std::map<uint32_t, uint32_t> ModuleUidByNid;

struct KernelState {
    KernelState();

    std::mutex mutex;
    CodecEngineBlocks codec_blocks;

    Ptr<const void> tls_address = Ptr<const void>(0);
    unsigned int tls_psize = 0;
    unsigned int tls_msize = 0;

    Ptr<const void> thread_event_start = Ptr<const void>(0);
    Address thread_event_start_arg = 0;
    Ptr<const void> thread_event_end = Ptr<const void>(0);
    Address thread_event_end_arg = 0;

    SimpleEventPtrs simple_events;
    TimerPtrs timers;
    SemaphorePtrs semaphores;
    CondvarPtrs condvars;
    CondvarPtrs lwcondvars;
    MutexPtrs mutexes;
    MutexPtrs lwmutexes; // also Mutexes for now
    RWLockPtrs rwlocks;
    EventFlagPtrs eventflags;
    MsgPipePtrs msgpipes;
    CallbackPtrs callbacks;

    ThreadStatePtrs threads;
    void *jni_env;
    void *jni_activity;

    SceKernelModuleInfoPtrs loaded_modules;
    LoadedSysmodules loaded_sysmodules;
    LoadedInternalSysmodules loaded_internal_sysmodules;

    // the variables in this block must be accessed by first locking export_nids_mutex
    std::mutex export_nids_mutex;
    ExportNids export_nids;
    FuncBindingInfos func_binding_infos;
    VarBindingInfos var_binding_infos;
    ModuleUidByNid module_uid_by_nid;

    bool cpu_opt;
    CorenumAllocator corenum_allocator;
    CPUProtocolPtr cpu_protocol;
    ExclusiveMonitorPtr exclusive_monitor;

    ObjectStore obj_store;

    // Mono JIT race condition workaround:
    // Store the address range of mono-vita.suprx code segment so we can detect
    // when abort() is called from Mono code (due to benign hash table assertion
    // caused by concurrent JIT compilation on multiple threads).
    Address mono_code_start = 0;
    Address mono_code_end = 0;
    Address mono_data_start = 0;    // segment 1 base of mono-vita.suprx

    // Per-core scheduling: on the real Vita, threads with the same CPU affinity
    // share a core and are time-sliced (never truly parallel). Vita3K runs each
    // guest thread on its own host thread, causing true parallelism and race
    // conditions in guest code that assumes single-core cooperative scheduling.
    // These mutexes + cycle-limited execution emulate per-core time slicing.
    static constexpr int NUM_CORES = 3;

    // Serializes Mono worker thread execution to prevent race conditions.
    std::mutex mono_thread_mutex;

    // Mono exception handler mechanism:
    // On real Vita, when a thread hits a null pointer / illegal access, the kernel
    // converts the hardware fault into a signal that wakes the Mono exception handler
    // thread via sceKernelWaitExceptionForMono(). The exception handler then suspends
    // the faulting thread, reads/modifies its CPU context (to redirect PC to the C#
    // exception handler), and resumes it.
    //
    // In Vita3K, Dynarmic doesn't generate real hardware faults. Instead, MemoryReadCode
    // and MemoryRead detect null accesses. We signal the exception handler thread here
    // and suspend the faulting thread until Mono processes the exception.
    std::mutex mono_exception_mutex;
    bool mono_exception_pending = false;
    SceUID mono_exception_handler_thread = 0;  // thread ID of ExceptionHandlerThread
    SceUID mono_exception_sema = 0;            // semaphore to block ExceptionHandlerThread
    SceUID mono_exception_thread_id = 0;       // faulting thread ID
    Address mono_exception_fault_addr = 0;     // address that caused the fault
    Address mono_exception_fault_pc = 0;       // PC at time of fault
    CPUContext mono_exception_saved_context;    // full CPU context at time of fault
    int mono_exception_blocked_count = 0;      // throttle counter for BLOCKED log messages

    // Pthread implementation for SceLibMonoBridge (used by mono-vita.suprx).
    // On real Vita, the pthread module provides POSIX threading on top of the
    // Vita kernel. Mono uses pthread for TLS (thread-local storage), mutexes,
    // condition variables, and thread management. Without working pthread,
    // Mono can't register worker threads in the GC, causing exception handling
    // to fail for those threads.
    struct PthreadState {
        // TLS (Thread Local Storage)
        static constexpr int MAX_KEYS = 256;
        std::mutex tls_mutex;
        int next_key = 1;
        std::map<int, void(*)(void*)> key_destructors;  // key → destructor function
        std::map<SceUID, std::map<int, uint32_t>> tls_data;  // thread_id → (key → value)

        // Mutex tracking (guest address → host mutex)
        std::mutex mutex_map_mutex;
        std::map<uint32_t, std::shared_ptr<std::recursive_mutex>> mutexes;  // guest_addr → mutex
        int next_mutex_id = 1;

        // Condition variable tracking
        std::mutex cond_map_mutex;
        struct CondVar {
            std::condition_variable_any cv;
        };
        std::map<uint32_t, std::shared_ptr<CondVar>> condvars;  // guest_addr → condvar

        // Thread handle mapping: SceUID → pthread_t (we use SceUID as pthread_t)
    } pthread;

    uint64_t start_tick;
    SceRtcTick base_tick;
    Ptr<SceProcessParam> process_param;

    Debugger debugger;

    SceUID get_next_uid() {
        return next_uid++;
    }

    bool init(MemState &mem, const CallImportFunc &call_import, bool cpu_opt);
    void load_process_param(MemState &mem, Ptr<uint32_t> ptr);
    ThreadStatePtr create_thread(MemState &mem, const char *name, Ptr<const void> entry_point = Ptr<const void>(0));
    ThreadStatePtr create_thread(MemState &mem, const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option);

    ThreadStatePtr get_thread(SceUID thread_id);
    Ptr<Ptr<void>> get_thread_tls_addr(MemState &mem, SceUID thread_id, int key);

    void exit_delete_all_threads();
    bool is_threads_paused() { return !paused_threads_status.empty(); }
    void pause_threads();
    void resume_threads();

    void set_memory_watch(bool enabled);
    void invalidate_jit_cache(Address start, size_t length);
    SceKernelModuleInfo *find_module_by_addr(Address address);

private:
    std::atomic<SceUID> next_uid{ 1 };
    std::map<SceUID, ThreadStatus> paused_threads_status;
};