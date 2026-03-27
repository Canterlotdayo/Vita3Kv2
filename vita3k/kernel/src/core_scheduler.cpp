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

CoreScheduler::CoreScheduler(int core_id)
    : core_id(core_id) {
}

void CoreScheduler::acquire(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);

    if (std::find(run_queue.begin(), run_queue.end(), thread) == run_queue.end()) {
        // Insert in priority order, at end of same-priority group
        auto insert_pos = run_queue.end();
        for (auto pos = run_queue.begin(); pos != run_queue.end(); ++pos) {
            if ((*pos)->priority > thread->priority) {
                insert_pos = pos;
                break;
            }
        }
        run_queue.insert(insert_pos, thread);
    }

    cv.wait(lock, [&]() {
        return active_thread == nullptr && pick_next() == thread;
    });

    active_thread = thread;
}

void CoreScheduler::release(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);

    if (active_thread != thread) {
        return;
    }

    active_thread = nullptr;

    // Round-robin: move this thread to the end of its priority group.
    // This ensures the next pick_next() returns a different same-priority
    // thread (if any), preventing starvation.
    auto it = std::find(run_queue.begin(), run_queue.end(), thread);
    if (it != run_queue.end()) {
        run_queue.erase(it);
        auto insert_pos = run_queue.end();
        for (auto pos = run_queue.begin(); pos != run_queue.end(); ++pos) {
            if ((*pos)->priority > thread->priority) {
                insert_pos = pos;
                break;
            }
        }
        run_queue.insert(insert_pos, thread);
    }

    cv.notify_all();
}

void CoreScheduler::add_thread(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);

    // Insert in priority order, at end of same-priority group (fair).
    auto insert_pos = run_queue.end();
    for (auto pos = run_queue.begin(); pos != run_queue.end(); ++pos) {
        if ((*pos)->priority > thread->priority) {
            insert_pos = pos;
            break;
        }
    }
    run_queue.insert(insert_pos, thread);

    if (active_thread == nullptr) {
        cv.notify_all();
    }
}

void CoreScheduler::remove_thread(ThreadState *thread) {
    std::unique_lock<std::mutex> lock(mutex);

    auto it = std::find(run_queue.begin(), run_queue.end(), thread);
    if (it != run_queue.end()) {
        run_queue.erase(it);
    }

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
    release(thread);
    acquire(thread);
}

ThreadState *CoreScheduler::pick_next() const {
    if (run_queue.empty())
        return nullptr;
    return run_queue.front();
}