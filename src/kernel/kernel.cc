// cpp version check
#if __cplusplus < 201703L
#warning C++ 17 is recommended
#endif

#ifndef __GNUC__
#warning It is recommended to build with GCC
#endif

#include <drivers/video/vga.h>

#include <arch/x86/boot/boot_info.h>
#include <bigos/memory.h>
#include <bigos/proc.h>
#include <bigos/sched.h>
#include <bigos/syscall.h>
#include <bigos/timer.h>
#include <bigos/tty.h>
#include <drivers/block/ata_pio.h>
#include <irq/interrupt.h>

#include <bigos/fs/exfat.h>
#include <bigos/io.h>
#include <ktl/buffer.h>
#include <string.h>

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
               bigos::timer::ticks() - pending_start < SCHED_SEMANTICS_PENDING_TIMEOUT_TICKS) {
        }

        if (bigos::sched::reschedule_pending()) {
            g_sched_semantics_delayed = true;
            bigos::serial_puts("BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED\n");
        } else {
            bigos::serial_puts("BIGOS_SCHED_SEMANTICS_FAILED pending\n");
        }
        bigos::sched::enable_preemption();

        const bigos::timer::tick_t preempt_start = bigos::timer::ticks();
        while (!g_sched_semantics_preempted &&
               bigos::timer::ticks() - preempt_start < SCHED_SEMANTICS_PREEMPT_TIMEOUT_TICKS) {
        }
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

        // Unknown number must return the deterministic SYS_ENOSYS error code.
        const int64_t unknown_ret = do_syscall(SYSCALL_SMOKE_UNKNOWN_NUMBER, 0);

        const bool ok = write_ret > 0 && tick_ret >= 0 && unknown_ret == bigos::sys::SYS_ENOSYS;
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

    void fs_smoke_failed(bigos::fs::FsStatus __status) noexcept {
        bigos::serial_puts("BIGOS_FS_EXFAT_READ_FAILED code=");
        bigos::serial_puts(bigos::fs::status_name(__status));
        bigos::serial_puts("\n");
    }

    void fs_smoke() noexcept {
        driver::block::AtaPioDevice ata = {};
        driver::block::ata_pio_primary_master_init(&ata);

        bigos::fs::Partition partition = {};
        bigos::fs::FsStatus status = bigos::fs::find_exfat_partition(&ata.block, &partition);
        if (status != bigos::fs::FsStatus::Success) {
            fs_smoke_failed(status);
            return;
        }

        bigos::fs::ExfatMount mount = {};
        status = bigos::fs::mount_exfat(&ata.block, &partition, &mount);
        if (status != bigos::fs::FsStatus::Success) {
            fs_smoke_failed(status);
            return;
        }

        bigos::fs::FileMetadata file = {};
        status = bigos::fs::lookup(&mount, FS_SMOKE_PATH, &file);
        if (status != bigos::fs::FsStatus::Success) {
            fs_smoke_failed(status);
            return;
        }

        char buffer[32] = {};
        const size_t expected_len = strlen(FS_SMOKE_PAYLOAD);
        bigos::fs::ReadResult result = bigos::fs::read_file(&mount, &file, 0, buffer, expected_len, sizeof(buffer));
        if (result.status != bigos::fs::FsStatus::Success || result.bytes_read != expected_len ||
            !bytes_equal(buffer, FS_SMOKE_PAYLOAD, expected_len)) {
            fs_smoke_failed(result.status == bigos::fs::FsStatus::Success ? bigos::fs::FsStatus::MalformedFilesystem
                                                                          : result.status);
            return;
        }

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
        driver::block::AtaPioDevice ata = {};
        driver::block::ata_pio_primary_master_init(&ata);

        bigos::fs::Partition partition = {};
        bigos::fs::FsStatus status = bigos::fs::find_exfat_partition(&ata.block, &partition);
        if (status != bigos::fs::FsStatus::Success) {
            user_elf_smoke_failed(bigos::fs::status_name(status));
            return;
        }

        bigos::fs::ExfatMount mount = {};
        status = bigos::fs::mount_exfat(&ata.block, &partition, &mount);
        if (status != bigos::fs::FsStatus::Success) {
            user_elf_smoke_failed(bigos::fs::status_name(status));
            return;
        }

        bigos::fs::FileMetadata file = {};
        status = bigos::fs::lookup(&mount, bigos::proc::USER_ELF_SMOKE_PATH, &file);
        if (status != bigos::fs::FsStatus::Success) {
            user_elf_smoke_failed(bigos::fs::status_name(status));
            return;
        }
        if (file.data_length == 0 || file.data_length > bigos::proc::USER_ELF_MAX_FILE_BYTES) {
            user_elf_smoke_failed("file-size");
            return;
        }

        void *image = bigos::kmalloc((size_t)file.data_length);
        if (image == nullptr) {
            user_elf_smoke_failed("buffer");
            return;
        }

        bigos::fs::ReadResult read = bigos::fs::read_file(
            &mount, &file, 0, image, (size_t)file.data_length, (size_t)file.data_length);
        if (read.status != bigos::fs::FsStatus::Success || read.bytes_read != file.data_length) {
            bigos::free(image);
            user_elf_smoke_failed(read.status == bigos::fs::FsStatus::Success ? "short-read"
                                                                              : bigos::fs::status_name(read.status));
            return;
        }

        const char *argv[] = {bigos::proc::USER_ELF_SMOKE_PATH};
        const bigos::proc::ExecArgs args = {argv, 1, nullptr, 0};
        static bigos::proc::Process elf_process;
        const bigos::proc::UserElfLoadError load_status =
            bigos::proc::create_elf_user_process(&elf_process, image, file.data_length, &args);
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

    bigos::proc::init();

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

    // The post-initialization halt behavior is now owned by the scheduler idle
    // thread instead of a naked hlt loop in kernel(). Enter after IRQs are on so
    // timer IRQ0 can wake the idle hlt.
    bigos::sched::start();
}
