---@diagnostic disable: undefined-global

local function run_boot_debug(emulator, serial_log, target_args, process, base_args)
    local args = {"tools/boot_debug.py", "run", "--emulator", emulator, "--skip-build", "--serial-log", serial_log}
    for _, arg in ipairs(base_args or {}) do
        table.insert(args, arg)
    end
    local first_arg = target_args[1] == "--" and 2 or 1
    for index = first_arg, #target_args do
        local arg = target_args[index]
        table.insert(args, arg)
    end
    local proc = process.openv("python3", args)
    local _, status = proc:wait()
    proc:close()
    return status == 0
end

target("bochs")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "boot-artifacts", "user-init-elf")
    on_run(function (target)
        import("core.base.option")
        import("core.base.process")
        return run_boot_debug("bochs", "build/test/bochs.serial.log", option.get("arguments") or {}, process)
    end)

target("qemu")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "boot-artifacts", "user-init-elf")
    on_run(function (target)
        import("core.base.option")
        import("core.base.process")
        return run_boot_debug("qemu", "build/test/qemu.serial.log", option.get("arguments") or {}, process)
    end)

target("qemu-gdb")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "boot-artifacts", "user-init-elf")
    on_run(function (target)
        import("core.base.option")
        import("core.base.process")
        return run_boot_debug("qemu-gdb", "build/test/qemu-gdb.serial.log", option.get("arguments") or {}, process)
    end)

target("qemu-uefi")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "uefi-artifacts", "user-init-elf")
    on_run(function (target)
        import("core.base.option")
        import("core.base.process")
        return run_boot_debug(
            "qemu",
            "build/test/qemu-uefi.serial.log",
            option.get("arguments") or {},
            process,
            {"--boot-mode", "uefi", "--image", "build/test/uefi-esp.img"}
        )
    end)
