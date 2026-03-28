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

#include <module/module.h>
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
#include <util/log.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

EXPORT(int, __aeabi_unwind_cpp_pr0) {
    return UNIMPLEMENTED();
}

EXPORT(int, __aeabi_unwind_cpp_pr1) {
    return UNIMPLEMENTED();
}

EXPORT(int, __ashldi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __divdi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __divsi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __lshrdi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __moddi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __modsi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __sce_aeabi_idiv1) {
    return UNIMPLEMENTED();
}

EXPORT(int, __sce_aeabi_ldiv1) {
    return UNIMPLEMENTED();
}

EXPORT(int, __udivdi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __udivsi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __umoddi3) {
    return UNIMPLEMENTED();
}

EXPORT(int, __umodsi3) {
    return UNIMPLEMENTED();
}

#pragma push_macro("environ")
#undef environ
EXPORT(int, environ) {
    return UNIMPLEMENTED();
}
#pragma pop_macro("environ")

EXPORT(int, g_ascii_strcasecmp) {
    return UNIMPLEMENTED();
}

EXPORT(int, g_file_vita_get_current_dir) {
    return UNIMPLEMENTED();
}

EXPORT(int, g_file_vita_get_full_path) {
    return UNIMPLEMENTED();
}

EXPORT(int, g_file_vita_set_current_dir) {
    return UNIMPLEMENTED();
}

EXPORT(int, getenv) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_assertion_message) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_array_append_vals) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_array_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_array_insert_vals) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_array_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ascii_strdown) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ascii_strncasecmp) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ascii_tolower) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ascii_xdigit_value) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_build_path) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_convert) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_dir_close) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_dir_open) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_dir_read_name) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_direct_equal) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_direct_hash) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_error_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_file_get_contents) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_file_open_tmp) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_file_test) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_filename_from_uri) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_filename_from_utf8) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_filename_to_uri) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_find_program_in_path) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_get_charset) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_get_current_dir) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_get_home_dir) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_get_tmp_dir) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_get_user_name) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_getenv) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_destroy) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_foreach) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_foreach_remove) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_foreach_steal) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_insert_replace) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_iter_init) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_iter_next) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_lookup) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_lookup_extended) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_new_full) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_remove) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_hash_table_size) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_alloc) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_append) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_copy) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_delete_link) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_find) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_foreach) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_insert_before) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_length) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_nth) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_nth_data) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_prepend) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_remove) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_remove_link) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_reverse) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_list_sort) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_locale_from_utf8) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_locale_to_utf8) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_log) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_log_set_always_fatal) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_log_set_fatal_mask) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_logv) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_markup_parse_context_end_parse) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_markup_parse_context_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_markup_parse_context_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_markup_parse_context_parse) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_memdup) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_path_get_basename) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_path_get_dirname) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_path_is_absolute) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_print) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_printerr) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_add) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_remove) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_remove_fast) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_remove_index) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_remove_index_fast) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ptr_array_sized_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_queue_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_queue_is_empty) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_queue_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_queue_pop_head) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_queue_push_head) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_set_prgname) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_setenv) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_shell_quote) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_append) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_concat) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_copy) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_delete_link) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_find) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_foreach) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_free_1) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_insert_sorted) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_last) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_length) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_nth) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_nth_data) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_prepend) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_remove) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_slist_reverse) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_snprintf) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_spaced_primes_closest) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_spawn_async_with_pipes) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_str_equal) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_str_has_prefix) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_str_hash) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strchomp) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strchug) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strconcat) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strdup_printf) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strdup_vprintf) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strerror) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strfreev) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_string_append) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_string_append_c) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_string_append_len) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_string_append_printf) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_string_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_string_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_string_printf) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strjoin) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strlcpy) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strndup) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strreverse) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_strsplit) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_timer_destroy) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_timer_elapsed) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_timer_new) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_timer_start) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_timer_stop) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_ucs4_to_utf16) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_unichar_tolower) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_unichar_type) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_unichar_xdigit_value) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_unsetenv) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_usleep) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_utf16_to_ucs4) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_utf16_to_utf8) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_utf8_strdown) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_utf8_to_utf16) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_g_utf8_validate) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_malloc) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_malloc0) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_realloc) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_try_malloc) {
    return UNIMPLEMENTED();
}

