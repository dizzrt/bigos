---@diagnostic disable: undefined-global, undefined-field

local uefi_srcdir = path.join("$(projectdir)", "kernel", "arch", "x86", "uefi")
local uefi_bindir = path.join("$(builddir)", "bin", "x86", "uefi")
local uefi_tempdir = path.join("$(builddir)", "temp", "x86", "uefi")

local function command_available(name)
    if os.isfile(name) then
        return true
    end
    local env_path = os.getenv("PATH") or ""
    for dir in env_path:gmatch("[^:]+") do
        if os.isfile(path.join(dir, name)) then
            return true
        end
    end
    return false
end

local function require_tool(name, env_name)
    local configured = os.getenv(env_name)
    if configured and configured ~= "" then
        if not command_available(configured) then
            raise("UEFI build preflight: %s points to missing tool: %s", env_name, configured)
        end
        return configured
    end
    if not command_available(name) then
        raise("UEFI build preflight: missing required tool: %s", name)
    end
    return name
end

target("uefi-loader")
    set_kind("phony")
    set_default(false)
    on_build(function()
        local clang = require_tool("clang", "BIGOS_UEFI_CLANG")
        local lld_link = require_tool("lld-link", "BIGOS_UEFI_LLD_LINK")
        require_tool("llvm-objcopy", "BIGOS_UEFI_LLVM_OBJCOPY")
        local llvm_objdump = require_tool("llvm-objdump", "BIGOS_UEFI_LLVM_OBJDUMP")

        os.mkdir(uefi_bindir)
        os.mkdir(uefi_tempdir)
        local loader_obj = path.join(uefi_tempdir, "loader.obj")
        local handoff_obj = path.join(uefi_tempdir, "handoff.obj")
        local output = path.join(uefi_bindir, "BOOTX64.EFI")
        if os.isfile(output) then
            os.rm(output)
        end

        local include_dir = path.join("$(projectdir)", "include")
        os.exec(
            "%s -target x86_64-pc-win32 -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fno-exceptions -fno-rtti -std=c++17 -I%s -I%s -c %s -o %s",
            clang,
            include_dir,
            uefi_srcdir,
            path.join(uefi_srcdir, "loader.cc"),
            loader_obj
        )
        os.exec(
            "%s -target x86_64-pc-win32 -ffreestanding -c %s -o %s",
            clang,
            path.join(uefi_srcdir, "handoff.s"),
            handoff_obj
        )
        os.exec(
            "%s /machine:x64 /subsystem:efi_application /entry:efi_main /nodefaultlib /out:%s %s %s",
            lld_link,
            output,
            loader_obj,
            handoff_obj
        )
        os.exec("%s -p %s", llvm_objdump, output)
    end)

target("uefi-artifacts")
    set_kind("phony")
    set_default(false)
    add_deps("uefi-loader")
