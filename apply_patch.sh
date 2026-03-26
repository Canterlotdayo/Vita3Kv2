#!/bin/bash
# KanColle Kai Vita3K Patch Script
# Run from the root of the Vita3K repository
set -e

echo "=== KanColle Kai Vita3K Patch ==="

# Check we're in the right directory
if [ ! -f "vita3k/main.cpp" ]; then
    echo "ERROR: Run this script from the root of the Vita3K repository"
    exit 1
fi

# Step 1: Create SafeConditionVariable header
echo "[1/5] Creating safe_condition_variable.h..."
cat > vita3k/util/include/util/safe_condition_variable.h << 'HEADER_EOF'
#pragma once

#include <condition_variable>
#include <mutex>
#include <system_error>

// Workaround for macOS bug where pthread_cond_wait sporadically returns EINVAL,
// causing std::condition_variable::wait to throw std::system_error.
// This drop-in replacement catches the exception and retries.
// See: https://github.com/graphia-app/graphia/issues/33

struct SafeConditionVariable {
    void notify_one() noexcept { cv.notify_one(); }
    void notify_all() noexcept { cv.notify_all(); }

    void wait(std::unique_lock<std::mutex> &lock) {
        while (true) {
            try {
                cv.wait(lock);
                return;
            } catch (const std::system_error &) {
            }
        }
    }

    template <typename Predicate>
    void wait(std::unique_lock<std::mutex> &lock, Predicate pred) {
        while (!pred()) {
            wait(lock);
        }
    }

    template <typename Rep, typename Period>
    std::cv_status wait_for(std::unique_lock<std::mutex> &lock, const std::chrono::duration<Rep, Period> &rel_time) {
        try {
            return cv.wait_for(lock, rel_time);
        } catch (const std::system_error &) {
            return std::cv_status::no_timeout;
        }
    }

    template <typename Rep, typename Period, typename Predicate>
    bool wait_for(std::unique_lock<std::mutex> &lock, const std::chrono::duration<Rep, Period> &rel_time, Predicate pred) {
        try {
            return cv.wait_for(lock, rel_time, pred);
        } catch (const std::system_error &) {
            return pred();
        }
    }

private:
    std::condition_variable cv;
};
HEADER_EOF

# Step 2: Global replace std::condition_variable -> SafeConditionVariable
# (excluding the wrapper header itself and external/)
echo "[2/5] Replacing std::condition_variable globally..."

find vita3k -type f \( -name "*.h" -o -name "*.cpp" \) \
    ! -path "*/external/*" \
    ! -path "*/safe_condition_variable.h" \
    -exec grep -l "std::condition_variable" {} \; | while read -r file; do
    sed -i.bak 's/std::condition_variable/SafeConditionVariable/g' "$file"
    rm -f "${file}.bak"
    echo "  Patched: $file"
done

# Step 3: Add #include <util/safe_condition_variable.h> where needed
echo "[3/5] Adding includes..."

add_include() {
    local file="$1"
    local after="$2"
    if ! grep -q "safe_condition_variable.h" "$file" 2>/dev/null; then
        sed -i.bak "s|${after}|${after}\n#include <util/safe_condition_variable.h>|" "$file"
        rm -f "${file}.bak"
        echo "  Added include to: $file"
    fi
}

add_include "vita3k/kernel/include/kernel/thread/thread_state.h" "#include <condition_variable>"
add_include "vita3k/kernel/include/kernel/sync_primitives.h" "#include <util/byte_ring_buffer.h>"
add_include "vita3k/renderer/include/renderer/state.h" "#include <threads/queue.h>"
add_include "vita3k/renderer/include/renderer/vulkan/types.h" "#include <vkutil/objects.h>"
add_include "vita3k/renderer/include/renderer/gxm_types.h" "#include <condition_variable>"
add_include "vita3k/threads/include/threads/queue.h" "#include <queue>"
add_include "vita3k/net/include/net/state.h" "#include <thread>"
add_include "vita3k/audio/include/audio/impl/cubeb_audio.h" "#include <condition_variable>"
add_include "vita3k/util/include/util/pool.h" "#include <vector>"
add_include "vita3k/util/include/util/net_utils.h" "#include <regex>"
add_include "vita3k/gui/src/bgm_player.cpp" "#include <io/state.h>"

