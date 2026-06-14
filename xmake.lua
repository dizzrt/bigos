---@diagnostic disable: undefined-global

set_xmakever("2.7.8")

set_project("bigos")
set_version("0.1.0")

includes("xmake/toolchains.lua")

set_languages("c17", "cxx17")
set_toolchains("x86_64-elf-gcc")
add_rules("mode.debug", "mode.release")

includes("xmake/options.lua")
includes("xmake/common.lua")
includes("xmake/boot_artifacts.lua")
includes("xmake/uefi_artifacts.lua")
includes("xmake/user_package.lua")
includes("xmake/runtime.lua")
includes("xmake/kernel.lua")
includes("xmake/run_targets.lua")
