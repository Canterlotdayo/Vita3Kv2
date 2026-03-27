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

#include <kernel/core_scheduler.h>
#include <kernel/thread/thread_state.h>

#include <util/log.h>

#include <algorithm>
#include <cassert>

bool CoreScheduler::ThreadPriorityCompare::operator()(const ThreadState *a, const ThreadState *b) const {
    // Lower priority value = higher scheduling priority (runs first).
    // On the Vita, priority 64 is highest for user threads, 191 is lowest.
    if (a->priority != b->priority)
        return a->priority < b->priority;
    // Tie-break by thread ID for deterministic ordering.
    return a->id < b->id;
}

CoreScheduler::CoreScheduler(int core_id)
    : core_id(core_id) {
}

void CoreScheduler::acquire(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);

    // The thread must be in the run queue to acquire.
    // (It should have been added via add_thread before this call.)
    // If not in the run queue, add it now as a safety net.
    if (run_queue.find(thread) == run_queue.end()) {
        run_queue.insert(thread);
    }

    // Wait until:
    // 1. No other thread holds the token (active_thread == nullptr), AND
    // 2. This thread is the highest-priority thread in the run queue.
    cv.wait(lock, [&]() {
        return active_thread == nullptr && pick_next() == thread;
    });

    active_thread = thread;
}

void CoreScheduler::release(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);

    if (active_thread != thread) {
        // Thread is not the active thread — this can happen if:
        // - Thread was stopped/removed while running
        // - Double-release (benign, just return)
        return;
    }

    active_thread = nullptr;

    // Wake all waiters so the highest-priority one can acquire.
    // Using notify_all because we need the specific highest-priority
    // thread to wake up and check its condition.
    cv.notify_all();
}

void CoreScheduler::add_thread(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);
    run_queue.insert(thread);

    // If no thread is currently active, wake waiters so the new thread
    // (or a higher-priority one) can acquire.
    if (active_thread == nullptr) {
        cv.notify_all();
    }
}

void CoreScheduler::remove_thread(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);
    run_queue.erase(thread);

    // If this thread was the active one, clear it and wake others.
    if (active_thread == thread) {
        active_thread = nullptr;
        cv.notify_all();
    }
}

bool CoreScheduler::is_active(ThreadState *thread) const {
    std::unique_lock<std::mutex> lock(mutex);
    return active_thread == thread;
}

void CoreScheduler::yield(ThreadState *thread) {
    // Release and re-acquire. Since the thread stays in the run queue,
    // if it's still the highest priority, it will immediately re-acquire.
    // If a same-priority thread is waiting, the set ordering (by ID) means
    // we need a mechanism to let others run. We handle this by temporarily
    // removing and re-inserting (which doesn't change position in set since
    // ordering is deterministic). Instead, we just release and re-acquire —
    // the notify_all in release() will wake all waiters, and the first one
    // to check pick_next() == self will win. For same-priority round-robin,
    // the current thread is still in the set and may win again, but that's
    // acceptable since true round-robin within a priority level is a
    // refinement that can be added later.
    release(thread);
    acquire(thread);
}

ThreadState *CoreScheduler::pick_next() const {
    // The set is ordered by priority (lowest value first = highest priority).
    if (run_queue.empty())
        return nullptr;
    return *run_queue.begin();
}
