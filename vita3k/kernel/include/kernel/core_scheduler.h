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

#include <util/types.h>

#include <condition_variable>
#include <mutex>
#include <set>
#include <atomic>

struct ThreadState;

// Per-core cooperative scheduler.
//
// On the real PS Vita, each of the 3 user CPU cores runs one thread at a time,
// preemptively time-slicing between threads assigned to that core by priority.
// Threads on the same core are NEVER truly parallel — they are interleaved.
//
// Vita3K originally created one host OS thread per guest thread, meaning threads
// on the "same core" could execute simultaneously on different physical cores.
// This causes race conditions in guest code that relies on single-core atomicity
// (e.g., Mono's class init, which uses a simple flag check + write pattern that
// is safe when serialized but broken under true parallelism).
//
// CoreScheduler serializes execution: only one guest thread may execute guest code
// on a given core at any time. It uses a token-passing model:
//
//   1. Before running guest code, a thread calls acquire() to get the core token.
//      If another thread holds it, the caller sleeps on a condition variable.
//
//   2. After a scheduling quantum expires (Dynarmic returns due to tick exhaustion)
//      or before an SVC that may block (sync primitive wait), the thread calls
//      release() to pass the token to the next highest-priority waiting thread.
//
//   3. When a thread blocks on a sync primitive, it releases the token. When woken,
//      it re-acquires. This keeps the core productive while threads are sleeping.
//
// Priority ordering: lower numeric priority = higher scheduling priority (matching
// the Vita's convention where SCE_KERNEL_HIGHEST_PRIORITY_USER = 64).

class CoreScheduler {
public:
    explicit CoreScheduler(int core_id);

    // Acquire the execution token for this core.
    // Blocks until this thread is the highest-priority runnable thread
    // and no other thread holds the token.
    // Must be called before executing any guest code.
    void acquire(ThreadState *thread);

    // Release the execution token, allowing the next thread to run.
    // Must be called after guest code returns (quantum expired, SVC, halt).
    void release(ThreadState *thread);

    // Add a thread to this core's run queue.
    // Called when a thread is created/started with affinity for this core,
    // or when a thread is woken from a sync primitive wait.
    void add_thread(ThreadState *thread);

    // Remove a thread from this core's run queue.
    // Called when a thread exits, is deleted, or changes affinity.
    void remove_thread(ThreadState *thread);

    // Check if a thread is currently the active (token-holding) thread.
    bool is_active(ThreadState *thread) const;

    // Yield: release and immediately re-acquire (goes to back of same-priority group).
    // Convenience for quantum expiry.
    void yield(ThreadState *thread);

    int get_core_id() const { return core_id; }

private:
    // Comparison: lower priority value = runs first (higher scheduling priority).
    // Ties broken by thread ID for determinism.
    struct ThreadPriorityCompare {
        bool operator()(const ThreadState *a, const ThreadState *b) const;
    };

    int core_id;

    mutable std::mutex mutex;
    std::condition_variable cv;

    // The thread currently holding the execution token (running guest code).
    // nullptr if no thread is running.
    ThreadState *active_thread = nullptr;

    // Threads waiting to run on this core, ordered by priority.
    std::set<ThreadState *, ThreadPriorityCompare> run_queue;

    // Pick the highest-priority thread from the run queue.
    ThreadState *pick_next() const;
};
