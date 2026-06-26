// cpp version check
#if __cplusplus < 201703L
#warning C++ 17 is recommended
#endif

#ifndef __GNUC__
#warning It is recommended to build with GCC
#endif

#include <arch/x86/ap_startup.h>
#include <bigos/block_io.h>
#include <bigos/device.h>
#include <bigos/glyph_font.h>
#include <bigos/memory.h>
#include <bigos/percpu.h>
#include <bigos/proc.h>
#include <bigos/sched.h>
#include <bigos/smp_ipi.h>
#include <bigos/syscall.h>
#include <bigos/time.h>
#include <bigos/timer.h>
#include <bigos/tty.h>
#include <irq/interrupt.h>

#include <bigos/fs/vfs.h>
#include <bigos/io.h>
#include <ktl/buffer.h>
#include <drivers/block/ram_block_device.h>
#include <drivers/block/virtio_blk.h>
#include <drivers/net/virtio_net.h>
#include <drivers/pci/config.h>
#include <drivers/pci/msix.h>
#include <drivers/video/vga.h>

#if defined(BIGOS_BLOCK_IO_REQUEST_SMOKE) || defined(BIGOS_WRITABLE_FS_SMOKE) ||                                       \
    defined(BIGOS_PERSISTENT_WRITABLE_FS_SMOKE)
#include <bigos/fs/bcache.h>
#include <bigos/fs/bigfs.h>
#include <bigos/cred.h>
#endif
#if defined(BIGOS_BLOCK_IO_REQUEST_SMOKE) || defined(BIGOS_WRITABLE_FS_SMOKE) ||                                       \
    defined(BIGOS_PERSISTENT_WRITABLE_FS_SMOKE)
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

#ifdef BIGOS_SCHEDULER_SMP_SMOKE
namespace {
    constexpr bigos::timer::tick_t SCHED_SMP_TIMEOUT_TICKS = 40;
    volatile bool g_sched_smp_bsp_ran = false;
    volatile bool g_sched_smp_ap_ran = false;

    void scheduler_smp_bsp_worker(void *) noexcept {
        g_sched_smp_bsp_ran = bigos::cpu::current_cpu_id() == bigos::cpu::BOOTSTRAP_CPU_ID;
        if (g_sched_smp_bsp_ran)
            bigos::serial_puts("BIGOS_SCHED_SMP_BSP_THREAD\n");

        const bigos::timer::tick_t start = bigos::timer::ticks();
        while (!g_sched_smp_ap_ran && bigos::timer::ticks() - start < SCHED_SMP_TIMEOUT_TICKS)
            bigos::sched::yield();

        if (g_sched_smp_bsp_ran && g_sched_smp_ap_ran)
            bigos::serial_puts("BIGOS_SCHED_SMP_PASSED\n");
        else
            bigos::serial_puts("BIGOS_SCHED_SMP_FAILED timeout\n");
    }

    void scheduler_smp_ap_worker(void *) noexcept {
        if (bigos::cpu::current_cpu_id() != bigos::cpu::BOOTSTRAP_CPU_ID) {
            g_sched_smp_ap_ran = true;
            bigos::serial_puts("BIGOS_SCHED_SMP_AP_THREAD\n");
        } else {
            bigos::serial_puts("BIGOS_SCHED_SMP_FAILED placement\n");
        }
    }

    bigos::cpu::CpuId scheduler_smp_target_cpu() noexcept {
        for (bigos::cpu::CpuId id = 1; id < bigos::cpu::MAX_CPUS; id++) {
            if (bigos::cpu::cpu_id_supported(id) && bigos::cpu::cpu_online(id))
                return id;
        }
        return bigos::cpu::MAX_CPUS;
    }
}   // namespace
#endif

#ifdef BIGOS_TLB_SHOOTDOWN_SMOKE
namespace {
    constexpr uint32_t TLB_SHOOTDOWN_SMOKE_TIMEOUT_ITERATIONS = 2000000;
    volatile bool g_tlb_shootdown_smoke_ap_entered = false;
    volatile bool g_tlb_shootdown_smoke_ap_release = false;
    bigos::mm::MmContext *g_tlb_shootdown_smoke_context = nullptr;

    bigos::cpu::CpuId tlb_shootdown_smoke_target_cpu() noexcept {
        for (bigos::cpu::CpuId id = 1; id < bigos::cpu::MAX_CPUS; id++) {
            if (bigos::cpu::cpu_id_supported(id) && bigos::cpu::cpu_online(id))
                return id;
        }
        return bigos::cpu::MAX_CPUS;
    }

    void tlb_shootdown_smoke_ap_worker(void *) noexcept {
        if (g_tlb_shootdown_smoke_context == nullptr || !bigos::mm::enter_mm_context(g_tlb_shootdown_smoke_context)) {
            bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_FAILED ap-enter\n");
            return;
        }

        g_tlb_shootdown_smoke_ap_entered = true;
        bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_AP_RESIDENT\n");
        while (!g_tlb_shootdown_smoke_ap_release)
            asm volatile("pause" ::: "memory");
        bigos::mm::leave_current_mm_context();
    }

    void tlb_shootdown_smoke_bsp_worker(void *) noexcept {
        const bigos::cpu::CpuId target_cpu = tlb_shootdown_smoke_target_cpu();
        if (target_cpu >= bigos::cpu::MAX_CPUS) {
            bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_SKIPPED cpu\n");
            return;
        }

        const uint64_t root = bigos::mm::derive_user_address_space_root();
        if (root == bigos::mm::INVALID_PHYS_ADDR) {
            bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_FAILED root\n");
            return;
        }
        g_tlb_shootdown_smoke_context = bigos::mm::create_mm_context(root);
        if (g_tlb_shootdown_smoke_context == nullptr) {
            (void)bigos::mm::teardown_user_address_space(root);
            bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_FAILED context\n");
            return;
        }

        if (bigos::sched::create_kernel_thread_on_cpu(&tlb_shootdown_smoke_ap_worker, nullptr, target_cpu) ==
            bigos::sched::INVALID_THREAD_ID) {
            bigos::mm::release_mm_context(g_tlb_shootdown_smoke_context);
            g_tlb_shootdown_smoke_context = nullptr;
            (void)bigos::mm::teardown_user_address_space(root);
            bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_FAILED thread\n");
            return;
        }

        uint32_t wait = 0;
        while (!g_tlb_shootdown_smoke_ap_entered && wait++ < TLB_SHOOTDOWN_SMOKE_TIMEOUT_ITERATIONS)
            bigos::sched::yield();
        if (!g_tlb_shootdown_smoke_ap_entered) {
            bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_FAILED resident-timeout\n");
            return;
        }

        bigos::mm::TlbInvalidationRequest request = {
            bigos::mm::TlbInvalidationScope::AddressSpace,
            bigos::mm::TlbInvalidationReason::Generic,
            bigos::mm::mm_context_root(g_tlb_shootdown_smoke_context),
            0,
            0,
            0,
            true,
            g_tlb_shootdown_smoke_context,
        };
        bigos::mm::invalidate_tlb(request);
        bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_PASSED\n");

        g_tlb_shootdown_smoke_ap_release = true;
        wait = 0;
        while (bigos::mm::mm_context_active_cpu_mask(g_tlb_shootdown_smoke_context) != 0 &&
               wait++ < TLB_SHOOTDOWN_SMOKE_TIMEOUT_ITERATIONS)
            bigos::sched::yield();
        bigos::mm::mark_mm_context_dying(g_tlb_shootdown_smoke_context);
        (void)bigos::mm::teardown_user_address_space(root);
        bigos::mm::release_mm_context(g_tlb_shootdown_smoke_context);
        g_tlb_shootdown_smoke_context = nullptr;
    }
}   // namespace
#endif

