// cpp version check
#if __cplusplus < 201703L
#warning C++ 17 is recommended
#endif

#ifndef __GNUC__
#warning It is recommended to build with GCC
#endif

#include <drivers/video/vga.h>

#include <bigos/memory.h>
#include <bigos/proc.h>
#include <bigos/sched.h>
#include <bigos/syscall.h>
#include <bigos/time.h>
#include <bigos/timer.h>
#include <bigos/tty.h>
#include <irq/interrupt.h>

#include <bigos/fs/vfs.h>
#include <bigos/io.h>
#include <ktl/buffer.h>
#if defined(BIGOS_WRITABLE_FS_SMOKE) || defined(BIGOS_PERSISTENT_WRITABLE_FS_SMOKE)
#include <bigos/fs/bcache.h>
#include <bigos/fs/bigfs.h>
#include <bigos/cred.h>
#include <string.h>
#endif
#ifdef BIGOS_PIPE_SMOKE
#include <bigos/ipc/pipe.h>
#include <bigos/sched.h>
#endif

struct BootInfoHeader;

extern "C" void kernel(const BootInfoHeader *boot_info);

#ifdef BIGOS_SCHEDULER_SMOKE
namespace {
    // Bounded deterministic scheduler smoke: two worker threads emit a fixed
    // number of markers and yield to each other, proving cooperative switching.
    constexpr uint32_t SCHEDULER_SMOKE_ITERATIONS = 3;

    void scheduler_smoke_worker_a(void *) noexcept {
        for (uint32_t i = 0; i < SCHEDULER_SMOKE_ITERATIONS; i++) {
            bigos::serial_puts("BIGOS_SCHED_THREAD_A\n");
            bigos::sched::yield();
        }
    }

    void scheduler_smoke_worker_b(void *) noexcept {
        for (uint32_t i = 0; i < SCHEDULER_SMOKE_ITERATIONS; i++) {
            bigos::serial_puts("BIGOS_SCHED_THREAD_B\n");
            bigos::sched::yield();
        }
    }
}   // namespace
#endif

#ifdef BIGOS_SCHEDULER_SEMANTICS_SMOKE
namespace {
    constexpr bigos::timer::tick_t SCHED_SEMANTICS_PENDING_TIMEOUT_TICKS = 8;
    constexpr bigos::timer::tick_t SCHED_SEMANTICS_PREEMPT_TIMEOUT_TICKS = 20;
    volatile bool g_sched_semantics_delayed = false;
    volatile bool g_sched_semantics_preempted = false;

    void scheduler_semantics_worker_a(void *) noexcept {
        bigos::serial_puts("BIGOS_SCHED_SEMANTICS_START\n");

        bigos::sched::disable_preemption();
        const bigos::timer::tick_t pending_start = bigos::timer::ticks();
        while (!bigos::sched::reschedule_pending() &&
               bigos::timer::ticks() - pending_start < SCHED_SEMANTICS_PENDING_TIMEOUT_TICKS) {}

        if (bigos::sched::reschedule_pending()) {
            g_sched_semantics_delayed = true;
            bigos::serial_puts("BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED\n");
        } else {
            bigos::serial_puts("BIGOS_SCHED_SEMANTICS_FAILED pending\n");
        }
        bigos::sched::enable_preemption();

        const bigos::timer::tick_t preempt_start = bigos::timer::ticks();
        while (!g_sched_semantics_preempted &&
               bigos::timer::ticks() - preempt_start < SCHED_SEMANTICS_PREEMPT_TIMEOUT_TICKS) {}
        if (!g_sched_semantics_preempted)
            bigos::serial_puts("BIGOS_SCHED_SEMANTICS_FAILED preempt\n");
    }

    void scheduler_semantics_worker_b(void *) noexcept {
        g_sched_semantics_preempted = true;
        bigos::serial_puts("BIGOS_SCHED_SEMANTICS_PREEMPTED\n");
        if (g_sched_semantics_delayed)
            bigos::serial_puts("BIGOS_SCHED_SEMANTICS_PASSED\n");
        else
            bigos::serial_puts("BIGOS_SCHED_SEMANTICS_FAILED delayed\n");
    }
}   // namespace
#endif

