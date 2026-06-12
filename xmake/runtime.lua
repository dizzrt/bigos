---@diagnostic disable: undefined-global, undefined-field, lowercase-global

function bigos_kernel_runtime_objects(target)
    local runtime_tempdir = path.join("$(builddir)", "temp", "runtime")
    local runtime_srcdir = path.join("$(projectdir)", "kernel", "runtime")
    os.exec("mkdir -p %s", runtime_tempdir)

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

    local objs_table = target:objectfiles()
    table.insert(objs_table, 1, path.translate(crtbegin))
    table.insert(objs_table, 1, path.translate(crti_object))
    table.insert(objs_table, 1, path.translate(crt0_object))
    table.insert(objs_table, path.translate(crtend))
    table.insert(objs_table, path.translate(crtn_object))
    return objs_table
end
