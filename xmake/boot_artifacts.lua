---@diagnostic disable: undefined-global, undefined-field

local boot_srcdir = path.join("$(projectdir)", "kernel", "arch", "x86", "boot")
local boot_bindir = path.join("$(builddir)", "bin", "x86", "boot")
local boot_tempdir = path.join("$(builddir)", "temp", "x86", "boot")

target("boot-mbr")
set_kind("phony")
set_default(false)
on_build(function()
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
on_build(function()
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
on_build(function()
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
on_build(function()
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