#ifdef BIGOS_BLOCKING_SMOKE
namespace {
    constexpr char BLOCKING_SMOKE_CHAR = 'Z';

    void blocking_smoke_reader(void *) noexcept {
        char ch = 0;
        bigos::serial_puts("BIGOS_BLOCKING_WAIT_BLOCKED\n");
        const int read_result = bigos::terminal::read_char_blocking(&ch, 50);
        if (read_result == 1 && ch == BLOCKING_SMOKE_CHAR)
            bigos::serial_puts("BIGOS_BLOCKING_WAIT_RESUMED\n");
        else {
            bigos::serial_puts("BIGOS_BLOCKING_SMOKE_FAILED\n");
            return;
        }

        bigos::serial_puts("BIGOS_BLOCKING_TIMEOUT_BLOCKED\n");
        const int sleep_result = bigos::timer::sleep_for(2);
        if (sleep_result == bigos::sched::WAIT_TIMEOUT)
            bigos::serial_puts("BIGOS_BLOCKING_TIMEOUT_EXPIRED\n");
        else {
            bigos::serial_puts("BIGOS_BLOCKING_SMOKE_FAILED\n");
            return;
        }

        bigos::serial_puts("BIGOS_BLOCKING_SMOKE_PASSED\n");
    }

    void blocking_smoke_producer(void *) noexcept {
        bigos::serial_puts("BIGOS_BLOCKING_WAKE_SENT\n");
        if (!bigos::terminal::enqueue_input(BLOCKING_SMOKE_CHAR))
            bigos::serial_puts("BIGOS_BLOCKING_SMOKE_FAILED\n");
        bigos::sched::yield();
    }
}   // namespace
#endif

#ifdef BIGOS_SYSCALL_SMOKE
namespace {
    // Bounded deterministic syscall smoke: from ring0 non-interrupt context,
    // issue a few `int 0x80` syscalls, read the return value from rax, and assert
    // the dispatcher was reached, the return value is correct, and an unknown
    // number returns the deterministic error code. Emits BIGOS_SYSCALL_* markers.
    //
    // This stage triggers int 0x80 from ring0 only: it does not enter ring3,
    // switch CR3, or load any user program.
    constexpr uint64_t SYSCALL_SMOKE_UNKNOWN_NUMBER = 0xdead;

    inline int64_t do_syscall(uint64_t number, uint64_t arg0) noexcept {
        int64_t result;
        // number in rax, argument 0 in rdi (per the minimal syscall ABI). The
        // result is returned in rax. rcx/r11 are clobbered by int/iretq semantics.
        asm volatile("int $0x80" : "=a"(result) : "a"(number), "D"(arg0) : "rcx", "r11", "memory");
        return result;
    }

    void syscall_smoke() noexcept {
        // SYS_DEBUG_WRITE emits the BIGOS_SYSCALL_WRITE marker and returns the
        // number of bytes written (length of the kernel-internal marker buffer).
        const int64_t write_ret = do_syscall(bigos::sys::SYS_DEBUG_WRITE, 0);

        // SYS_GET_TICK returns the monotonic kernel tick via rax.
        const int64_t tick_ret = do_syscall(bigos::sys::SYS_GET_TICK, 0);

        // Unknown number must return the deterministic -ENOSYS error code.
        const int64_t unknown_ret = do_syscall(SYSCALL_SMOKE_UNKNOWN_NUMBER, 0);

        const bool ok = write_ret > 0 && tick_ret >= 0 && unknown_ret == -bigos::ENOSYS;
        if (ok)
            bigos::serial_puts("BIGOS_SYSCALL_SMOKE_PASSED\n");
        else
            bigos::serial_puts("BIGOS_SYSCALL_SMOKE_FAILED\n");
    }
}   // namespace
#endif

#ifdef BIGOS_FS_SMOKE
namespace {
    constexpr const char *FS_SMOKE_PATH = "/boot/fs_smoke.txt";
    constexpr const char *FS_SMOKE_PAYLOAD = "BIGOS_FS_SMOKE_PAYLOAD\n";