#ifdef BIGOS_MULTICORE_HARDENING_SMOKE
namespace {
    constexpr uint32_t MULTICORE_HARDENING_TIMEOUT_ITERATIONS = 2000000;
    volatile bool g_multicore_hardening_ap_ready = false;
    volatile bool g_multicore_hardening_remote_wake = false;
    volatile bool g_multicore_hardening_timeout_wake = false;
    volatile bool g_multicore_hardening_release = false;
    volatile bool g_multicore_hardening_finish = false;
    bigos::sched::WaitQueue g_multicore_hardening_wait_queue = {};
    bigos::mm::MmContext *g_multicore_hardening_context = nullptr;

    bigos::cpu::CpuId multicore_hardening_target_cpu() noexcept {
        for (bigos::cpu::CpuId id = 1; id < bigos::cpu::MAX_CPUS; id++) {
            if (bigos::cpu::cpu_id_supported(id) && bigos::cpu::cpu_online(id) &&
                bigos::cpu::slot_for(id).timer_state == bigos::cpu::LocalTimerState::Ready)
                return id;
        }
        return bigos::cpu::MAX_CPUS;
    }

    bool multicore_hardening_release_ready(void *) noexcept {
        return g_multicore_hardening_release;
    }

    void multicore_hardening_ap_worker(void *) noexcept {
        if (bigos::cpu::current_cpu_id() == bigos::cpu::BOOTSTRAP_CPU_ID) {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED placement\n");
            return;
        }
        if (g_multicore_hardening_context == nullptr || !bigos::mm::enter_mm_context(g_multicore_hardening_context)) {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED ap-mm\n");
            return;
        }

        g_multicore_hardening_ap_ready = true;
        bigos::serial_puts("BIGOS_MULTICORE_HARDENING_AP_THREAD\n");

        const int wait_result = bigos::sched::wait_queue_wait_until(
            &g_multicore_hardening_wait_queue, &multicore_hardening_release_ready, nullptr, 40);
        if (wait_result == bigos::sched::WAIT_OK) {
            g_multicore_hardening_remote_wake = true;
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_REMOTE_WAKE\n");
        } else {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED remote-wake\n");
        }

        if (bigos::sched::sleep_for(2) == bigos::sched::WAIT_TIMEOUT) {
            g_multicore_hardening_timeout_wake = true;
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_TIMEOUT_WAKE\n");
        } else {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED timeout-wake\n");
        }

        while (!g_multicore_hardening_finish)
            asm volatile("pause" ::: "memory");
        bigos::mm::leave_current_mm_context();
    }

