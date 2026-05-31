set_xmakever("2.7.8")

set_project("bigos")
set_version("0.1.0")

includes("toolchains.lua")

set_languages("c17","cxx17")
set_toolchains("x86_64-elf-gcc")
add_rules("mode.debug", "mode.release")

add_includedirs("$(projectdir)/include")
add_includedirs("$(projectdir)/cpp/include")
add_includedirs("$(projectdir)/cpp/libsupc++/include")
add_includedirs("$(projectdir)/drivers/include")
add_includedirs("$(projectdir)/mm")
add_includedirs("$(projectdir)/mm/include")

add_cxxflags("-mno-sse","-mno-sse2", "-mno-mmx", "-mcmodel=kernel", "-ffreestanding", "-mno-red-zone", "-fno-rtti", "-fno-exceptions")

add_asflags("-mno-sse","-mno-sse2", "-mno-mmx", "-mcmodel=kernel", "-ffreestanding", "-mno-red-zone", "-fno-rtti", "-fno-exceptions")

target("kernel")
    set_plat("cross")
    set_arch("x86_64")
    set_kind("binary")
    
    -- O2 optimize
    set_optimize("faster")
    
    add_files("kernel/**.cc")
    add_files("kernel/**.s")
    add_files("cpp/**.cc")
    add_files("drivers/**.cc")
    add_files("mm/**.cc")

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
        local output_path = path.translate("$(buildir)/kernel")

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