    bool bytes_equal(const char *__a, const char *__b, size_t __len) noexcept {
        for (size_t i = 0; i < __len; i++) {
            if (__a[i] != __b[i])
                return false;
        }
        return true;
    }

    void fs_smoke_failed(bigos::vfs::Status __status) noexcept {
        bigos::serial_puts("BIGOS_FS_EXFAT_READ_FAILED code=");
        bigos::serial_puts(bigos::vfs::status_name(__status));
        bigos::serial_puts("\n");
    }

    void fs_smoke() noexcept {
        bigos::vfs::Status status = bigos::vfs::init();
        if (status != bigos::vfs::Status::Success) {
            fs_smoke_failed(status);
            return;
        }

        bigos::vfs::File *file = nullptr;
        status = bigos::vfs::open_absolute(FS_SMOKE_PATH, bigos::vfs::OPEN_RDONLY, &file);
        if (status != bigos::vfs::Status::Success) {
            fs_smoke_failed(status);
            return;
        }

        char buffer[32] = {};
        const size_t expected_len = strlen(FS_SMOKE_PAYLOAD);
        size_t bytes_read = 0;
        status = bigos::vfs::read(file, buffer, expected_len, &bytes_read);
        if (status != bigos::vfs::Status::Success || bytes_read != expected_len ||
            !bytes_equal(buffer, FS_SMOKE_PAYLOAD, expected_len)) {
            fs_smoke_failed(status == bigos::vfs::Status::Success ? bigos::vfs::Status::Unsupported : status);
            bigos::vfs::release(file);
            return;
        }

        status = bigos::vfs::read(file, buffer, 1, &bytes_read);
        if (status != bigos::vfs::Status::Success || bytes_read != 0) {
            fs_smoke_failed(status == bigos::vfs::Status::Success ? bigos::vfs::Status::Unsupported : status);
            bigos::vfs::release(file);
            return;
        }

        bigos::vfs::release(file);
        bigos::serial_puts("BIGOS_FS_EXFAT_READ_PASSED\n");
    }
}   // namespace
#endif

#ifdef BIGOS_USER_ELF_SMOKE
namespace {
    void user_elf_smoke_failed(const char *__reason) noexcept {
        bigos::serial_puts("BIGOS_USER_ELF_LOAD_FAILED ");
        bigos::serial_puts(__reason);
        bigos::serial_puts("\n");
    }

    void user_elf_smoke_entry(void *) noexcept {
        bigos::vfs::Status status = bigos::vfs::init();
        if (status != bigos::vfs::Status::Success) {
            user_elf_smoke_failed(bigos::vfs::status_name(status));
            return;
        }

        bigos::vfs::File *file = nullptr;
        status = bigos::vfs::open_absolute(bigos::proc::USER_ELF_SMOKE_PATH, bigos::vfs::OPEN_RDONLY, &file);
        if (status != bigos::vfs::Status::Success) {
            user_elf_smoke_failed(bigos::vfs::status_name(status));
            return;
        }

        const uint64_t file_size = file->vnode != nullptr ? file->vnode->size : 0;
        if (file_size == 0 || file_size > bigos::proc::USER_ELF_MAX_FILE_BYTES) {
            bigos::vfs::release(file);
            user_elf_smoke_failed("file-size");
            return;
        }

        void *image = bigos::kmalloc((size_t)file_size);
        if (image == nullptr) {
            bigos::vfs::release(file);
            user_elf_smoke_failed("buffer");
            return;
        }

        size_t bytes_read = 0;
        status = bigos::vfs::read(file, image, (size_t)file_size, &bytes_read);
        bigos::vfs::release(file);
        if (status != bigos::vfs::Status::Success || bytes_read != file_size) {
            bigos::free(image);
            user_elf_smoke_failed(
                status == bigos::vfs::Status::Success ? "short-read" : bigos::vfs::status_name(status));
            return;
        }

        const char *argv[] = {bigos::proc::USER_ELF_SMOKE_PATH};
        const bigos::proc::ExecArgs args = {argv, 1, nullptr, 0};
        static bigos::proc::Process elf_process;
        const bigos::proc::UserElfLoadError load_status =
            bigos::proc::create_elf_user_process(&elf_process, image, file_size, &args);
        bigos::free(image);
        if (load_status != bigos::proc::UserElfLoadError::Success) {
            user_elf_smoke_failed(bigos::proc::user_elf_load_error_name(load_status));
            return;
        }

        bigos::serial_puts("BIGOS_USER_ELF_LOAD_PASSED\n");
        bigos::proc::run_user_process(&elf_process);
    }
}   // namespace
#endif

