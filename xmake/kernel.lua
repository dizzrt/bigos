---@diagnostic disable: undefined-global, undefined-field

target("kernel")
set_plat("cross")
set_arch("x86_64")
set_kind("binary")
add_deps("user-init-elf")

-- O2 optimize
set_optimize("faster")

add_includedirs("$(projectdir)/include")
add_includedirs("$(projectdir)/cpp/include")
add_includedirs("$(projectdir)/cpp/libsupc++/include")

add_cxxflags(
    "-mno-sse",
    "-mno-sse2",
    "-mno-mmx",
    "-mcmodel=kernel",
    "-ffreestanding",
    "-mno-red-zone",
    "-fno-rtti",
    "-fno-exceptions"
)

add_asflags(
    "-mno-sse",
    "-mno-sse2",
    "-mno-mmx",
    "-mcmodel=kernel",
    "-ffreestanding",
    "-mno-red-zone",
    "-fno-rtti",
    "-fno-exceptions"
)

add_files("$(projectdir)/kernel/core/*.cc")
add_files("$(projectdir)/kernel/core/bigos/**.cc")
add_files("$(projectdir)/kernel/core/bigos/**.s")
add_files("$(projectdir)/kernel/core/irq/**.cc")
add_files("$(projectdir)/kernel/core/irq/**.s")
add_files("$(projectdir)/kernel/core/sched/**.cc")
add_files("$(projectdir)/kernel/core/sched/**.s")
add_files("$(projectdir)/kernel/core/syscall/**.cc")
add_files("$(projectdir)/kernel/core/signal/**.cc")
add_files("$(projectdir)/kernel/core/terminal/**.cc")
add_files("$(projectdir)/kernel/core/timer/**.cc")
add_files("$(projectdir)/kernel/core/time/**.cc")
add_files("$(projectdir)/kernel/core/fs/**.cc")
add_files("$(projectdir)/kernel/core/ipc/**.cc")
add_files("$(projectdir)/kernel/drivers/**.cc")
add_files("$(projectdir)/kernel/mm/**.cc")
add_files("$(projectdir)/cpp/**.cc")

if has_config("mm_self_test") then
    add_defines("BIGOS_MM_SELF_TEST")
end

if has_config("slab_debug") or has_config("mm_self_test") then
    add_defines("BIGOS_SLAB_DEBUG")
end

if has_config("page_fault_smoke") then
    add_defines("BIGOS_PAGE_FAULT_SMOKE")
end

if has_config("timer_smoke") then
    add_defines("BIGOS_TIMER_SMOKE")
end

if has_config("ap_startup_percpu_timers") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
end

if has_config("scheduler_smp_smoke") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
    add_defines("BIGOS_SCHEDULER_SMP_SMOKE")
end

if has_config("tlb_shootdown_smoke") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
    add_defines("BIGOS_TLB_SHOOTDOWN_SMOKE")
end

if has_config("multicore_hardening_smoke") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
    add_defines("BIGOS_MULTICORE_HARDENING_SMOKE")
end

if has_config("keyboard_smoke") then
    add_defines("BIGOS_KEYBOARD_SMOKE")
end

if has_config("scheduler_smoke") then
    add_defines("BIGOS_SCHEDULER_SMOKE")
end

if has_config("scheduler_semantics_smoke") then
    add_defines("BIGOS_SCHEDULER_SEMANTICS_SMOKE")
end

if has_config("blocking_smoke") then
    add_defines("BIGOS_BLOCKING_SMOKE")
end

if has_config("user_vmem_smoke") then
    add_defines("BIGOS_USER_VMEM_SMOKE")
end

if has_config("syscall_smoke") then
    add_defines("BIGOS_SYSCALL_SMOKE")
end

add_defines("BIGOS_USER_PROCESS")
add_files("$(projectdir)/kernel/core/proc/**.cc")
add_files("$(projectdir)/kernel/core/proc/**.s")

if has_config("user_program_smoke") then
    add_defines("BIGOS_USER_PROGRAM_SMOKE")
end

if has_config("user_elf_smoke") then
    add_defines("BIGOS_USER_ELF_SMOKE")
end

if has_config("fs_smoke") then
    add_defines("BIGOS_FS_SMOKE")
end

if has_config("block_io_request_smoke") then
    add_defines("BIGOS_BLOCK_IO_REQUEST_SMOKE")
end

if has_config("pci_config_vector_smoke") then
    add_defines("BIGOS_PCI_CONFIG_VECTOR_SMOKE")
end

if has_config("pci_msix_smoke") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
    add_defines("BIGOS_PCI_MSIX_SMOKE")
end

if has_config("virtio_blk_smoke") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
    add_defines("BIGOS_VIRTIO_BLK_SMOKE")
end

if has_config("modern_storage_backend_smoke") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
    add_defines("BIGOS_MODERN_STORAGE_BACKEND_SMOKE")
end

