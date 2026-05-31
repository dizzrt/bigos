-- define toolchain
toolchain("x86_64-elf-gcc")

-- mark as standalone toolchain
set_kind("standalone")

-- set toolset
set_toolset("cc", "x86_64-elf-gcc")
set_toolset("cxx", "x86_64-elf-gcc", "x86_64-elf-g++")
set_toolset("ld", "x86_64-elf-g++", "x86_64-elf-gcc")
set_toolset("sh", "x86_64-elf-g++", "x86_64-elf-gcc")
set_toolset("ar", "x86_64-elf-ar")
set_toolset("ex", "x86_64-elf-ar")
set_toolset("strip", "x86_64-elf-strip")
set_toolset("mm", "x86_64-elf-gcc")
set_toolset("mxx", "x86_64-elf-gcc", "x86_64-elf-g++")
set_toolset("as", "x86_64-elf-gcc")

add_defines("x86_64-elf-gcc")

-- check toolchain
on_check(function(toolchain)
    return import("lib.detect.find_tool")("x86_64-elf-gcc")
end)