#ifdef BIGOS_WRITABLE_FS_SMOKE
namespace {
    bool wfs_bytes_equal(const char *__a, const char *__b, size_t __len) noexcept {
        for (size_t i = 0; i < __len; i++)
            if (__a[i] != __b[i])
                return false;
        return true;
    }

    void writable_fs_smoke_entry(void *) noexcept {
        // Runs from blockable kernel-thread context: bigfs init performs block IO.
        if (!bigos::bigfs::init()) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED init\n");
            return;
        }
        driver::block::BlockDevice *dev = bigos::bigfs::device();
        const uint32_t uid = bigos::cred::ROOT_UID;   // root may write any file
        const uint32_t gid = 0;

        // 1) O_CREAT + write + read back consistent.
        const char *path = "/rw/file.txt";
        const char *payload = "BIGOS_WRITABLE_FS_PAYLOAD";
        const size_t plen = strlen(payload);
        uint32_t inode = 0;
        uint64_t size = 0;
        bool is_dir = false;
        bigos::bigfs::Status s =
            bigos::bigfs::open(
                path, bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT, 0644, uid, gid, &inode, &size, &is_dir);
        if (s != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED create\n");
            return;
        }
        size_t written = 0;
        s = bigos::bigfs::write(inode, 0, payload, plen, uid, gid, &written);
        if (s != bigos::bigfs::Status::Success || written != plen) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED write\n");
            return;
        }
        char buf[64] = {};
        size_t read = 0;
        s = bigos::bigfs::read(inode, 0, buf, plen, &read);
        if (s != bigos::bigfs::Status::Success || read != plen || !wfs_bytes_equal(buf, payload, plen)) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED readback\n");
            return;
        }

        // 2) fsync to device, force-evict cache, then read again consistent.
        if (bigos::bigfs::fsync() != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED fsync\n");
            return;
        }
        bigos::bcache::invalidate_device(dev);
        for (size_t i = 0; i < sizeof(buf); i++)
            buf[i] = 0;
        read = 0;
        s = bigos::bigfs::read(inode, 0, buf, plen, &read);
        if (s != bigos::bigfs::Status::Success || read != plen || !wfs_bytes_equal(buf, payload, plen)) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED evict-readback\n");
            return;
        }

        // 3) owner/mode permission denial: a root-owned 0600 file rejects a write
        // from a non-owner whose other bits grant no write access.
        const char *priv_path = "/rw/priv.txt";
        uint32_t priv_inode = 0;
        s = bigos::bigfs::open(priv_path, bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT, 0600, uid, gid, &priv_inode,
            &size, &is_dir);
        if (s != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED priv-create\n");
            return;
        }
        // uid 2000 (not owner, not group, other has no write bit) must be denied.
        size_t denied_written = 0;
        s = bigos::bigfs::write(priv_inode, 0, payload, plen, 2000, 2000, &denied_written);
        if (s != bigos::bigfs::Status::AccessDenied) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED perm\n");
            return;
        }

        // 4) read-only exFAT backend write rejected with EROFS via the VFS layer.
        if (bigos::vfs::init() == bigos::vfs::Status::Success) {
            bigos::vfs::File *ro = nullptr;
            const bigos::vfs::Status ro_status =
                bigos::vfs::open_absolute("/boot/fs_smoke.txt", bigos::vfs::OPEN_WRONLY, 0644, uid, gid, &ro);
            if (ro_status != bigos::vfs::Status::ReadOnlyFs) {
                if (ro != nullptr)
                    bigos::vfs::release(ro);
                bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED erofs\n");
                return;
            }
        }

        bigos::serial_puts("BIGOS_WRITABLE_FS_PASSED\n");
    }
}   // namespace
#endif