if has_config("demand_paging_smoke") then
    add_defines("BIGOS_DEMAND_PAGING_SMOKE")
end

if has_config("file_backed_mapping_smoke") or has_config("shared_readonly_mappings_smoke") then
    add_defines("BIGOS_FILE_BACKED_MAPPING_SMOKE")
end

if has_config("anonymous_lifecycle_smoke") then
    add_defines("BIGOS_ANONYMOUS_LIFECYCLE_SMOKE")
end

if has_config("growable_tables_smoke") then
    add_defines("BIGOS_GROWABLE_TABLES_SMOKE")
end

if has_config("fork_cow_smoke") then
    add_defines("BIGOS_FORK_COW_SMOKE")
end

if has_config("time_identity_smoke") then
    add_defines("BIGOS_TIME_IDENTITY_SMOKE")
end

if has_config("signal_smoke") then
    add_defines("BIGOS_SIGNAL_SMOKE")
end

if has_config("writable_fs_smoke") then
    add_defines("BIGOS_WRITABLE_FS_SMOKE")
end

if has_config("persistent_writable_fs") or has_config("persistent_writable_fs_smoke") then
    add_defines("BIGOS_PERSISTENT_WRITABLE_FS")
end

if has_config("persistent_writable_fs_modern_backend") then
    add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")
    add_defines("BIGOS_PERSISTENT_WRITABLE_FS")
    add_defines("BIGOS_PERSISTENT_WRITABLE_FS_MODERN_BACKEND")
end

if has_config("persistent_writable_fs_smoke") then
    add_defines("BIGOS_PERSISTENT_WRITABLE_FS_SMOKE")
end

if has_config("pipe_smoke") then
    add_defines("BIGOS_PIPE_SMOKE")
end

if has_config("userland_smoke") then
    add_defines("BIGOS_USERLAND_SMOKE")
end

if has_config("filesystem_maturity_smoke") then
    add_defines("BIGOS_USERLAND_SMOKE")
    add_defines("BIGOS_FILESYSTEM_MATURITY_SMOKE")
end

if is_mode("debug") then
    set_symbols("debug")
elseif is_mode("release") then
    set_symbols("hidden")
    set_strip("all")
end

on_link(function(target)
    local runtime_tempdir = path.join("$(builddir)", "temp", "runtime")
    local runtime_srcdir = path.join("$(projectdir)", "kernel", "runtime")
    os.mkdir(runtime_tempdir)

    local crt0_object = path.join(runtime_tempdir, "crt0.o")
    local crti_object = path.join(runtime_tempdir, "crti.o")
    local crtn_object = path.join(runtime_tempdir, "crtn.o")
    os.exec("x86_64-elf-as -c %s -o %s", path.join(runtime_srcdir, "crt0.s"), crt0_object)
    os.exec("x86_64-elf-as -c %s -o %s", path.join(runtime_srcdir, "crti.s"), crti_object)
    os.exec("x86_64-elf-as -c %s -o %s", path.join(runtime_srcdir, "crtn.s"), crtn_object)

    local crtbegin = os.iorun("x86_64-elf-gcc -print-file-name=crtbegin.o"):trim()
    local crtend = os.iorun("x86_64-elf-gcc -print-file-name=crtend.o"):trim()
    if crtbegin == "crtbegin.o" or not os.isfile(crtbegin) then
        raise("unable to resolve crtbegin.o from x86_64-elf-gcc")
    end
    if crtend == "crtend.o" or not os.isfile(crtend) then
        raise("unable to resolve crtend.o from x86_64-elf-gcc")
    end
    local libgcc = os.iorun("x86_64-elf-gcc -print-libgcc-file-name"):trim()
    if libgcc == "libgcc.a" or not os.isfile(libgcc) then
        raise("unable to resolve libgcc.a from x86_64-elf-gcc")
    end
    local libgcc_dir = path.directory(libgcc)

    local objs_table = target:objectfiles()
    table.insert(objs_table, 1, path.translate(crtbegin))
    table.insert(objs_table, 1, path.translate(crti_object))
    table.insert(objs_table, 1, path.translate(crt0_object))
    table.insert(objs_table, path.translate(crtend))
    table.insert(objs_table, path.translate(crtn_object))

    local ld_flags = "-nostdlib -L" .. path.translate(libgcc_dir) .. " -lgcc"
    local ld_script = path.translate("$(projectdir)/link.lds")
    local objs = table.concat(objs_table, " ")
    local output_path = path.translate("$(builddir)/kernel")

    os.exec("x86_64-elf-ld %s -T %s %s -o %s", ld_flags, ld_script, objs, output_path)
end)

on_run(function()
    local bochs_cmd = "bochs"
    if is_host("windows") then
        bochs_cmd = "bochsdbg"
    end

    local bochs_config = "$(projectdir)/test/bochsrc.bxrc"
    os.exec("%s -f %s -q", bochs_cmd, bochs_config)
end)
