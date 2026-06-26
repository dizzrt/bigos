---@diagnostic disable: undefined-global, undefined-field

target("user-init-elf")
set_kind("phony")
-- Default-on: normal boot now packages /boot/user/init.elf for launch_init.
-- user_elf_smoke continues to reuse the same artifact. This target is
-- generalized to build user C programs (crt0 + minimal user libc) and link
-- them as static ET_EXEC ELF64 with -nostdlib -static. The default init is
-- the resident C init; the userland_smoke build instead links the smoke
-- validation program as /boot/user/init.elf so launch_init runs it as PID-1.
set_default(true)
on_build(function()
    import("core.base.option")

    local projectdir = "$(projectdir)"
    local user_bindir = path.join("$(builddir)", "bin", "user")
    local user_tempdir = path.join("$(builddir)", "temp", "user")
    local user_bin_subdir = path.join(user_bindir, "bin")
    local user_smoke_bin_subdir = path.join(user_bin_subdir, "smoke")
    os.mkdir(user_bindir)
    os.mkdir(user_tempdir)
    os.mkdir(user_bin_subdir)

    -- User C compile flags: freestanding, no host libc, no SSE/red-zone,
    -- size-optimized to stay inside the bounded artifact limit.
    local cflags = "-c -ffreestanding -nostdlib -mno-sse -mno-sse2 -mno-mmx " ..
        "-mno-red-zone -fno-pic -fno-pie -Os -std=c17 -Wall -Wextra"
    local libc_inc = path.join(projectdir, "user", "libc", "include")
    local link_lds = path.join(projectdir, "user", "link.lds")

    -- Keep the build-time bound aligned with USER_ELF_MAX_FILE_BYTES, which
    -- is enforced again by launch_init/execve before loading user ELFs.
    local size_limit = 64 * 1024

    -- Compile crt0 once.
    local crt0_obj = path.join(user_tempdir, "crt0.o")
    os.exec("x86_64-elf-as -c %s -o %s", path.join(projectdir, "user", "crt0", "crt0.s"), crt0_obj)

    -- Compile the user libc once into a list of object files.
    local libc_srcs = { "syscall.c", "string.c", "malloc.c", "stdio.c", "env.c", "ctype.c", "assert.c" }
    local libc_objs = {}
    for _, src in ipairs(libc_srcs) do
        local obj = path.join(user_tempdir, "libc_" .. path.basename(src) .. ".o")
        os.exec("x86_64-elf-gcc %s -I%s %s -o %s", cflags, libc_inc,
            path.join(projectdir, "user", "libc", src), obj)
        table.insert(libc_objs, obj)
    end

    -- Helper: compile a single C program with crt0 + libc and link as
    -- static ET_EXEC ELF64; enforce the bounded size limit.
    local function build_user_program(src, output)
        local obj = path.join(user_tempdir, path.basename(src) .. ".o")
        os.exec("x86_64-elf-gcc %s -I%s %s -o %s", cflags, libc_inc, src, obj)
        local objs = crt0_obj .. " " .. obj
        for _, lo in ipairs(libc_objs) do
            objs = objs .. " " .. lo
        end
        os.exec("x86_64-elf-ld -nostdlib -static -z max-page-size=0x1000 -T %s %s -o %s",
            link_lds, objs, output)
        local size = os.filesize(output)
        if size > size_limit then
            raise("%s is too large: %d bytes > %d bytes", output, size, size_limit)
        end
    end

    -- Always build only the regular bounded /bin programs.
    local user_bin_programs = {
        { "sh", path.join(projectdir, "user", "sh", "sh.c") },
        { "echo", path.join(projectdir, "user", "bin", "echo.c") },
        { "cat", path.join(projectdir, "user", "bin", "cat.c") },
        { "ls", path.join(projectdir, "user", "bin", "ls.c") },
        { "mkdir", path.join(projectdir, "user", "bin", "mkdir.c") },
        { "rm", path.join(projectdir, "user", "bin", "rm.c") },
        { "rmdir", path.join(projectdir, "user", "bin", "rmdir.c") },
        { "rename", path.join(projectdir, "user", "bin", "rename.c") },
        { "stat", path.join(projectdir, "user", "bin", "stat.c") },
        { "touch", path.join(projectdir, "user", "bin", "touch.c") },
        { "truncate", path.join(projectdir, "user", "bin", "truncate.c") },
        { "mkfs_bigfs", path.join(projectdir, "user", "bin", "mkfs_bigfs.c") },
        { "cp", path.join(projectdir, "user", "bin", "cp.c") },
        { "mv", path.join(projectdir, "user", "bin", "mv.c") },
        { "tee", path.join(projectdir, "user", "bin", "tee.c") },
        { "write", path.join(projectdir, "user", "bin", "write.c") },
        { "append", path.join(projectdir, "user", "bin", "append.c") },
        { "head", path.join(projectdir, "user", "bin", "head.c") },
        { "tail", path.join(projectdir, "user", "bin", "tail.c") },
        { "wc", path.join(projectdir, "user", "bin", "wc.c") },
        { "grep", path.join(projectdir, "user", "bin", "grep.c") },
        { "hexdump", path.join(projectdir, "user", "bin", "hexdump.c") },
        { "date", path.join(projectdir, "user", "bin", "date.c") },
        { "kill", path.join(projectdir, "user", "bin", "kill.c") },
        { "sleep", path.join(projectdir, "user", "bin", "sleep.c") },
        { "basename", path.join(projectdir, "user", "bin", "basename.c") },
        { "dirname", path.join(projectdir, "user", "bin", "dirname.c") },
        { "more", path.join(projectdir, "user", "bin", "more.c") },
        { "find", path.join(projectdir, "user", "bin", "find.c") },
        { "du", path.join(projectdir, "user", "bin", "du.c") },
    }
    for _, program in ipairs(user_bin_programs) do
        build_user_program(program[2], path.join(user_bin_subdir, program[1]))
    end

    -- Build /bin/smoke probes only for default-off userland validation images.
    if has_config("userland_smoke") then
        os.mkdir(user_smoke_bin_subdir)
        local smoke_bin_programs = {
            { "args", path.join(projectdir, "user", "smoke", "bin", "args.c") },
            { "env", path.join(projectdir, "user", "smoke", "bin", "env.c") },
            { "out", path.join(projectdir, "user", "smoke", "bin", "out.c") },
            { "errno", path.join(projectdir, "user", "smoke", "bin", "errno.c") },
            { "exit", path.join(projectdir, "user", "smoke", "bin", "exit.c") },
            { "libc_subset", path.join(projectdir, "user", "smoke", "bin", "libc_subset.c") },
        }
        for _, program in ipairs(smoke_bin_programs) do
            build_user_program(program[2], path.join(user_smoke_bin_subdir, program[1]))
        end
    elseif has_config("filesystem_maturity_smoke") then
        os.mkdir(user_smoke_bin_subdir)
        local smoke_bin_programs = {
            { "args", path.join(projectdir, "user", "smoke", "bin", "args.c") },
            { "env", path.join(projectdir, "user", "smoke", "bin", "env.c") },
            { "out", path.join(projectdir, "user", "smoke", "bin", "out.c") },
            { "errno", path.join(projectdir, "user", "smoke", "bin", "errno.c") },
            { "exit", path.join(projectdir, "user", "smoke", "bin", "exit.c") },
            { "libc_subset", path.join(projectdir, "user", "smoke", "bin", "libc_subset.c") },
        }
        for _, program in ipairs(smoke_bin_programs) do
            build_user_program(program[2], path.join(user_smoke_bin_subdir, program[1]))
        end
    else
        os.rm(user_smoke_bin_subdir)
    end

    -- Select the PID-1 init image:
    --   sleep_syscall_smoke                -> blocking sleep syscall validation program,
    --   userland_smoke/filesystem_maturity_smoke -> userland validation program,
    --   user_elf_smoke/user_program_smoke -> minimal print+exit smoke ELF
    --                               (preserves BIGOS_USER_ENTER/EXIT),
    --   otherwise                -> resident C init that launches /bin/sh.
    local init_output = path.join(user_bindir, "init.elf")
    if has_config("anonymous_lifecycle_smoke") then
        build_user_program(path.join(projectdir, "user", "smoke", "anonymous_lifecycle_smoke.c"), init_output)
    elseif has_config("sleep_syscall_smoke") then
        build_user_program(path.join(projectdir, "user", "smoke", "sleep_syscall_smoke.c"), init_output)
    elseif has_config("userland_smoke") or has_config("filesystem_maturity_smoke") then
        build_user_program(path.join(projectdir, "user", "smoke", "userland_smoke.c"), init_output)
    elseif has_config("user_elf_smoke") or has_config("user_program_smoke") then
        build_user_program(path.join(projectdir, "user", "smoke", "elf_smoke.c"), init_output)
    else
        build_user_program(path.join(projectdir, "user", "init", "init.c"), init_output)
    end
end)