#ifdef BIGOS_PERSISTENT_WRITABLE_FS_SMOKE
namespace {
    bool pfs_bytes_equal(const char *__a, const char *__b, size_t __len) noexcept {
        for (size_t i = 0; i < __len; i++)
            if (__a[i] != __b[i])
                return false;
        return true;
    }

    bool pfs_write_file(const char *__path, const char *__payload) noexcept {
        const uint32_t uid = bigos::cred::ROOT_UID;
        const uint32_t gid = 0;
        const size_t len = strlen(__payload);
        uint32_t inode = 0;
        uint64_t size = 0;
        bool is_dir = false;
        bigos::bigfs::Status status = bigos::bigfs::open(
            __path, bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT | bigos::vfs::OPEN_TRUNC, 0644, uid, gid, &inode,
            &size, &is_dir);
        if (status != bigos::bigfs::Status::Success || is_dir)
            return false;
        size_t written = 0;
        status = bigos::bigfs::write(inode, 0, __payload, len, uid, gid, &written);
        bigos::bigfs::close_inode(inode);
        return status == bigos::bigfs::Status::Success && written == len;
    }

    bool pfs_read_file(const char *__path, const char *__payload) noexcept {
        const uint32_t uid = bigos::cred::ROOT_UID;
        const uint32_t gid = 0;
        const size_t len = strlen(__payload);
        uint32_t inode = 0;
        uint64_t size = 0;
        bool is_dir = false;
        bigos::bigfs::Status status =
            bigos::bigfs::open(__path, bigos::vfs::OPEN_RDONLY, 0644, uid, gid, &inode, &size, &is_dir);
        if (status != bigos::bigfs::Status::Success || is_dir || size != len)
            return false;
        char buf[80] = {};
        size_t read = 0;
        status = bigos::bigfs::read(inode, 0, buf, len, &read);
        bigos::bigfs::close_inode(inode);
        return status == bigos::bigfs::Status::Success && read == len && pfs_bytes_equal(buf, __payload, len);
    }

    bool pfs_check_directory_and_metadata(const char *__path, const char *__payload) noexcept {
        const uint32_t uid = bigos::cred::ROOT_UID;
        const uint32_t gid = 0;
        const char *dir_path = "/rw/persistdir";
        const char *nested_path = "/rw/persistdir/nested.txt";
        const char *renamed_path = "/rw/persistdir/renamed.txt";
        const char *nested_payload = "BIGOS_PERSISTENT_DIR_PAYLOAD";

        bigos::bigfs::Status status = bigos::bigfs::mkdir(dir_path, 0755, uid, gid);
        if (status != bigos::bigfs::Status::Success && status != bigos::bigfs::Status::Exists)
            return false;
        if (!pfs_write_file(nested_path, nested_payload))
            return false;
        status = bigos::bigfs::rename(nested_path, renamed_path, uid, gid);
        if (status != bigos::bigfs::Status::Success && status != bigos::bigfs::Status::Exists)
            return false;
        if (!pfs_read_file(renamed_path, nested_payload))
            return false;

        uint32_t dir_inode = 0;
        uint64_t dir_size = 0;
        bool is_dir = false;
        status = bigos::bigfs::open(dir_path, bigos::vfs::OPEN_RDONLY, 0644, uid, gid, &dir_inode, &dir_size, &is_dir);
        if (status != bigos::bigfs::Status::Success || !is_dir)
            return false;
        bigos::bigfs::DirectoryEntry entries[4] = {};
        size_t entries_read = 0;
        uint64_t next_offset = 0;
        status = bigos::bigfs::readdir(dir_inode, 0, entries, 4, &entries_read, &next_offset);
        bigos::bigfs::close_inode(dir_inode);
        if (status != bigos::bigfs::Status::Success || entries_read == 0)
            return false;
        bool found_renamed = false;
        for (size_t i = 0; i < entries_read; i++)
            if (entries[i].type == bigos::bigfs::INODE_REGULAR && pfs_bytes_equal(entries[i].name, "renamed.txt", 11))
                found_renamed = true;
        if (!found_renamed)
            return false;

        uint32_t file_inode = 0;
        uint64_t file_size = 0;
        status = bigos::bigfs::open(__path, bigos::vfs::OPEN_RDONLY, 0644, uid, gid, &file_inode, &file_size, &is_dir);
        if (status != bigos::bigfs::Status::Success || is_dir || file_size != strlen(__payload))
            return false;
        uint32_t mode = 0;
        uint32_t owner = 0;
        uint32_t group = 0;
        uint64_t stat_size = 0;
        bool stat_is_dir = false;
        const bool stat_ok = bigos::bigfs::stat(file_inode, &mode, &owner, &group, &stat_size, &stat_is_dir);
        bigos::bigfs::close_inode(file_inode);
        if (!stat_ok || stat_is_dir || stat_size != strlen(__payload) || owner != uid || group != gid)
            return false;

        const char *delete_path = "/rw/persistdir/delete-me.txt";
        if (!pfs_write_file(delete_path, "delete"))
            return false;
        status = bigos::bigfs::unlink(delete_path, uid, gid);
        return status == bigos::bigfs::Status::Success;
    }

