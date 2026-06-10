---@diagnostic disable: undefined-global

set_xmakever("2.7.8")

set_project("bigos")
set_version("0.1.0")

includes("toolchains.lua")

set_languages("c17","cxx17")
set_toolchains("x86_64-elf-gcc")
add_rules("mode.debug", "mode.release")

option("mm_self_test")
    set_default(false)
    set_showmenu(true)
    set_description("enable early memory runtime self-test")
option_end()

option("slab_debug")
    set_default(false)
    set_showmenu(true)
    set_description("enable slab allocator debug guards")
option_end()

option("page_fault_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only page fault trigger")
option_end()

option("timer_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only timer IRQ smoke marker")
option_end()

option("keyboard_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only keyboard IRQ smoke handler")
option_end()

option("scheduler_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only scheduler two-thread smoke")
option_end()

option("scheduler_semantics_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only timer preemption scheduler semantics smoke")
option_end()

option("blocking_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only blocking primitive smoke")
option_end()

option("user_vmem_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only user address space vmem smoke marker")
option_end()

option("syscall_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only ring0 int 0x80 syscall self-test")
option_end()

option("user_program_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only first user program ring3 smoke")
option_end()

option("user_elf_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only filesystem-backed user ELF ring3 smoke")
option_end()

option("fs_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only kernel exFAT filesystem smoke marker")
option_end()

option("demand_paging_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only demand-paging lazy materialization / kill smoke marker")
option_end()

option("growable_tables_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only growable process/fd table smoke marker")
option_end()

option("fork_cow_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only fork/copy-on-write smoke marker")
option_end()

option("time_identity_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only wall-clock time and process identity smoke marker")
option_end()

option("signal_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only minimal signal model smoke marker")
option_end()

add_includedirs("$(projectdir)/include")
add_includedirs("$(projectdir)/cpp/include")
add_includedirs("$(projectdir)/cpp/libsupc++/include")

add_cxxflags("-mno-sse","-mno-sse2", "-mno-mmx", "-mcmodel=kernel", "-ffreestanding", "-mno-red-zone", "-fno-rtti", "-fno-exceptions")

add_asflags("-mno-sse","-mno-sse2", "-mno-mmx", "-mcmodel=kernel", "-ffreestanding", "-mno-red-zone", "-fno-rtti", "-fno-exceptions")

local boot_srcdir = path.join("$(projectdir)", "src", "arch", "x86", "boot")
local boot_bindir = path.join("$(builddir)", "bin", "x86", "boot")
local boot_tempdir = path.join("$(builddir)", "temp", "x86", "boot")

local function run_boot_debug(emulator, serial_log, target_args, process)
    local args = {"tools/boot_debug.py", "run", "--emulator", emulator, "--skip-build", "--serial-log", serial_log}
    local first_arg = target_args[1] == "--" and 2 or 1
    for index = first_arg, #target_args do
        local arg = target_args[index]
        table.insert(args, arg)
    end
    local proc = process.openv("python3", args)
    local _, status = proc:wait()
    proc:close()
    if status ~= 0 then
        error(string.format("boot_debug.py failed with exit code %d", status))
    end
end

target("boot-mbr")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        os.mkdir(boot_bindir)
        os.mkdir(boot_tempdir)
        local object = path.join(boot_tempdir, "mbr.o")
        local output = path.join(boot_bindir, "mbr.bin")
        os.exec("x86_64-elf-as %s -o %s", path.join(boot_srcdir, "mbr.s"), object)
        os.exec("x86_64-elf-ld --oformat binary -nostdlib -e _start -Ttext 0x7c00 %s -o %s", object, output)
        local size = os.filesize(output)
        if size > 512 then
            raise("%s is too large: %d bytes > 512 bytes", output, size)
        end
    end)

target("boot-dbr")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        os.mkdir(boot_bindir)
        os.mkdir(boot_tempdir)
        local object = path.join(boot_tempdir, "dbr_exfat.o")
        local output = path.join(boot_bindir, "dbr.bin")
        os.exec("x86_64-elf-as %s -o %s", path.join(boot_srcdir, "dbr_exfat.s"), object)
        os.exec("x86_64-elf-ld --oformat binary -nostdlib -e _start -Ttext 0x7c00 %s -o %s", object, output)
        local size = os.filesize(output)
        if size > 512 then
            raise("%s is too large: %d bytes > 512 bytes", output, size)
        end
    end)

target("boot-exdbr")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        os.mkdir(boot_bindir)
        os.mkdir(boot_tempdir)
        local object = path.join(boot_tempdir, "exdbr_exfat.o")
        local output = path.join(boot_bindir, "exdbr.bin")
        os.exec("x86_64-elf-as %s -o %s", path.join(boot_srcdir, "exdbr_exfat.s"), object)
        os.exec("x86_64-elf-ld --oformat binary -nostdlib -e _start -Ttext 0x1000 %s -o %s", object, output)
        local size = os.filesize(output)
        if size > 4096 then
            raise("%s is too large: %d bytes > 4096 bytes", output, size)
        end
    end)

target("boot-loader")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        os.mkdir(boot_bindir)
        os.mkdir(boot_tempdir)
        local boot_s_object = path.join(boot_tempdir, "boot.s.o")
        local boot_cc_object = path.join(boot_tempdir, "boot.cc.o")
        local output = path.join(boot_bindir, "boot.bin")

        os.exec("x86_64-elf-as -c %s -o %s", path.join(boot_srcdir, "boot.s"), boot_s_object)
        os.exec(
            "x86_64-elf-gcc -c -mno-sse -mno-sse2 -mno-mmx -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -std=c++17 -I%s %s -o %s",
            path.join("$(projectdir)", "include"),
            path.join(boot_srcdir, "boot.cc"),
            boot_cc_object
        )
        os.exec(
            "x86_64-elf-ld --oformat binary -nostdlib -e _start -Ttext 0x10000 %s %s -o %s",
            boot_s_object,
            boot_cc_object,
            output
        )
        local size = os.filesize(output)
        if size > 524288 then
            raise("%s is too large: %d bytes > 524288 bytes", output, size)
        end
    end)

