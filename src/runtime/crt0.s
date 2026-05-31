.code64
.section .bigos, "ax"
.extern kernel
.extern _init
.extern _fini

.globl _start
.type _start, @function
_start:
    call _init
    call kernel
    call _fini