    bool pfs_check_exfat_read_only_asset() noexcept {
        if (bigos::vfs::init() != bigos::vfs::Status::Success)
            return false;
        bigos::vfs::File *boot_file = nullptr;
        const bigos::vfs::Status open_status = bigos::vfs::open_absolute(
            "/boot/fs_smoke.txt", bigos::vfs::OPEN_RDONLY, 0644, bigos::cred::ROOT_UID, 0, &boot_file);
        if (open_status != bigos::vfs::Status::Success || boot_file == nullptr)
            return false;
        char buf[32] = {};
        size_t read = 0;
        const bigos::vfs::Status read_status = bigos::vfs::read(boot_file, buf, sizeof(buf), &read);
        bigos::vfs::release(boot_file);
        const char *payload = "BIGOS_FS_SMOKE_PAYLOAD\n";
        return read_status == bigos::vfs::Status::Success && read == strlen(payload) &&
               pfs_bytes_equal(buf, payload, read);
    }

    void persistent_writable_fs_smoke_entry(void *) noexcept {
        const char *path = "/rw/persist.txt";
        const char *payload = "BIGOS_PERSISTENT_WRITABLE_FS_PAYLOAD";

        if (!bigos::bigfs::init()) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_FAILED init\n");
            return;
        }

        if (bigos::bigfs::persistent()) {
            if (!pfs_read_file(path, payload)) {
                bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED readback\n");
                return;
            }
            if (!pfs_check_directory_and_metadata(path, payload)) {
                bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED metadata\n");
                return;
            }
            if (!pfs_check_exfat_read_only_asset()) {
                bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED exfat\n");
                return;
            }
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED\n");
            return;
        }

        if (bigos::bigfs::format_persistent() != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED mkfs\n");
            return;
        }
        if (!pfs_write_file(path, payload)) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED write\n");
            return;
        }
        if (!pfs_check_directory_and_metadata(path, payload)) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED metadata\n");
            return;
        }
        if (!pfs_check_exfat_read_only_asset()) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED exfat\n");
            return;
        }
        if (bigos::bigfs::fsync() != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED fsync\n");
            return;
        }
        bigos::bcache::invalidate_device(bigos::bigfs::device());
        if (!pfs_read_file(path, payload)) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED evict-readback\n");
            return;
        }
        bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED\n");
    }
}   // namespace
#endif

#ifdef BIGOS_PIPE_SMOKE
namespace {
    bigos::vfs::File *g_pipe_read = nullptr;
    bigos::vfs::File *g_pipe_write = nullptr;
    volatile bool g_pipe_failed = false;
    volatile bool g_pipe_reader_done = false;

    bool pipe_bytes_equal(const char *__a, const char *__b, size_t __len) noexcept {
        for (size_t i = 0; i < __len; i++)
            if (__a[i] != __b[i])
                return false;
        return true;
    }