EXPORT(int, monoeg_try_realloc) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_alloc_mem) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_alloc_raw) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_app_exit_liveboard) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_code_mem_alloc) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_code_mem_flush_icache) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_code_mem_free) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_code_mem_initialize) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_code_mem_lock) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_code_mem_terminate) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_code_mem_unlock) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_create_semaphore) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_crypto_close) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_crypto_fread) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_crypto_open) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_crypto_read) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_delay_thread) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_delete_semaphore) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_disable_ftz) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_errno_loc) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_free_mem) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_free_prng_provider) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_free_raw) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_get_errnoloc) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_get_prng_provider) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_get_thread_context) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_get_ticks_32) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_get_ticks_64) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_get_ticks_since_111) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_get_win32_filetime) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_getpagesize) {
    return 4096; // Vita page size
}

EXPORT(int, pss_getpid) {
    return 1; // Return a dummy PID
}

EXPORT(int, pss_gettimeofday) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_chstat) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_close) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_dclose) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_dopen) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_getstat) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_lseek) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_mkdir) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_open) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_read) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_remove) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_rename) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_rmdir) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_io_write) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_nanosleep) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_accept) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_bind) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_connect) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_epoll_create) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_epoll_ctl) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_epoll_destroy) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_epoll_wait) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_gethostname) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_getpeername) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_getsockname) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_getsockopt) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_htonl) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_htons) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_init) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_listen) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_ntohl) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_ntohs) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_recv) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_recvfrom) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_resolver_create) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_resolver_start_aton) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_resolver_start_ntoa) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_send) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_sendto) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_setsockopt) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_shutdown) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_socket) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_net_socket_close) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_prng_fill) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_resume_thread) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_set_thread_context) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_set_win32_filetime) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_signal_semaphore) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_supports_fast_tls) {
    // Return 0 = no fast TLS. Mono will use pthread TLS instead.
    return 0;
}

EXPORT(int, pss_suspend_thread) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_threads_initialize) {
    LOG_INFO("pss_threads_initialize called");
    return 0;
}

EXPORT(int, pss_usb_transport_close1) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_usb_transport_close2) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_usb_transport_connect) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_usb_transport_recv) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_usb_transport_send) {
    return UNIMPLEMENTED();
}

EXPORT(int, pss_wait_semaphore) {
    return UNIMPLEMENTED();
}

EXPORT(int, pthread_attr_init) {
    // Guest passes a pointer to pthread_attr_t struct. Just zero-init it.
    return 0; // success
}

EXPORT(int, pthread_attr_setstacksize) {
    // Ignore — Vita3K manages stack sizes via sceKernelCreateThread
    return 0;
}

EXPORT(int, pthread_cleanup_pop_) {
    return 0;
}

EXPORT(int, pthread_cleanup_push_) {
    return 0;
}

EXPORT(int, pthread_cond_broadcast, uint32_t *cond) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.cond_map_mutex);
    uint32_t key = cond ? *cond : 0;
    auto it = ps.condvars.find(key);
    if (it != ps.condvars.end()) {
        it->second->cv.notify_all();
    }
    return 0;
}

EXPORT(int, pthread_cond_destroy, uint32_t *cond) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.cond_map_mutex);
    if (cond) {
        ps.condvars.erase(*cond);
    }
    return 0;
}

EXPORT(int, pthread_cond_init, uint32_t *cond, void *attr) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.cond_map_mutex);
    static uint32_t next_cond_id = 1;
    uint32_t id = next_cond_id++;
    ps.condvars[id] = std::make_shared<KernelState::PthreadState::CondVar>();
    if (cond) *cond = id;
    return 0;
}

EXPORT(int, pthread_cond_signal, uint32_t *cond) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.cond_map_mutex);
    uint32_t key = cond ? *cond : 0;
    auto it = ps.condvars.find(key);
    if (it != ps.condvars.end()) {
        it->second->cv.notify_one();
    }
    return 0;
}

EXPORT(int, pthread_cond_timedwait, uint32_t *cond, uint32_t *mutex_guest, void *abstime) {
    auto &ps = emuenv.kernel.pthread;
    uint32_t cond_key = cond ? *cond : 0;
    uint32_t mutex_key = mutex_guest ? *mutex_guest : 0;

    std::shared_ptr<KernelState::PthreadState::CondVar> cv;
    std::shared_ptr<std::recursive_mutex> mtx;
    {
        std::lock_guard<std::mutex> lock(ps.cond_map_mutex);
        auto it = ps.condvars.find(cond_key);
        if (it != ps.condvars.end()) cv = it->second;
    }
    {
        std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
        auto it = ps.mutexes.find(mutex_key);
        if (it != ps.mutexes.end()) mtx = it->second;
    }
    if (cv && mtx) {
        cv->cv.wait_for(*mtx, std::chrono::milliseconds(100));
    }
    return 0;
}