    void multicore_hardening_bsp_worker(void *) noexcept {
        bigos::sched::init_wait_queue(&g_multicore_hardening_wait_queue);
        const bigos::cpu::CpuId target_cpu = multicore_hardening_target_cpu();
        if (target_cpu >= bigos::cpu::MAX_CPUS) {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_SKIPPED cpu\n");
            return;
        }

        const uint64_t root = bigos::mm::derive_user_address_space_root();
        if (root == bigos::mm::INVALID_PHYS_ADDR) {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED root\n");
            return;
        }
        g_multicore_hardening_context = bigos::mm::create_mm_context(root);
        if (g_multicore_hardening_context == nullptr) {
            (void)bigos::mm::teardown_user_address_space(root);
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED context\n");
            return;
        }

        if (bigos::sched::create_kernel_thread_on_cpu(&multicore_hardening_ap_worker, nullptr, target_cpu) ==
            bigos::sched::INVALID_THREAD_ID) {
            bigos::mm::release_mm_context(g_multicore_hardening_context);
            g_multicore_hardening_context = nullptr;
            (void)bigos::mm::teardown_user_address_space(root);
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED thread\n");
            return;
        }

        uint32_t wait = 0;
        while (!g_multicore_hardening_ap_ready && wait++ < MULTICORE_HARDENING_TIMEOUT_ITERATIONS)
            bigos::sched::yield();
        if (!g_multicore_hardening_ap_ready) {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED ap-timeout\n");
            return;
        }

        const bigos::smp::IpiDeliveryResult nudge =
            bigos::smp::send_ipi(target_cpu, bigos::smp::IpiType::SchedulerNudge);
        if (nudge.status == bigos::smp::IpiDeliveryStatus::Delivered)
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_IPI\n");
        else {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED ipi\n");
            g_multicore_hardening_finish = true;
            return;
        }

        g_multicore_hardening_release = true;
        (void)bigos::sched::wake_one(&g_multicore_hardening_wait_queue);
        wait = 0;
        while ((!g_multicore_hardening_remote_wake || !g_multicore_hardening_timeout_wake) &&
               wait++ < MULTICORE_HARDENING_TIMEOUT_ITERATIONS)
            bigos::sched::yield();
        if (!g_multicore_hardening_remote_wake || !g_multicore_hardening_timeout_wake) {
            bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED scheduler\n");
            g_multicore_hardening_finish = true;
            return;
        }

        bigos::mm::TlbInvalidationRequest request = {
            bigos::mm::TlbInvalidationScope::AddressSpace,
            bigos::mm::TlbInvalidationReason::Generic,
            bigos::mm::mm_context_root(g_multicore_hardening_context),
            0,
            0,
            0,
            true,
            g_multicore_hardening_context,
        };
        bigos::mm::invalidate_tlb(request);
        bigos::serial_puts("BIGOS_MULTICORE_HARDENING_TLB\n");
        bigos::serial_puts("BIGOS_MULTICORE_HARDENING_PASSED\n");

        g_multicore_hardening_finish = true;
        wait = 0;
        while (bigos::mm::mm_context_active_cpu_mask(g_multicore_hardening_context) != 0 &&
               wait++ < MULTICORE_HARDENING_TIMEOUT_ITERATIONS)
            bigos::sched::yield();
        bigos::mm::mark_mm_context_dying(g_multicore_hardening_context);
        (void)bigos::mm::teardown_user_address_space(root);
        bigos::mm::release_mm_context(g_multicore_hardening_context);
        g_multicore_hardening_context = nullptr;
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

#ifdef BIGOS_BLOCK_IO_REQUEST_SMOKE
namespace {
    struct BlockIoSmokeContext {
        uint8_t sector[driver::block::DEFAULT_SECTOR_SIZE];
        driver::block::BlockDevice *peer_device;
        uint32_t recursive_depth;
        bool probe_queue_full;
        bool saw_queue_full;
        bool peer_accepted_under_pressure;
        bool fail_write;
    };

    uint8_t g_block_io_smoke_write_buf[driver::block::DEFAULT_SECTOR_SIZE] = {};
    uint8_t g_block_io_smoke_read_buf[driver::block::DEFAULT_SECTOR_SIZE] = {};
    uint8_t g_block_io_smoke_nested_buf[driver::block::DEFAULT_SECTOR_SIZE] = {};
    uint8_t g_block_io_smoke_peer_buf[driver::block::DEFAULT_SECTOR_SIZE] = {};

    struct BlockIoCompletionSmoke {
        bigos::block_io::Request request;
        bigos::block_io::CompletionToken token;
        driver::block::BlockDevice device;
        uint8_t buffer[driver::block::DEFAULT_SECTOR_SIZE];
        bigos::block_io::Status final_status;
        bigos::block_io::Status producer_status;
    };

    BlockIoCompletionSmoke g_block_io_completion_smoke = {};

    driver::block::BlockStatus block_io_smoke_read(driver::block::BlockDevice *__device, uint64_t __lba,
        uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept {
        if (__device == nullptr || __device->context == nullptr || __lba != 0 || __sector_count != 1 ||
            __dst_len < driver::block::DEFAULT_SECTOR_SIZE)
            return driver::block::BlockStatus::InvalidArgument;
        BlockIoSmokeContext *ctx = (BlockIoSmokeContext *)__device->context;
        if (ctx->probe_queue_full) {
            ctx->recursive_depth++;
            if (ctx->recursive_depth == 1 && ctx->peer_device != nullptr) {
                if (bigos::block_io::read_sync(ctx->peer_device, 0, 1, g_block_io_smoke_peer_buf,
                        sizeof(g_block_io_smoke_peer_buf)) == bigos::block_io::Status::Success)
                    ctx->peer_accepted_under_pressure = true;
            }
            if (ctx->recursive_depth < bigos::block_io::QUEUE_CAPACITY_PER_DEVICE) {
                (void)bigos::block_io::read_sync(
                    __device, 0, 1, g_block_io_smoke_nested_buf, sizeof(g_block_io_smoke_nested_buf));
            } else {
                const bigos::block_io::Status status = bigos::block_io::read_sync(
                    __device, 0, 1, g_block_io_smoke_nested_buf, sizeof(g_block_io_smoke_nested_buf));
                if (status == bigos::block_io::Status::QueueFull)
                    ctx->saw_queue_full = true;
            }
            ctx->recursive_depth--;
        }
        memcpy(__dst, ctx->sector, driver::block::DEFAULT_SECTOR_SIZE);
        return driver::block::BlockStatus::Success;
    }

    driver::block::BlockStatus block_io_smoke_write(driver::block::BlockDevice *__device, uint64_t __lba,
        uint32_t __sector_count, const void *__src, size_t __src_len) noexcept {
        if (__device == nullptr || __device->context == nullptr || __lba != 0 || __sector_count != 1 ||
            __src_len < driver::block::DEFAULT_SECTOR_SIZE)
            return driver::block::BlockStatus::InvalidArgument;
        BlockIoSmokeContext *ctx = (BlockIoSmokeContext *)__device->context;
        if (ctx->fail_write)
            return driver::block::BlockStatus::DeviceError;
        memcpy(ctx->sector, __src, driver::block::DEFAULT_SECTOR_SIZE);
        return driver::block::BlockStatus::Success;
    }

    bigos::block_io::Status block_io_smoke_issue_failure(driver::block::BlockDevice *,
        bigos::block_io::Request *, const bigos::block_io::CompletionToken *) noexcept {
        return bigos::block_io::Status::DeviceError;
    }

    bool block_io_smoke_bcache_round_trip(driver::block::BlockDevice *__ram) noexcept {
        bigos::bcache::BufferBlock *block = bigos::bcache::get(__ram, 9);
        if (block == nullptr)
            return false;
        block->data[0] = 0xa5;
        block->data[1] = 0x5a;
        bigos::bcache::mark_dirty(block);
        if (bigos::bcache::sync(block) != bigos::bcache::Status::Success || block->dirty) {
            bigos::bcache::put(block);
            return false;
        }
        bigos::bcache::put(block);
        if (bigos::bcache::invalidate_device(__ram) != bigos::bcache::Status::Success)
            return false;

        block = bigos::bcache::get(__ram, 9);
        if (block == nullptr)
            return false;
        const bool ok = block->data[0] == 0xa5 && block->data[1] == 0x5a;
        bigos::bcache::put(block);
        return ok;
    }

    bool block_io_smoke_bcache_dirty_failure(driver::block::BlockDevice *__ram) noexcept {
        if (__ram == nullptr || __ram->context == nullptr)
            return false;
        driver::block::RamBlockDevice *ram = (driver::block::RamBlockDevice *)__ram->context;
        bigos::bcache::BufferBlock *block = bigos::bcache::get(__ram, 10);
        if (block == nullptr)
            return false;
        block->data[0] = 0xcc;
        bigos::bcache::mark_dirty(block);
        driver::block::ram_block_set_write_fault(ram, true);
        const bool failed_dirty = bigos::bcache::sync(block) == bigos::bcache::Status::IoError && block->dirty;
        driver::block::ram_block_set_write_fault(ram, false);
        const bool recovered = bigos::bcache::sync(block) == bigos::bcache::Status::Success && !block->dirty;
        bigos::bcache::put(block);
        return failed_dirty && recovered;
    }

    void block_io_completion_producer(void *__arg) noexcept {
        BlockIoCompletionSmoke *ctx = (BlockIoCompletionSmoke *)__arg;
        ctx->producer_status = bigos::block_io::complete_from_irq(&ctx->token, ctx->final_status);
    }

    void block_io_prepare_pending_smoke(
        BlockIoCompletionSmoke *__ctx, bigos::block_io::Operation __operation) noexcept {
        memset(__ctx, 0, sizeof(*__ctx));
        __ctx->device.sector_size = driver::block::DEFAULT_SECTOR_SIZE;
        __ctx->device.total_sectors = 16;
        __ctx->device.context = __ctx;
        __ctx->device.read_impl = &block_io_smoke_read;
        __ctx->device.write_impl = &block_io_smoke_write;
        __ctx->request.device = &__ctx->device;
        __ctx->request.operation = __operation;
        __ctx->request.lba = 0;
        __ctx->request.sector_count = 1;
        __ctx->request.buffer = __ctx->buffer;
        __ctx->request.buffer_len = sizeof(__ctx->buffer);
        __ctx->request.status = bigos::block_io::Status::InvalidRequest;
        __ctx->request.state = bigos::block_io::RequestState::Invalid;
        __ctx->request.queue_slot = UINT32_MAX;
    }

    bool block_io_smoke_edge_failed(const char *__reason) noexcept {
        bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED completion-edges-");
        bigos::serial_puts(__reason);
        bigos::serial_puts("\n");
        return false;
    }

    bool block_io_smoke_completion_wait() noexcept {
        BlockIoCompletionSmoke *ctx = &g_block_io_completion_smoke;
        block_io_prepare_pending_smoke(ctx, bigos::block_io::Operation::Read);
        if (bigos::block_io::arm_pending(&ctx->request, &ctx->token) != bigos::block_io::Status::Success ||
            ctx->request.state != bigos::block_io::RequestState::Pending) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED completion-wait-arm status=");
            bigos::serial_puts(bigos::block_io::status_name(ctx->request.status));
            bigos::serial_puts(" state=");
            bigos::serial_puts(bigos::block_io::request_state_name(ctx->request.state));
            bigos::serial_puts(ctx->request.queued ? " queued=1\n" : " queued=0\n");
            return false;
        }
        ctx->final_status = bigos::block_io::Status::Success;
        ctx->producer_status = bigos::block_io::Status::InvalidRequest;
        if (bigos::sched::create_kernel_thread(&block_io_completion_producer, ctx) == bigos::sched::INVALID_THREAD_ID) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED completion-wait-thread\n");
            return false;
        }
        const bigos::block_io::Status waited = bigos::block_io::wait_pending(&ctx->request, 20);
        return waited == bigos::block_io::Status::Success &&
               ctx->request.state == bigos::block_io::RequestState::CompletedSuccess && !ctx->request.queued;
    }

    bool block_io_smoke_completion_edges() noexcept {
        BlockIoCompletionSmoke *ctx = &g_block_io_completion_smoke;
        bigos::block_io::DiagnosticsSnapshot diagnostics = {};

        bigos::block_io::reset_diagnostics();
        block_io_prepare_pending_smoke(ctx, bigos::block_io::Operation::Write);
        if (bigos::block_io::arm_pending(&ctx->request, &ctx->token) != bigos::block_io::Status::Success)
            return block_io_smoke_edge_failed("arm-device-error");
        if (bigos::block_io::complete_from_irq(&ctx->token, bigos::block_io::Status::DeviceError) !=
            bigos::block_io::Status::Success)
            return block_io_smoke_edge_failed("complete-device-error");
        bigos::block_io::diagnostics_snapshot(&diagnostics);
        if (diagnostics.device_error_count != 1 ||
            diagnostics.last_terminal_reason != bigos::block_io::TerminalReason::DeviceError ||
            ctx->request.terminal_reason != bigos::block_io::TerminalReason::DeviceError)
            return block_io_smoke_edge_failed("diag-device-error");
        if (bigos::block_io::complete_from_irq(&ctx->token, bigos::block_io::Status::Success) !=
            bigos::block_io::Status::CompletionRejected)
            return block_io_smoke_edge_failed("duplicate-reject");
        bigos::block_io::diagnostics_snapshot(&diagnostics);
        if (diagnostics.duplicate_completion_count != 1 ||
            diagnostics.last_rejection_reason != bigos::block_io::CompletionRejectionReason::DuplicateCompletion ||
            ctx->request.rejection_reason != bigos::block_io::CompletionRejectionReason::DuplicateCompletion)
            return block_io_smoke_edge_failed("diag-duplicate");
        if (bigos::block_io::wait_pending(&ctx->request, 0) != bigos::block_io::Status::DeviceError)
            return block_io_smoke_edge_failed("wait-device-error");

        bigos::block_io::reset_diagnostics();
        block_io_prepare_pending_smoke(ctx, bigos::block_io::Operation::Read);
        if (bigos::block_io::arm_pending(&ctx->request, &ctx->token) != bigos::block_io::Status::Success)
            return block_io_smoke_edge_failed("arm-timeout");
        const bigos::block_io::CompletionToken stale = ctx->token;
        if (bigos::block_io::wait_pending(&ctx->request, 1) != bigos::block_io::Status::PendingTimeout)
            return block_io_smoke_edge_failed("wait-timeout");
        bigos::block_io::diagnostics_snapshot(&diagnostics);
        if (diagnostics.timeout_count != 1 || ctx->request.terminal_reason != bigos::block_io::TerminalReason::Timeout)
            return block_io_smoke_edge_failed("diag-timeout");
        if (bigos::block_io::complete_from_irq(&stale, bigos::block_io::Status::Success) !=
            bigos::block_io::Status::CompletionRejected)
            return block_io_smoke_edge_failed("late-reject");
        bigos::block_io::diagnostics_snapshot(&diagnostics);
        if (diagnostics.late_completion_count != 1 ||
            diagnostics.last_rejection_reason != bigos::block_io::CompletionRejectionReason::LateCompletion)
            return block_io_smoke_edge_failed("diag-late");

        block_io_prepare_pending_smoke(ctx, bigos::block_io::Operation::Read);
        if (bigos::block_io::arm_pending(&ctx->request, &ctx->token) != bigos::block_io::Status::Success)
            return block_io_smoke_edge_failed("arm-reuse");
        if (bigos::block_io::complete_from_irq(&stale, bigos::block_io::Status::Success) !=
            bigos::block_io::Status::CompletionRejected)
            return block_io_smoke_edge_failed("reuse-reject");
        bigos::block_io::diagnostics_snapshot(&diagnostics);
        if (diagnostics.slot_reuse_protection_count == 0 || diagnostics.identity_mismatch_count == 0)
            return block_io_smoke_edge_failed("diag-reuse");
        if (bigos::block_io::complete_from_irq(&ctx->token, bigos::block_io::Status::Success) !=
            bigos::block_io::Status::Success)
            return block_io_smoke_edge_failed("complete-reuse");
        if (bigos::block_io::wait_pending(&ctx->request, 0) != bigos::block_io::Status::Success)
            return block_io_smoke_edge_failed("wait-reuse");

        bigos::block_io::reset_diagnostics();
        block_io_prepare_pending_smoke(ctx, bigos::block_io::Operation::Read);
        if (bigos::block_io::arm_pending(&ctx->request, &ctx->token) != bigos::block_io::Status::Success)
            return block_io_smoke_edge_failed("arm-cancel");
        if (bigos::block_io::cancel_pending(&ctx->request) != bigos::block_io::Status::Success ||
            ctx->request.status != bigos::block_io::Status::Cancelled ||
            ctx->request.terminal_reason != bigos::block_io::TerminalReason::Cancelled)
            return block_io_smoke_edge_failed("cancel");
        bigos::block_io::diagnostics_snapshot(&diagnostics);
        if (diagnostics.cancel_count != 1)
            return block_io_smoke_edge_failed("diag-cancel");

        bigos::block_io::reset_diagnostics();
        driver::block::BlockDevice issue_fail = ctx->device;
        issue_fail.issue_impl = &block_io_smoke_issue_failure;
        if (bigos::block_io::read_sync(&issue_fail, 0, 1, ctx->buffer, sizeof(ctx->buffer)) !=
            bigos::block_io::Status::DeviceError)
            return block_io_smoke_edge_failed("issue-failure-status");
        bigos::block_io::diagnostics_snapshot(&diagnostics);
        if (diagnostics.issue_failure_count != 1 ||
            diagnostics.last_terminal_reason != bigos::block_io::TerminalReason::IssueFailure)
            return block_io_smoke_edge_failed("diag-issue-failure");
        return true;
    }

    void block_io_request_smoke() noexcept {
        BlockIoSmokeContext ctx = {};
        driver::block::BlockDevice device = {};
        device.sector_size = driver::block::DEFAULT_SECTOR_SIZE;
        device.total_sectors = 16;
        device.context = &ctx;
        device.read_impl = &block_io_smoke_read;
        device.write_impl = &block_io_smoke_write;

        uint8_t *write_buf = g_block_io_smoke_write_buf;
        uint8_t *read_buf = g_block_io_smoke_read_buf;
        memset(write_buf, 0, driver::block::DEFAULT_SECTOR_SIZE);
        memset(read_buf, 0, driver::block::DEFAULT_SECTOR_SIZE);
        driver::block::BlockDevice *ram_device = bigos::device::block(bigos::device::DeviceRole::RamValidationBlock);
        if (ram_device == nullptr || ram_device == bigos::device::block(bigos::device::DeviceRole::BootBlock) ||
            ram_device == bigos::device::block(bigos::device::DeviceRole::PersistentWritableBlock)) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-publish\n");
            return;
        }

        write_buf[0] = 0x42;
        if (bigos::block_io::write_sync(&device, 0, 1, write_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
            bigos::block_io::Status::Success) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED write\n");
            return;
        }
        if (bigos::block_io::read_sync(&device, 0, 1, read_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
                bigos::block_io::Status::Success ||
            read_buf[0] != 0x42) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED read\n");
            return;
        }
        if (bigos::block_io::read_sync(nullptr, 0, 1, read_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
            bigos::block_io::Status::InvalidRequest) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED invalid\n");
            return;
        }
        driver::block::BlockDevice not_ready = device;
        not_ready.read_impl = nullptr;
        if (bigos::block_io::read_sync(&not_ready, 0, 1, read_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
            bigos::block_io::Status::DeviceNotReady) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED not-ready\n");
            return;
        }
        if (bigos::block_io::read_sync(&device, 0, 1, read_buf, 1) != bigos::block_io::Status::BufferTooSmall) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED buffer\n");
            return;
        }
        if (bigos::block_io::read_sync(&device, device.total_sectors, 1, read_buf,
                driver::block::DEFAULT_SECTOR_SIZE) != bigos::block_io::Status::Overflow) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED range\n");
            return;
        }
        if (bigos::block_io::read_sync(&device, 0, 0, read_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
            bigos::block_io::Status::InvalidRequest) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED zero\n");
            return;
        }

        driver::block::BlockDevice read_only = device;
        read_only.write_impl = nullptr;
        if (bigos::block_io::write_sync(&read_only, 0, 1, write_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
            bigos::block_io::Status::Unsupported) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED readonly\n");
            return;
        }

        ctx.fail_write = true;
        if (bigos::block_io::write_sync(&device, 0, 1, write_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
            bigos::block_io::Status::DeviceError) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED device-error\n");
            return;
        }
        ctx.fail_write = false;

        write_buf[0] = 0x71;
        write_buf[1] = 0x17;
        if (bigos::block_io::write_role_sync(bigos::device::DeviceRole::RamValidationBlock, 2, 1, write_buf,
                driver::block::DEFAULT_SECTOR_SIZE) != bigos::block_io::Status::Success) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-write\n");
            return;
        }
        if (bigos::block_io::read_role_sync(bigos::device::DeviceRole::RamValidationBlock, 2, 1, read_buf,
                driver::block::DEFAULT_SECTOR_SIZE) != bigos::block_io::Status::Success ||
            read_buf[0] != 0x71 || read_buf[1] != 0x17) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-read\n");
            return;
        }
        write_buf[0] = 0x99;
        if (bigos::block_io::write_role_sync(bigos::device::DeviceRole::RamValidationBlock, 2, 1, write_buf, 1) !=
            bigos::block_io::Status::BufferTooSmall) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-buffer\n");
            return;
        }
        if (bigos::block_io::read_role_sync(bigos::device::DeviceRole::RamValidationBlock, 2, 1, read_buf,
                driver::block::DEFAULT_SECTOR_SIZE) != bigos::block_io::Status::Success ||
            read_buf[0] != 0x71) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-unchanged\n");
            return;
        }
        if (bigos::block_io::read_role_sync(bigos::device::DeviceRole::RamValidationBlock, ram_device->total_sectors, 1,
                read_buf, driver::block::DEFAULT_SECTOR_SIZE) != bigos::block_io::Status::Overflow) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-range\n");
            return;
        }
        if (bigos::block_io::read_role_sync(bigos::device::DeviceRole::VgaText, 0, 1, read_buf,
                driver::block::DEFAULT_SECTOR_SIZE) != bigos::block_io::Status::DeviceNotReady) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED role-not-ready\n");
            return;
        }

        ctx.probe_queue_full = true;
        ctx.peer_device = ram_device;
        ctx.peer_accepted_under_pressure = false;
        ctx.saw_queue_full = false;
        ctx.recursive_depth = 0;
        if (bigos::block_io::read_sync(&device, 0, 1, read_buf, driver::block::DEFAULT_SECTOR_SIZE) !=
                bigos::block_io::Status::Success ||
            !ctx.saw_queue_full || !ctx.peer_accepted_under_pressure) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED queue-full\n");
            return;
        }

        if (!block_io_smoke_bcache_round_trip(ram_device)) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-cache\n");
            return;
        }
        if (!block_io_smoke_bcache_dirty_failure(ram_device)) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED ram-cache-dirty\n");
            return;
        }
        if (!block_io_smoke_completion_wait()) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED completion-wait\n");
            return;
        }
        if (!block_io_smoke_completion_edges()) {
            bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED completion-edges\n");
            return;
        }

        bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_PASSED\n");
    }

    void block_io_request_smoke_entry(void *) noexcept {
        block_io_request_smoke();
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

    bool wfs_all_zero(const char *__buf, size_t __len) noexcept {
        for (size_t i = 0; i < __len; i++)
            if (__buf[i] != 0)
                return false;
        return true;
    }

    bool wfs_check_stable_growth(uint32_t __uid, uint32_t __gid) noexcept {
        uint32_t inode = 0;
        uint64_t size = 0;
        bool is_dir = false;
        bigos::bigfs::Status s = bigos::bigfs::open("/rw/growth.txt",
            bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT | bigos::vfs::OPEN_TRUNC, 0644, __uid, __gid, &inode,
            &size, &is_dir);
        if (s != bigos::bigfs::Status::Success || is_dir)
            return false;

        size_t written = 0;
        s = bigos::bigfs::write(inode, 0, "abc", 3, __uid, __gid, &written);
        if (s != bigos::bigfs::Status::Success || written != 3)
            return false;
        s = bigos::bigfs::write(inode, bigos::bigfs::BLOCK_SIZE - 2, "uvwxy", 5, __uid, __gid, &written);
        if (s != bigos::bigfs::Status::Success || written != 5)
            return false;
        s = bigos::bigfs::write(inode, bigos::bigfs::BLOCK_SIZE + 9, "Z", 1, __uid, __gid, &written);
        if (s != bigos::bigfs::Status::Success || written != 1)
            return false;

        char gap[6] = {};
        size_t read = 0;
        s = bigos::bigfs::read(inode, 3, gap, sizeof(gap), &read);
        if (s != bigos::bigfs::Status::Success || read != sizeof(gap) || !wfs_all_zero(gap, sizeof(gap)))
            return false;
        char cross[5] = {};
        s = bigos::bigfs::read(inode, bigos::bigfs::BLOCK_SIZE - 2, cross, sizeof(cross), &read);
        if (s != bigos::bigfs::Status::Success || read != sizeof(cross) || !wfs_bytes_equal(cross, "uvwxy", 5))
            return false;

        if (bigos::bigfs::truncate(inode, 2, __uid, __gid) != bigos::bigfs::Status::Success)
            return false;
        uint32_t mode = 0;
        uint32_t owner = 0;
        uint32_t group = 0;
        uint64_t stat_size = 0;
        uint64_t atime = 0;
        uint64_t mtime = 0;
        uint64_t ctime = 0;
        bool stat_is_dir = false;
        if (!bigos::bigfs::stat(inode, &mode, &owner, &group, &stat_size, &stat_is_dir, &atime, &mtime, &ctime) ||
            stat_size != 2)
            return false;
        s = bigos::bigfs::read(inode, 2, gap, sizeof(gap), &read);
        if (s != bigos::bigfs::Status::Success || read != 0)
            return false;
        if (bigos::bigfs::truncate(inode, 10, __uid, __gid) != bigos::bigfs::Status::Success)
            return false;
        s = bigos::bigfs::read(inode, 2, gap, sizeof(gap), &read);
        if (s != bigos::bigfs::Status::Success || read != sizeof(gap) || !wfs_all_zero(gap, sizeof(gap)))
            return false;
        s = bigos::bigfs::write(inode, bigos::bigfs::MAX_FILE_SIZE, "x", 1, __uid, __gid, &written);
        if (s != bigos::bigfs::Status::NoSpace || written != 0)
            return false;
        bigos::bigfs::close_inode(inode);

        uint32_t reuse_a = 0;
        s = bigos::bigfs::open("/rw/reuse-a", bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT, 0644, __uid, __gid,
            &reuse_a, &size, &is_dir);
        if (s != bigos::bigfs::Status::Success)
            return false;
        char fill[bigos::bigfs::BLOCK_SIZE];
        for (size_t i = 0; i < sizeof(fill); i++)
            fill[i] = 'Q';
        s = bigos::bigfs::write(reuse_a, 0, fill, sizeof(fill), __uid, __gid, &written);
        if (s != bigos::bigfs::Status::Success || written != sizeof(fill))
            return false;
        if (bigos::bigfs::truncate(reuse_a, 0, __uid, __gid) != bigos::bigfs::Status::Success)
            return false;
        bigos::bigfs::close_inode(reuse_a);

        uint32_t reuse_b = 0;
        s = bigos::bigfs::open("/rw/reuse-b", bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT, 0644, __uid, __gid,
            &reuse_b, &size, &is_dir);
        if (s != bigos::bigfs::Status::Success)
            return false;
        if (bigos::bigfs::truncate(reuse_b, 16, __uid, __gid) != bigos::bigfs::Status::Success)
            return false;
        char zeros[16] = {};
        s = bigos::bigfs::read(reuse_b, 0, zeros, sizeof(zeros), &read);
        if (s != bigos::bigfs::Status::Success || read != sizeof(zeros) || !wfs_all_zero(zeros, sizeof(zeros)))
            return false;
        bigos::bigfs::close_inode(reuse_b);

        uint32_t root_inode = 0;
        s = bigos::bigfs::open("/rw", bigos::vfs::OPEN_RDONLY, 0, __uid, __gid, &root_inode, &size, &is_dir);
        if (s != bigos::bigfs::Status::Success || !is_dir)
            return false;
        s = bigos::bigfs::truncate(root_inode, 0, __uid, __gid);
        bigos::bigfs::close_inode(root_inode);
        return s == bigos::bigfs::Status::IsDirectory;
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
        bigos::bigfs::Status s = bigos::bigfs::open(
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
        if (bigos::bcache::invalidate_device(dev) != bigos::bcache::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED evict-writeback\n");
            return;
        }
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
        s = bigos::bigfs::open(
            priv_path, bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT, 0600, uid, gid, &priv_inode, &size, &is_dir);
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

        if (!wfs_check_stable_growth(uid, gid)) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED stable-growth\n");
            return;
        }

        // 4) nested directory tree mutation, enumeration and empty-directory removal.
        s = bigos::bigfs::mkdir("/rw/tree", 0755, uid, gid);
        if (s != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-mkdir\n");
            return;
        }
        s = bigos::bigfs::mkdir("/rw/tree/sub", 0755, uid, gid);
        if (s != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-mkdir-sub\n");
            return;
        }
        s = bigos::bigfs::mkdir("/rw/tree/sub/empty", 0755, uid, gid);
        if (s != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-mkdir-empty\n");
            return;
        }
        uint32_t tree_a = 0;
        s = bigos::bigfs::open("/rw/tree/sub/a.txt", bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT, 0644, uid, gid,
            &tree_a, &size, &is_dir);
        if (s != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-create-a\n");
            return;
        }
        s = bigos::bigfs::write(tree_a, 0, "tree-a", 6, uid, gid, &written);
        bigos::bigfs::close_inode(tree_a);
        if (s != bigos::bigfs::Status::Success || written != 6) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-write-a\n");
            return;
        }
        uint32_t tree_b = 0;
        s = bigos::bigfs::open("/rw/tree/sub/b.txt", bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT, 0644, uid, gid,
            &tree_b, &size, &is_dir);
        if (s != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-create-b\n");
            return;
        }
        s = bigos::bigfs::write(tree_b, 0, "tree-b", 6, uid, gid, &written);
        bigos::bigfs::close_inode(tree_b);
        if (s != bigos::bigfs::Status::Success || written != 6) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-write-b\n");
            return;
        }
        uint32_t tree_dir = 0;
        s = bigos::bigfs::open("/rw/tree/sub", bigos::vfs::OPEN_RDONLY, 0644, uid, gid, &tree_dir, &size, &is_dir);
        if (s != bigos::bigfs::Status::Success || !is_dir) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-open-dir\n");
            return;
        }
        bigos::bigfs::DirectoryEntry entries[4] = {};
        size_t entries_read = 0;
        uint64_t next_offset = 0;
        s = bigos::bigfs::readdir(tree_dir, 0, entries, 4, &entries_read, &next_offset);
        bigos::bigfs::close_inode(tree_dir);
        bool found_a = false;
        bool found_b = false;
        bool found_empty = false;
        for (size_t i = 0; i < entries_read; i++) {
            if (entries[i].type == bigos::vfs::DIRENT_TYPE_FILE && wfs_bytes_equal(entries[i].name, "a.txt", 5))
                found_a = true;
            if (entries[i].type == bigos::vfs::DIRENT_TYPE_FILE && wfs_bytes_equal(entries[i].name, "b.txt", 5))
                found_b = true;
            if (entries[i].type == bigos::vfs::DIRENT_TYPE_DIRECTORY && wfs_bytes_equal(entries[i].name, "empty", 5))
                found_empty = true;
        }
        if (s != bigos::bigfs::Status::Success || !found_a || !found_b || !found_empty) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-readdir\n");
            return;
        }
        if (bigos::bigfs::rmdir("/rw/tree/sub", uid, gid) != bigos::bigfs::Status::NotEmpty) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-rmdir-not-empty\n");
            return;
        }
        if (bigos::bigfs::unlink("/rw/tree/sub/a.txt", uid, gid) != bigos::bigfs::Status::Success ||
            bigos::bigfs::unlink("/rw/tree/sub/b.txt", uid, gid) != bigos::bigfs::Status::Success ||
            bigos::bigfs::rmdir("/rw/tree/sub/empty", uid, gid) != bigos::bigfs::Status::Success ||
            bigos::bigfs::rmdir("/rw/tree/sub", uid, gid) != bigos::bigfs::Status::Success ||
            bigos::bigfs::rmdir("/rw/tree", uid, gid) != bigos::bigfs::Status::Success) {
            bigos::serial_puts("BIGOS_WRITABLE_FS_FAILED tree-cleanup\n");
            return;
        }

        // 5) read-only exFAT backend write rejected with EROFS via the VFS layer.
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

    bool pfs_all_zero(const char *__buf, size_t __len) noexcept {
        for (size_t i = 0; i < __len; i++)
            if (__buf[i] != 0)
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
        bigos::bigfs::Status status =
            bigos::bigfs::open(__path, bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT | bigos::vfs::OPEN_TRUNC, 0644,
                uid, gid, &inode, &size, &is_dir);
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

    bool pfs_write_stable_growth_state() noexcept {
        const uint32_t uid = bigos::cred::ROOT_UID;
        const uint32_t gid = 0;
        uint32_t inode = 0;
        uint64_t size = 0;
        bool is_dir = false;
        bigos::bigfs::Status status = bigos::bigfs::open("/rw/persist-growth",
            bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT | bigos::vfs::OPEN_TRUNC, 0644, uid, gid, &inode, &size,
            &is_dir);
        if (status != bigos::bigfs::Status::Success || is_dir)
            return false;
        size_t written = 0;
        status = bigos::bigfs::write(inode, 0, "abc", 3, uid, gid, &written);
        if (status != bigos::bigfs::Status::Success || written != 3)
            return false;
        status = bigos::bigfs::write(inode, bigos::bigfs::BLOCK_SIZE - 2, "uvwxy", 5, uid, gid, &written);
        if (status != bigos::bigfs::Status::Success || written != 5)
            return false;
        status = bigos::bigfs::write(inode, bigos::bigfs::BLOCK_SIZE + 9, "Z", 1, uid, gid, &written);
        if (status != bigos::bigfs::Status::Success || written != 1)
            return false;
        bigos::bigfs::close_inode(inode);

        status = bigos::bigfs::open("/rw/persist-trunc",
            bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_CREAT | bigos::vfs::OPEN_TRUNC, 0644, uid, gid, &inode, &size,
            &is_dir);
        if (status != bigos::bigfs::Status::Success || is_dir)
            return false;
        status = bigos::bigfs::write(inode, 0, "abcdef", 6, uid, gid, &written);
        if (status != bigos::bigfs::Status::Success || written != 6)
            return false;
        if (bigos::bigfs::truncate(inode, 2, uid, gid) != bigos::bigfs::Status::Success)
            return false;
        if (bigos::bigfs::truncate(inode, 10, uid, gid) != bigos::bigfs::Status::Success)
            return false;
        bigos::bigfs::close_inode(inode);
        return true;
    }

    bool pfs_verify_stable_growth_state() noexcept {
        const uint32_t uid = bigos::cred::ROOT_UID;
        const uint32_t gid = 0;
        uint32_t inode = 0;
        uint64_t size = 0;
        bool is_dir = false;
        bigos::bigfs::Status status =
            bigos::bigfs::open("/rw/persist-growth", bigos::vfs::OPEN_RDONLY, 0, uid, gid, &inode, &size, &is_dir);
        if (status != bigos::bigfs::Status::Success || is_dir || size != bigos::bigfs::BLOCK_SIZE + 10)
            return false;
        char buf[8] = {};
        size_t read = 0;
        status = bigos::bigfs::read(inode, 0, buf, 3, &read);
        if (status != bigos::bigfs::Status::Success || read != 3 || !pfs_bytes_equal(buf, "abc", 3))
            return false;
        status = bigos::bigfs::read(inode, 3, buf, sizeof(buf), &read);
        if (status != bigos::bigfs::Status::Success || read != sizeof(buf) || !pfs_all_zero(buf, sizeof(buf)))
            return false;
        status = bigos::bigfs::read(inode, bigos::bigfs::BLOCK_SIZE - 2, buf, 5, &read);
        if (status != bigos::bigfs::Status::Success || read != 5 || !pfs_bytes_equal(buf, "uvwxy", 5))
            return false;
        bigos::bigfs::close_inode(inode);

        status = bigos::bigfs::open("/rw/persist-trunc", bigos::vfs::OPEN_RDONLY, 0, uid, gid, &inode, &size, &is_dir);
        if (status != bigos::bigfs::Status::Success || is_dir || size != 10)
            return false;
        status = bigos::bigfs::read(inode, 0, buf, 2, &read);
        if (status != bigos::bigfs::Status::Success || read != 2 || !pfs_bytes_equal(buf, "ab", 2))
            return false;
        status = bigos::bigfs::read(inode, 2, buf, sizeof(buf), &read);
        bigos::bigfs::close_inode(inode);
        return status == bigos::bigfs::Status::Success && read == sizeof(buf) && pfs_all_zero(buf, sizeof(buf));
    }

    bool pfs_check_directory_and_metadata(const char *__path, const char *__payload) noexcept {
        const uint32_t uid = bigos::cred::ROOT_UID;
        const uint32_t gid = 0;
        const char *dir_path = "/rw/persistdir";
        const char *nested_path = "/rw/persistdir/nested.txt";
        const char *renamed_path = "/rw/persistdir/renamed.txt";
        const char *tree_path = "/rw/persistdir/tree";
        const char *tree_sub_path = "/rw/persistdir/tree/sub";
        const char *tree_empty_path = "/rw/persistdir/tree/sub/empty";
        const char *tree_a_path = "/rw/persistdir/tree/sub/a.txt";
        const char *tree_b_path = "/rw/persistdir/tree/sub/b.txt";
        const char *nested_payload = "BIGOS_PERSISTENT_DIR_PAYLOAD";

        bigos::bigfs::Status status = bigos::bigfs::mkdir(dir_path, 0755, uid, gid);
        if (status != bigos::bigfs::Status::Success && status != bigos::bigfs::Status::Exists)
            return false;
        status = bigos::bigfs::mkdir(tree_path, 0755, uid, gid);
        if (status != bigos::bigfs::Status::Success && status != bigos::bigfs::Status::Exists)
            return false;
        status = bigos::bigfs::mkdir(tree_sub_path, 0755, uid, gid);
        if (status != bigos::bigfs::Status::Success && status != bigos::bigfs::Status::Exists)
            return false;
        status = bigos::bigfs::mkdir(tree_empty_path, 0755, uid, gid);
        if (status != bigos::bigfs::Status::Success && status != bigos::bigfs::Status::Exists)
            return false;
        if (!pfs_write_file(tree_a_path, "persist-a") || !pfs_write_file(tree_b_path, "persist-b"))
            return false;
        if (bigos::bigfs::rmdir(tree_sub_path, uid, gid) != bigos::bigfs::Status::NotEmpty)
            return false;
        status = bigos::bigfs::rmdir(tree_empty_path, uid, gid);
        if (status != bigos::bigfs::Status::Success && status != bigos::bigfs::Status::NotFound)
            return false;
        if (!pfs_read_file(tree_a_path, "persist-a") || !pfs_read_file(tree_b_path, "persist-b"))
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

        status =
            bigos::bigfs::open(tree_sub_path, bigos::vfs::OPEN_RDONLY, 0644, uid, gid, &dir_inode, &dir_size, &is_dir);
        if (status != bigos::bigfs::Status::Success || !is_dir)
            return false;
        for (size_t i = 0; i < 4; i++)
            entries[i] = {};
        entries_read = 0;
        next_offset = 0;
        status = bigos::bigfs::readdir(dir_inode, 0, entries, 4, &entries_read, &next_offset);
        bigos::bigfs::close_inode(dir_inode);
        if (status != bigos::bigfs::Status::Success)
            return false;
        bool found_a = false;
        bool found_b = false;
        bool found_empty = false;
        for (size_t i = 0; i < entries_read; i++) {
            if (entries[i].type == bigos::vfs::DIRENT_TYPE_FILE && pfs_bytes_equal(entries[i].name, "a.txt", 5))
                found_a = true;
            if (entries[i].type == bigos::vfs::DIRENT_TYPE_FILE && pfs_bytes_equal(entries[i].name, "b.txt", 5))
                found_b = true;
            if (entries[i].type == bigos::vfs::DIRENT_TYPE_DIRECTORY && pfs_bytes_equal(entries[i].name, "empty", 5))
                found_empty = true;
        }
        if (!found_a || !found_b || found_empty)
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
        uint64_t atime = 0;
        uint64_t mtime = 0;
        uint64_t ctime = 0;
        bool stat_is_dir = false;
        const bool stat_ok =
            bigos::bigfs::stat(file_inode, &mode, &owner, &group, &stat_size, &stat_is_dir, &atime, &mtime, &ctime);
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

        bool formatted_from_empty = false;
        if (!bigos::bigfs::init()) {
#ifdef BIGOS_PERSISTENT_WRITABLE_FS_MODERN_BACKEND
            if (bigos::bigfs::format_persistent() == bigos::bigfs::Status::Success) {
                formatted_from_empty = true;
            } else {
                bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_FAILED init\n");
                return;
            }
#else
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_FAILED init\n");
            return;
#endif
        }

        if (bigos::bigfs::persistent() && !formatted_from_empty) {
            if (!pfs_read_file(path, payload)) {
                bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED readback\n");
                return;
            }
            if (!pfs_check_directory_and_metadata(path, payload)) {
                bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED metadata\n");
                return;
            }
            if (!pfs_verify_stable_growth_state()) {
                bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED stable-growth\n");
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
        if (!pfs_write_stable_growth_state()) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED stable-growth\n");
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
        if (bigos::vfs::sync_writable_backend() != bigos::vfs::Status::Success) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED sync\n");
            return;
        }
        if (bigos::bcache::invalidate_device(bigos::bigfs::device()) != bigos::bcache::Status::Success) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED evict-writeback\n");
            return;
        }
        if (!pfs_read_file(path, payload)) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED evict-readback\n");
            return;
        }
        if (!pfs_verify_stable_growth_state()) {
            bigos::serial_puts("BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED stable-evict-readback\n");
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

#if defined(BIGOS_VIRTIO_BLK_SMOKE) || defined(BIGOS_MODERN_STORAGE_BACKEND_SMOKE)
namespace {
    void virtio_blk_smoke_entry(void *) noexcept {
        (void)driver::block::virtio_blk_smoke();
    }
}   // namespace
#endif

#ifdef BIGOS_VIRTIO_NET_SMOKE
namespace {
    void virtio_net_smoke_entry(void *) noexcept {
        (void)driver::net::virtio_net_smoke();
    }
}   // namespace
#endif

#ifdef BIGOS_PCI_CONFIG_VECTOR_SMOKE
namespace {
    void pci_vector_smoke_irq_handler(bigos::irq::InterruptFrame *__frame) noexcept {
        (void)__frame;
    }

    bool pci_config_smoke() noexcept {
        if (!driver::pci::context_allows_config_access()) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED pci-context\n");
            return false;
        }

        driver::pci::DeviceId device = {};
        driver::pci::Status status = driver::pci::probe_device({0xff, 31, 7}, &device);
        if (status != driver::pci::Status::NoDevice) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED no-device\n");
            return false;
        }

        driver::pci::FunctionAddress found = {};
        bool found_device = false;
        for (uint8_t dev = 0; dev < 32 && !found_device; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                const driver::pci::FunctionAddress address = {0, dev, func};
                status = driver::pci::probe_device(address, &device);
                if (status == driver::pci::Status::Ok) {
                    found = address;
                    found_device = true;
                    break;
                }
            }
        }
        if (!found_device) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED probe\n");
            return false;
        }

        uint32_t raw_id = 0;
        uint16_t vendor = 0;
        uint8_t first_byte = 0;
        if (driver::pci::read_config32(found, 0x00, &raw_id) != driver::pci::Status::Ok ||
            driver::pci::read_config16(found, 0x00, &vendor) != driver::pci::Status::Ok ||
            driver::pci::read_config8(found, 0x00, &first_byte) != driver::pci::Status::Ok ||
            vendor != (uint16_t)(raw_id & 0xffffu) || first_byte != (uint8_t)(raw_id & 0xffu)) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED derived-read\n");
            return false;
        }

        driver::pci::Capability caps[driver::pci::MAX_CAPABILITIES] = {};
        uint8_t cap_count = 0;
        status = driver::pci::read_capabilities(found, caps, driver::pci::MAX_CAPABILITIES, &cap_count);
        if (status != driver::pci::Status::Ok && status != driver::pci::Status::BadCapabilityList) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED caps\n");
            return false;
        }

        driver::pci::BarInfo bar = {};
        bool read_bar = false;
        for (uint8_t dev = 0; dev < 32 && !read_bar; dev++) {
            for (uint8_t func = 0; func < 8 && !read_bar; func++) {
                const driver::pci::FunctionAddress address = {0, dev, func};
                if (driver::pci::probe_device(address, &device) != driver::pci::Status::Ok)
                    continue;
                for (uint8_t index = 0; index < driver::pci::BAR_COUNT; index++) {
                    status = driver::pci::read_bar(address, index, &bar);
                    if (status == driver::pci::Status::Ok && bar.kind != driver::pci::BarKind::None) {
                        read_bar = true;
                        break;
                    }
                    if (status != driver::pci::Status::UnsupportedBar)
                        break;
                }
            }
        }
        if (!read_bar) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED bar\n");
            return false;
        }

        bigos::serial_puts("BIGOS_PCI_CONFIG_SMOKE_PASSED\n");
        return true;
    }

    bool vector_alloc_smoke() noexcept {
        if (!bigos::irq::context_allows_vector_allocation()) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED vector-context\n");
            return false;
        }

        uint8_t first = 0;
        if (bigos::irq::allocate_lapic_vector(&pci_vector_smoke_irq_handler, &first) !=
            bigos::irq::VectorAllocStatus::Ok) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED vector-alloc\n");
            return false;
        }
        if (first < bigos::irq::DYNAMIC_LAPIC_VECTOR_FIRST || first > bigos::irq::DYNAMIC_LAPIC_VECTOR_LAST) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED vector-range\n");
            return false;
        }
        if (bigos::irq::release_lapic_vector(first) != bigos::irq::VectorAllocStatus::Ok ||
            bigos::irq::release_lapic_vector(first) != bigos::irq::VectorAllocStatus::NotAllocated) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED vector-release\n");
            return false;
        }

        uint8_t vectors[bigos::irq::DYNAMIC_LAPIC_VECTOR_COUNT] = {};
        for (uint8_t i = 0; i < bigos::irq::DYNAMIC_LAPIC_VECTOR_COUNT; i++) {
            if (bigos::irq::allocate_lapic_vector(&pci_vector_smoke_irq_handler, &vectors[i]) !=
                bigos::irq::VectorAllocStatus::Ok) {
                bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED vector-fill\n");
                return false;
            }
        }

        uint8_t extra = 0;
        if (bigos::irq::allocate_lapic_vector(&pci_vector_smoke_irq_handler, &extra) !=
            bigos::irq::VectorAllocStatus::Exhausted) {
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED vector-exhaust\n");
            return false;
        }

        for (uint8_t i = 0; i < bigos::irq::DYNAMIC_LAPIC_VECTOR_COUNT; i++) {
            if (bigos::irq::release_lapic_vector(vectors[i]) != bigos::irq::VectorAllocStatus::Ok) {
                bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_FAILED vector-cleanup\n");
                return false;
            }
        }

        bigos::serial_puts("BIGOS_VECTOR_ALLOC_SMOKE_PASSED\n");
        return true;
    }

    void pci_config_vector_smoke() noexcept {
        if (pci_config_smoke() && vector_alloc_smoke())
            bigos::serial_puts("BIGOS_PCI_CONFIG_VECTOR_PASSED\n");
    }
}   // namespace
#endif