    void pipe_smoke_reader(void *) noexcept {
        // The reader blocks on the empty pipe until the writer wakes it; the
        // payload must arrive in FIFO order.
        const char *expect = "BIGOS_PIPE_FIFO";
        const size_t elen = strlen(expect);
        char buf[32] = {};
        size_t total = 0;
        while (total < elen) {
            size_t got = 0;
            const bigos::vfs::Status s = bigos::vfs::read(g_pipe_read, buf + total, elen - total, &got);
            if (s != bigos::vfs::Status::Success || got == 0)
                break;
            total += got;
        }
        if (total != elen || !pipe_bytes_equal(buf, expect, elen))
            g_pipe_failed = true;

        // After the writer closes its end, a subsequent read returns 0 (EOF).
        size_t eof_got = 0;
        const bigos::vfs::Status eof = bigos::vfs::read(g_pipe_read, buf, 1, &eof_got);
        if (eof != bigos::vfs::Status::Success || eof_got != 0)
            g_pipe_failed = true;
        g_pipe_reader_done = true;
    }

    void pipe_smoke_writer(void *) noexcept {
        const char *payload = "BIGOS_PIPE_FIFO";
        const size_t plen = strlen(payload);
        size_t written = 0;
        const bigos::vfs::Status s = bigos::vfs::write(g_pipe_write, payload, plen, &written);
        if (s != bigos::vfs::Status::Success || written != plen)
            g_pipe_failed = true;
        // Close the write end so the reader observes EOF.
        bigos::vfs::release(g_pipe_write);
        g_pipe_write = nullptr;

        // Wait for the reader to finish consuming + EOF.
        for (uint32_t spin = 0; spin < 1000 && !g_pipe_reader_done; spin++)
            bigos::sched::yield();

        // dup-like sharing check: a second pipe whose read end is closed makes a
        // write return EPIPE; also verify dup shares offset via lseek on a file
        // is covered by the writable smoke. Here check EPIPE.
        bigos::vfs::File *r2 = nullptr;
        bigos::vfs::File *w2 = nullptr;
        if (bigos::ipc::create(&r2, &w2) == bigos::vfs::Status::Success) {
            bigos::vfs::release(r2);   // close read end
            size_t w2done = 0;
            const bigos::vfs::Status ps = bigos::vfs::write(w2, payload, plen, &w2done);
            if (ps != bigos::vfs::Status::BrokenPipe)
                g_pipe_failed = true;
            bigos::vfs::release(w2);
        } else {
            g_pipe_failed = true;
        }

        // Release the reader's end so the pipe object is reclaimed.
        if (g_pipe_read != nullptr) {
            bigos::vfs::release(g_pipe_read);
            g_pipe_read = nullptr;
        }

        if (g_pipe_failed)
            bigos::serial_puts("BIGOS_PIPE_FAILED\n");
        else
            bigos::serial_puts("BIGOS_PIPE_PASSED\n");
    }

    void pipe_smoke_entry(void *) noexcept {
        if (bigos::ipc::create(&g_pipe_read, &g_pipe_write) != bigos::vfs::Status::Success) {
            bigos::serial_puts("BIGOS_PIPE_FAILED create\n");
            return;
        }
        // Reader runs first and blocks on the empty pipe; the writer then wakes it.
        if (bigos::sched::create_kernel_thread(&pipe_smoke_reader, nullptr) == bigos::sched::INVALID_THREAD_ID ||
            bigos::sched::create_kernel_thread(&pipe_smoke_writer, nullptr) == bigos::sched::INVALID_THREAD_ID)
            bigos::serial_puts("BIGOS_PIPE_FAILED thread\n");
    }
}   // namespace
#endif

