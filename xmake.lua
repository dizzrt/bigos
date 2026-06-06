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

option("user_vmem_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("enable validation-only user address space vmem smoke marker")
option_end()

add_includedirs("$(projectdir)/include")
add_includedirs("$(projectdir)/cpp/include")
add_includedirs("$(projectdir)/cpp/libsupc++/include")

add_cxxflags("-mno-sse","-mno-sse2", "-mno-mmx", "-mcmodel=kernel", "-ffreestanding", "-mno-red-zone", "-fno-rtti", "-fno-exceptions")

add_asflags("-mno-sse","-mno-sse2", "-mno-mmx", "-mcmodel=kernel", "-ffreestanding", "-mno-red-zone", "-fno-rtti", "-fno-exceptions")

target("kernel")
    set_plat("cross")
    set_arch("x86_64")
    set_kind("binary")
    
    -- O2 optimize
    set_optimize("faster")
    
    add_files("src/kernel/**.cc")
    add_files("src/kernel/**.s")
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

    if has_config("user_vmem_smoke") then
        add_defines("BIGOS_USER_VMEM_SMOKE")
    end

    if is_mode("debug") then 
        set_symbols("debug")
    elseif is_mode("release") then
        set_symbols("hidden")
        set_strip("all")
    end

    on_link(function (target) 
        local objs_table = target:objectfiles()
        table.insert(objs_table,1,path.translate("$(projectdir)/lib/crtbegin.o"))
        table.insert(objs_table,1,path.translate("$(projectdir)/lib/crti.o"))
        table.insert(objs_table,1,path.translate("$(projectdir)/lib/crt0.o"))
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