void kernel(const BootInfoHeader *boot_info) {
    driver::video::vga::clear_screen();
    bigos::serial_init();

    bigos::cpu::init_bootstrap_cpu();
    bigos::init_mem(boot_info);
    bigos::font::init_kernel_glyph_lookup();
    (void)bigos::arch::x86::ap_startup::prepare_trampoline_region();
    bigos::device::init();
    (void)bigos::device::probe_all(bigos::device::ProbeContext::KernelInit);
#ifdef BIGOS_MM_SELF_TEST
    bigos::mm::self_test();
#endif
#ifdef BIGOS_USER_VMEM_SMOKE
    // Non-interrupt-context one-shot validation of the page-attribute primitives
    // and user address-space root derivation. Does not switch CR3 or enter ring3.
    bigos::mm::user_vmem_smoke();
#endif
    bigos::terminal::init_tty();
#ifdef BIGOS_AP_STARTUP_PERCPU_TIMERS
    bigos::cpu::init_topology_from_mp();
#endif
    bigos::irq::initIRQ();
#ifdef BIGOS_AP_STARTUP_PERCPU_TIMERS
    if (bigos::irq::apic_default_delivery_active())
        (void)bigos::arch::x86::ap_startup::start_application_processors();
    else
        bigos::serial_puts("BIGOS_APIC_DEFAULT_BSP_ONLY_FALLBACK\n");
#endif
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

#ifdef BIGOS_PCI_CONFIG_VECTOR_SMOKE
    // Validation-only PCI config and vector lifecycle smoke. Runs from ordinary
    // kernel context after IDT/PIC/LAPIC ownership is initialized and IRQs are
    // enabled, but before user processes are created. It does not program
    // MSI/MSI-X, map BAR MMIO, or expose a user-visible device model.
    pci_config_vector_smoke();
#endif

#ifdef BIGOS_PCI_MSIX_SMOKE
    // Validation-only MSI-X delivery smoke. It configures a synthetic table entry
    // in ordinary kernel context and uses LAPIC fixed IPI as the controlled
    // producer, so the IRQ path remains allocation-free and LAPIC-owned for EOI.
    (void)driver::pci::msix::smoke();
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

#ifdef BIGOS_BLOCK_IO_REQUEST_SMOKE
    // Validation-only kernel-thread smoke over a fake block device and the
    // internal RAM block role. It exercises request validation, queue accounting,
    // framework publication, cache round trips and status propagation without
    // claiming async I/O, user-visible devices or new hardware storage support.
    if (bigos::sched::create_kernel_thread(&block_io_request_smoke_entry, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_BLOCK_IO_REQUEST_FAILED thread\n");
#endif

#if defined(BIGOS_VIRTIO_BLK_SMOKE) || defined(BIGOS_MODERN_STORAGE_BACKEND_SMOKE)
    // Validation-only modern virtio-blk smoke. Probe runs from a blockable
    // kernel thread after IRQ/LAPIC/MSI-X dispatch is initialized; default boot
    // roles remain ATA-backed and do not depend on this internal validation role.
    if (bigos::sched::create_kernel_thread(&virtio_blk_smoke_entry, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_VIRTIO_BLK_FAILED thread\n");
#endif

#ifdef BIGOS_VIRTIO_NET_SMOKE
    // Validation-only modern virtio-net smoke. Default boot, storage,
    // filesystem, and userland remain independent of this internal network role.
    if (bigos::sched::create_kernel_thread(&virtio_net_smoke_entry, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_VIRTIO_NET_FAILED thread\n");
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
#ifdef BIGOS_SCHEDULER_SMP_SMOKE
    const bigos::cpu::CpuId scheduler_smp_cpu = scheduler_smp_target_cpu();
    if (scheduler_smp_cpu < bigos::cpu::MAX_CPUS) {
        bigos::sched::create_kernel_thread(&scheduler_smp_bsp_worker, nullptr);
        if (bigos::sched::create_kernel_thread_on_cpu(&scheduler_smp_ap_worker, nullptr, scheduler_smp_cpu) ==
            bigos::sched::INVALID_THREAD_ID)
            bigos::serial_puts("BIGOS_SCHED_SMP_FAILED thread\n");
    } else {
        bigos::serial_puts("BIGOS_SCHED_SMP_FAILED cpu\n");
    }
#endif
#ifdef BIGOS_TLB_SHOOTDOWN_SMOKE
    if (bigos::sched::create_kernel_thread(&tlb_shootdown_smoke_bsp_worker, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_SMOKE_FAILED thread\n");
#endif
#ifdef BIGOS_MULTICORE_HARDENING_SMOKE
    if (bigos::sched::create_kernel_thread(&multicore_hardening_bsp_worker, nullptr) == bigos::sched::INVALID_THREAD_ID)
        bigos::serial_puts("BIGOS_MULTICORE_HARDENING_FAILED thread\n");
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