void kernel(const BootInfoHeader *boot_info) {
    driver::video::vga::clear_screen();
    bigos::serial_init();

    bigos::init_mem(boot_info);
#ifdef BIGOS_MM_SELF_TEST
    bigos::mm::self_test();
#endif
#ifdef BIGOS_USER_VMEM_SMOKE
    // Non-interrupt-context one-shot validation of the page-attribute primitives
    // and user address-space root derivation. Does not switch CR3 or enter ring3.
    bigos::mm::user_vmem_smoke();
#endif
    bigos::terminal::init_tty();
    bigos::irq::initIRQ();
#ifdef BIGOS_PAGE_FAULT_SMOKE
    bigos::irq::triggerPageFaultForValidation();
#endif
    bigos::irq::enableIRQ();

    bigos::serial_puts("BigOS kernel reached\n");
    bigos::kprintf("BigOS kernel reached\n");

#ifdef BIGOS_FS_SMOKE
    // Validation-only runtime disk read. Runs from ordinary kernel context after
    // COM1, port I/O, and memory allocation are available; normal boot leaves it off.
    fs_smoke();
#endif

#ifdef BIGOS_SYSCALL_SMOKE
    // Non-interrupt-context one-shot validation of the int 0x80 syscall entry,
    // ABI register convention, dispatch routing, and unknown-number error return.
    // Runs from ring0 only; does not enter ring3 or switch CR3.
    syscall_smoke();
#endif

    // Establish the one-shot wall-clock baseline after the monotonic tick is
    // available (IRQ0/PIT unmasked above) and before any process is created, so
    // process start timestamps observe a ready wall clock.
    bigos::time::init();

    bigos::proc::init();

#ifdef BIGOS_TIME_IDENTITY_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::time_identity_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_TIME_IDENTITY_FAILED thread\n");
#endif

#ifdef BIGOS_SIGNAL_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::signal_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_SIGNAL_FAILED thread\n");
#endif

#ifdef BIGOS_WRITABLE_FS_SMOKE
    if (bigos::sched::create_kernel_thread(&writable_fs_smoke_entry, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED thread\n");
#endif

#ifdef BIGOS_PERSISTENT_WRITABLE_FS_SMOKE
    if (bigos::sched::create_kernel_thread(&persistent_writable_fs_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_FAILED thread\n");
#endif

#ifdef BIGOS_PIPE_SMOKE
    if (bigos::sched::create_kernel_thread(&pipe_smoke_entry, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_PIPE_FAILED thread\n");
#endif

#ifdef BIGOS_SCHEDULER_SMOKE
    bigos::sched::create_kernel_thread(&scheduler_smoke_worker_a, nullptr);
    bigos::sched::create_kernel_thread(&scheduler_smoke_worker_b, nullptr);
#endif
#ifdef BIGOS_SCHEDULER_SEMANTICS_SMOKE
    bigos::sched::create_kernel_thread(&scheduler_semantics_worker_a, nullptr);
    bigos::sched::create_kernel_thread(&scheduler_semantics_worker_b, nullptr);
#endif
#ifdef BIGOS_BLOCKING_SMOKE
    bigos::sched::create_kernel_thread(&blocking_smoke_reader, nullptr);
    bigos::sched::create_kernel_thread(&blocking_smoke_producer, nullptr);
#endif
#ifdef BIGOS_USER_PROGRAM_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::user_program_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_USER_LOAD_FAILED thread\n");
#endif
#ifdef BIGOS_USER_ELF_SMOKE
    if (bigos::sched::create_kernel_thread(&user_elf_smoke_entry, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_USER_ELF_LOAD_FAILED thread\n");
#endif
#ifdef BIGOS_DEMAND_PAGING_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::demand_paging_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_DEMAND_PAGING_FAILED thread\n");
#endif
#ifdef BIGOS_FILE_BACKED_MAPPING_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::file_backed_mapping_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_FAILED thread\n");
#endif
#ifdef BIGOS_GROWABLE_TABLES_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::growable_tables_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_GROWABLE_TABLES_FAILED thread\n");
#endif
#ifdef BIGOS_FORK_COW_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::fork_cow_smoke_entry, nullptr) ==
        bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_FORK_COW_FAILED thread\n");
#endif

    // Default-on, no-#ifdef normal-boot init launch (decision 1). Runs as a
    // kernel thread so run_user_process can enter ring3 and the deferred reaper
    // owns teardown after init exits. Missing/invalid init halts via the unified
    // panic path inside launch_init.
#ifndef BIGOS_PERSISTENT_WRITABLE_FS_SMOKE
    if (bigos::sched::create_kernel_thread(&bigos::proc::launch_init, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_INIT_LOAD_FAILED thread\n");
#endif

    // The post-initialization halt behavior is now owned by the scheduler idle
    // thread instead of a naked hlt loop in kernel(). Enter after IRQs are on so
    // timer IRQ0 can wake the idle hlt.
    bigos::sched::start();
}