EXPORT(int, pthread_cond_wait, uint32_t *cond, uint32_t *mutex_guest) {
    auto &ps = emuenv.kernel.pthread;
    uint32_t cond_key = cond ? *cond : 0;
    uint32_t mutex_key = mutex_guest ? *mutex_guest : 0;

    std::shared_ptr<KernelState::PthreadState::CondVar> cv;
    std::shared_ptr<std::recursive_mutex> mtx;
    {
        std::lock_guard<std::mutex> lock(ps.cond_map_mutex);
        auto it = ps.condvars.find(cond_key);
        if (it != ps.condvars.end()) cv = it->second;
    }
    {
        std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
        auto it = ps.mutexes.find(mutex_key);
        if (it != ps.mutexes.end()) mtx = it->second;
    }
    if (cv && mtx) {
        cv->cv.wait(*mtx);
    }
    return 0;
}

EXPORT(int, pthread_create, uint32_t *thread_out, void *attr, Ptr<void> start_routine, Ptr<void> arg) {
    // Mono creates worker threads via pthread_create.
    // We create a Vita thread with entry_point = start_routine,
    // then start it with r0 = arg (the void* parameter).
    const auto entry = Ptr<const void>(start_routine.address());
    constexpr int stack_size = 0x10000; // 64KB
    constexpr int priority = 0xA0;

    auto thread = emuenv.kernel.create_thread(emuenv.mem, "MonoPthread", entry,
                                               priority, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT,
                                               stack_size, nullptr);
    if (!thread) {
        LOG_ERROR("pthread_create: create_thread failed");
        return -1;
    }

    // Start the thread with r0 = arg address (pthread convention)
    int ret = thread->start(static_cast<SceSize>(arg.address()), Ptr<void>(0), true);
    if (ret < 0) {
        LOG_ERROR("pthread_create: start failed: 0x{:X}", ret);
        return -1;
    }

    if (thread_out) {
        *thread_out = static_cast<uint32_t>(thread->id);
    }
    LOG_INFO("pthread_create: created thread MonoPthread (SceUID: {})", thread->id);
    return 0;
}

EXPORT(int, pthread_detach) {
    return 0; // All Vita threads are effectively detached
}

EXPORT(int, pthread_equal, uint32_t t1, uint32_t t2) {
    return t1 == t2 ? 1 : 0;
}

EXPORT(int, pthread_exit) {
    // Thread exits — will be handled by run_loop returning
    auto thread = emuenv.kernel.get_thread(thread_id);
    if (thread) {
        thread->exit(0);
    }
    return 0;
}

EXPORT(int, pthread_getspecific, int key) {
    // Returns the TLS value (as int, which is actually a pointer).
    // POSIX: returns NULL (0) if key not found or not set.
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.tls_mutex);
    auto thread_it = ps.tls_data.find(thread_id);
    if (thread_it != ps.tls_data.end()) {
        auto key_it = thread_it->second.find(key);
        if (key_it != thread_it->second.end()) {
            return static_cast<int>(key_it->second);
        }
    }
    return 0; // NULL — key not set for this thread
}

EXPORT(int, pthread_getspecific_for_thread, int key, SceUID target_thread_id) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.tls_mutex);
    auto thread_it = ps.tls_data.find(target_thread_id);
    if (thread_it != ps.tls_data.end()) {
        auto key_it = thread_it->second.find(key);
        if (key_it != thread_it->second.end()) {
            return static_cast<int>(key_it->second);
        }
    }
    return 0;
}

EXPORT(int, pthread_join, uint32_t thread_handle, void *retval) {
    auto thread = emuenv.kernel.get_thread(static_cast<SceUID>(thread_handle));
    if (thread) {
        std::unique_lock<std::mutex> lock(thread->mutex);
        thread->something_to_do.wait(lock, [&] { return thread->status == ThreadStatus::dormant; });
    }
    return 0;
}

EXPORT(int, pthread_key_create, int *key_out, Ptr<void> destructor) {
    // POSIX: creates a new TLS key, stores it in *key_out
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.tls_mutex);

    if (ps.next_key >= KernelState::PthreadState::MAX_KEYS) {
        LOG_ERROR("pthread_key_create: too many TLS keys");
        return -1; // EAGAIN
    }

    int key = ps.next_key++;
    // Store destructor if provided (we don't call destructors yet but track them)
    if (destructor.address()) {
        ps.key_destructors[key] = reinterpret_cast<void(*)(void*)>(destructor.address());
    }

    if (key_out) {
        *key_out = key;
    }
    LOG_INFO("pthread_key_create: created TLS key {}", key);
    return 0; // success
}