target("boot-artifacts")
    set_kind("phony")
    set_default(false)
    add_deps("boot-mbr", "boot-dbr", "boot-exdbr", "boot-loader")

target("user-init-elf")
    set_kind("phony")
    -- Default-on: normal boot now packages /boot/user/init.elf for launch_init.
    -- user_elf_smoke continues to reuse the same artifact.
    set_default(true)
    on_build(function (target)
        local user_srcdir = path.join("$(projectdir)", "user", "init")
        local user_bindir = path.join("$(builddir)", "bin", "user")
        local user_tempdir = path.join("$(builddir)", "temp", "user")
        os.mkdir(user_bindir)
        os.mkdir(user_tempdir)
        local object = path.join(user_tempdir, "init.s.o")
        local output = path.join(user_bindir, "init.elf")
        os.exec("x86_64-elf-as -c %s -o %s", path.join(user_srcdir, "init.s"), object)
        os.exec(
            "x86_64-elf-ld -nostdlib -static -z max-page-size=0x1000 -T %s %s -o %s",
            path.join(user_srcdir, "link.lds"),
            object,
            output
        )
        local size = os.filesize(output)
        if size > 65536 then
            raise("%s is too large: %d bytes > 65536 bytes", output, size)
        end
    end)

target("kernel")
    set_plat("cross")
    set_arch("x86_64")
    set_kind("binary")
    
    -- O2 optimize
    set_optimize("faster")
    
    add_files("src/kernel/*.cc")
    add_files("src/kernel/bigos/**.cc")
    add_files("src/kernel/irq/**.cc")
    add_files("src/kernel/irq/**.s")
    add_files("src/kernel/sched/**.cc")
    add_files("src/kernel/sched/**.s")
    add_files("src/kernel/syscall/**.cc")
    add_files("src/kernel/signal/**.cc")
    add_files("src/kernel/terminal/**.cc")
    add_files("src/kernel/timer/**.cc")
    add_files("src/kernel/time/**.cc")
    add_files("src/kernel/fs/**.cc")
    add_files("src/drivers/**.cc")
    add_files("src/mm/**.cc")
    add_files("cpp/**.cc")

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
    add_files("src/kernel/proc/**.cc")
    add_files("src/kernel/proc/**.s")

    if has_config("user_program_smoke") then
        add_defines("BIGOS_USER_PROGRAM_SMOKE")
    end

    if has_config("user_elf_smoke") then
        add_defines("BIGOS_USER_ELF_SMOKE")
    end

    if has_config("fs_smoke") then
        add_defines("BIGOS_FS_SMOKE")
    end

    if has_config("demand_paging_smoke") then
        add_defines("BIGOS_DEMAND_PAGING_SMOKE")
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

    if is_mode("debug") then 
        set_symbols("debug")
    elseif is_mode("release") then
        set_symbols("hidden")
        set_strip("all")
    end

    on_link(function (target) 
        local runtime_tempdir = path.join("$(builddir)", "temp", "runtime")
        os.mkdir(runtime_tempdir)
        local crt0_object = path.join(runtime_tempdir, "crt0.o")
        os.exec("x86_64-elf-as -c %s -o %s", path.join("$(projectdir)", "src", "runtime", "crt0.s"), crt0_object)

        local objs_table = target:objectfiles()
        table.insert(objs_table,1,path.translate("$(projectdir)/lib/crtbegin.o"))
        table.insert(objs_table,1,path.translate("$(projectdir)/lib/crti.o"))
        table.insert(objs_table,1,path.translate(crt0_object))
        table.insert(objs_table,path.translate("$(projectdir)/lib/crtend.o"))
        table.insert(objs_table,path.translate("$(projectdir)/lib/crtn.o"))

        local ld_flags = "-nostdlib -lgcc"
        local ld_script = path.translate("$(projectdir)/link.lds")

        local objs = table.concat(objs_table," ")
        local lib_dir = path.translate("$(projectdir)/lib")
        local output_path = path.translate("$(builddir)/kernel")

        os.exec("x86_64-elf-ld %s -L%s -T %s %s -o %s",ld_flags,lib_dir,ld_script,objs,output_path)
    end)

    on_run(function (target) 
        local bochs_cmd = "bochs"
        if is_host("windows") then
            bochs_cmd = "bochsdbg"
        end

        local bochs_config = "$(projectdir)/test/bochsrc.bxrc"
        os.exec("%s -f %s -q",bochs_cmd,bochs_config)
    end)

target("bochs")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "boot-artifacts")
    on_run(function (target)
        import("core.base.option")
        import("core.base.process")
        run_boot_debug("bochs", "build/test/bochs.serial.log", option.get("arguments") or {}, process)
    end)

target("qemu")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "boot-artifacts")
    on_run(function (target)
        import("core.base.option")
        import("core.base.process")
        run_boot_debug("qemu", "build/test/qemu.serial.log", option.get("arguments") or {}, process)
    end)

target("qemu-gdb")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "boot-artifacts")
    on_run(function (target)
        import("core.base.option")
        import("core.base.process")
        run_boot_debug("qemu-gdb", "build/test/qemu-gdb.serial.log", option.get("arguments") or {}, process)
    end)