# Step 4: Patch sceKernelCallAbortHandler
echo "[4/5] Patching sceKernelCallAbortHandler..."
PROCESSMGR="vita3k/modules/SceProcessmgr/SceProcessmgr.cpp"

# Add cpu/functions.h include if not present
if ! grep -q "cpu/functions.h" "$PROCESSMGR"; then
    sed -i.bak 's|#include <kernel/state.h>|#include <cpu/functions.h>\n#include <kernel/state.h>|' "$PROCESSMGR"
    rm -f "${PROCESSMGR}.bak"
fi

# Replace the function
python3 - "$PROCESSMGR" << 'PYEOF'
import sys
f = sys.argv[1]
with open(f, 'r') as fh:
    content = fh.read()

old = '''EXPORT(int, sceKernelCallAbortHandler, uint32_t param1, uint32_t param2) {
    TRACY_FUNC(sceKernelCallAbortHandler, param1, param2);
    return UNIMPLEMENTED();
}'''

new = '''EXPORT(int, sceKernelCallAbortHandler, uint32_t param1, uint32_t param2) {
    TRACY_FUNC(sceKernelCallAbortHandler, param1, param2);

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    const char *tname = thread ? thread->name.c_str() : "unknown";
    LOG_ERROR("=== ABORT HANDLER CALLED ===");
    LOG_ERROR("  Thread: {} (ID: {})", tname, thread_id);
    LOG_ERROR("  Params: 0x{:X}, 0x{:X}", param1, param2);
    if (thread && thread->cpu) {
        auto ctx = save_context(*thread->cpu);
        LOG_ERROR("  CPU context:\\n{}", ctx.description());
    }
    LOG_ERROR("============================");

    return 0;
}'''

if old in content:
    content = content.replace(old, new)
    with open(f, 'w') as fh:
        fh.write(content)
    print("  Patched sceKernelCallAbortHandler")
else:
    print("  sceKernelCallAbortHandler already patched or not found")
PYEOF

# Step 5: Patch sceKernelCallModuleExit
echo "[5/5] Patching sceKernelCallModuleExit..."
LIBKERNEL="vita3k/modules/SceLibKernel/SceLibKernel.cpp"

python3 - "$LIBKERNEL" << 'PYEOF'
import sys
f = sys.argv[1]
with open(f, 'r') as fh:
    content = fh.read()

old = '''EXPORT(int, sceKernelCallModuleExit) {
    TRACY_FUNC(sceKernelCallModuleExit);
    return UNIMPLEMENTED();
}'''

new = '''EXPORT(int, sceKernelCallModuleExit) {
    TRACY_FUNC(sceKernelCallModuleExit);

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    const char *tname = thread ? thread->name.c_str() : "unknown";
    LOG_INFO("=== MODULE EXIT CALLED ===");
    LOG_INFO("  Thread: {} (ID: {})", tname, thread_id);
    if (thread && thread->cpu) {
        auto ctx = save_context(*thread->cpu);
        LOG_INFO("  CPU context:\\n{}", ctx.description());
    }
    LOG_INFO("==========================");

    return 0;
}'''

if old in content:
    content = content.replace(old, new)
    with open(f, 'w') as fh:
        fh.write(content)
    print("  Patched sceKernelCallModuleExit")
else:
    print("  sceKernelCallModuleExit already patched or not found")
PYEOF

# Step 6: Make TTY logs visible
echo "[+] Making TTY logs visible..."
IOFILE="vita3k/io/src/io.cpp"
sed -i.bak 's/LOG_TRACE_IF(log_file_op, "\*\*\* TTY: {}", s);/LOG_WARN("*** TTY: {}", s);/' "$IOFILE"
rm -f "${IOFILE}.bak"

echo ""
echo "=== Patch applied successfully! ==="
echo "Rebuild with: cmake --build build"