EXPORT(int, pthread_key_delete, int key) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.tls_mutex);
    ps.key_destructors.erase(key);
    // Remove from all threads
    for (auto &t : ps.tls_data) {
        t.second.erase(key);
    }
    return 0;
}

EXPORT(int, pthread_mutex_destroy, uint32_t *mutex_guest) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
    if (mutex_guest) {
        ps.mutexes.erase(*mutex_guest);
    }
    return 0;
}

EXPORT(int, pthread_mutex_init, uint32_t *mutex_guest, void *attr) {
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
    uint32_t id = ps.next_mutex_id++;
    ps.mutexes[id] = std::make_shared<std::recursive_mutex>();
    if (mutex_guest) *mutex_guest = id;
    return 0;
}

EXPORT(int, pthread_mutex_lock, uint32_t *mutex_guest) {
    auto &ps = emuenv.kernel.pthread;
    uint32_t key = mutex_guest ? *mutex_guest : 0;

    // Auto-init static mutexes (PTHREAD_MUTEX_INITIALIZER = 0)
    if (key == 0 && mutex_guest) {
        std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
        key = ps.next_mutex_id++;
        ps.mutexes[key] = std::make_shared<std::recursive_mutex>();
        *mutex_guest = key;
    }

    std::shared_ptr<std::recursive_mutex> mtx;
    {
        std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
        auto it = ps.mutexes.find(key);
        if (it != ps.mutexes.end()) mtx = it->second;
    }
    if (mtx) mtx->lock();
    return 0;
}

EXPORT(int, pthread_mutex_trylock, uint32_t *mutex_guest) {
    auto &ps = emuenv.kernel.pthread;
    uint32_t key = mutex_guest ? *mutex_guest : 0;
    if (key == 0 && mutex_guest) {
        std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
        key = ps.next_mutex_id++;
        ps.mutexes[key] = std::make_shared<std::recursive_mutex>();
        *mutex_guest = key;
    }
    std::shared_ptr<std::recursive_mutex> mtx;
    {
        std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
        auto it = ps.mutexes.find(key);
        if (it != ps.mutexes.end()) mtx = it->second;
    }
    if (mtx && mtx->try_lock()) return 0;
    return 16; // EBUSY
}

EXPORT(int, pthread_mutex_unlock, uint32_t *mutex_guest) {
    auto &ps = emuenv.kernel.pthread;
    uint32_t key = mutex_guest ? *mutex_guest : 0;
    std::shared_ptr<std::recursive_mutex> mtx;
    {
        std::lock_guard<std::mutex> lock(ps.mutex_map_mutex);
        auto it = ps.mutexes.find(key);
        if (it != ps.mutexes.end()) mtx = it->second;
    }
    if (mtx) mtx->unlock();
    return 0;
}

EXPORT(int, pthread_mutexattr_destroy) {
    return 0;
}

EXPORT(int, pthread_mutexattr_init) {
    return 0;
}

EXPORT(int, pthread_mutexattr_settype) {
    return 0; // We always use recursive mutexes
}

EXPORT(int, pthread_self) {
    // Return the current thread's SceUID as the pthread handle
    return static_cast<int>(thread_id);
}

EXPORT(int, pthread_setspecific, int key, uint32_t value) {
    // POSIX: sets the TLS value for the current thread
    auto &ps = emuenv.kernel.pthread;
    std::lock_guard<std::mutex> lock(ps.tls_mutex);
    ps.tls_data[thread_id][key] = value;
    return 0; // success
}

EXPORT(int, pthread_vita_tls_create_np, int *key_out, Ptr<void> destructor) {
    // Vita-specific TLS — same as pthread_key_create
    return export_pthread_key_create(emuenv, thread_id, export_name, key_out, destructor);
}

EXPORT(int, pthread_vita_tls_get_np, int key) {
    return export_pthread_getspecific(emuenv, thread_id, export_name, key);
}

EXPORT(int, pthread_vita_tls_set_np, int key, uint32_t value) {
    return export_pthread_setspecific(emuenv, thread_id, export_name, key, value);
}

EXPORT(int, sched_yield) {
    std::this_thread::yield();
    return 0;
}

EXPORT(int, unlink) {
    return UNIMPLEMENTED();
}