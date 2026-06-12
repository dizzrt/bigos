---@diagnostic disable: undefined-global

add_includedirs("$(projectdir)/include")
add_includedirs("$(projectdir)/cpp/include")
add_includedirs("$(projectdir)/cpp/libsupc++/include")

add_cxxflags(
    "-mno-sse",
    "-mno-sse2",
    "-mno-mmx",
    "-mcmodel=kernel",
    "-ffreestanding",
    "-mno-red-zone",
    "-fno-rtti",
    "-fno-exceptions"
)

add_asflags(
    "-mno-sse",
    "-mno-sse2",
    "-mno-mmx",
    "-mcmodel=kernel",
    "-ffreestanding",
    "-mno-red-zone",
    "-fno-rtti",
    "-fno-exceptions"
)
