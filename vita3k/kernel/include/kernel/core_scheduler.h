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
#include <list>
#include <mutex>

struct ThreadState;

// Per-core cooperative scheduler with round-robin for same-priority threads.
//
// The run queue is a std::list ordered by priority (lower value = higher
// scheduling priority). Threads with equal priority are round-robined:
// after a thread's quantum expires (release), it is moved to the end of
// its priority group, so the next same-priority thread gets a turn.

class CoreScheduler {
public:
    explicit CoreScheduler(int core_id);

    void acquire(ThreadState *thread);
    void release(ThreadState *thread);
    void add_thread(ThreadState *thread);
    void remove_thread(ThreadState *thread);
    bool is_active(ThreadState *thread) const;
    void yield(ThreadState *thread);

    int get_core_id() const { return core_id; }

private:
    int core_id;
    mutable std::mutex mutex;
    std::condition_variable cv;
    ThreadState *active_thread = nullptr;

    // Ordered by priority. Same-priority threads are round-robined.
    std::list<ThreadState *> run_queue;

    ThreadState *pick_next() const;
};