# this mbr will copy itself to 0x9fc00
# and then load the dbr to 0x9fe00

.file "mbr.s"
.code16
.text

.set BASE,      0x7c00
.set DEST,      0x0600
.set OFFSET,    DEST - BASE

.set DBRSEG,    0x0000
.set DBROFFSET, 0x7c00

.set STACKSEG,  0x9000
.set STACKSP,   0xfbff
.set INT13_RETRY_LIMIT, 3

.macro error msg
    movw $(\msg + OFFSET), %si
    call err_print
.endm

.globl _start
_start:
    jmp mbr

DiskAddressPacket:
    .byte 0x10      # 0x00 size of packet
    .byte 0         # 0x01 reserved
    .word 1         # 0x02 number of sectors to transfer
    .word DBROFFSET # 0x04 offset of transfer buffer
    .word DBRSEG    # 0x06 segment of transfer buffer
    .long 0         # 0x08 lower 32-bits of 48-bits strating LBA
    .long 0         # 0x0c upper 16-bits of 48-bits strating LBA

str_brief: .asciz "BigOS Mbr 1.0"
str_loading: .asciz "loading..."

err_int13e: .asciz "int 13H extensions not supported"
err_noActivePartition: .asciz "no active partition found"
err_read: .asciz "read disk error"

print:
    movb $0x0e, %ah
print_loop:
    movb (%si), %al
    cmpb $0, %al
    je print_end
    int $0x10
    incw %si
    jmp print_loop
print_end:
    mov $0x03, %ah
    mov $0, %bx
    int $0x10
    incb %dh
    mov $0, %dl
    mov $0x02, %ah
    int $0x10
    ret

err_print:
    call print
    jmp .

mbr:
    # prepare registers
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es

    # save boot drive code
    movb %dl, (0x802)

    # prepare stack
    mov $STACKSEG, %ax
    mov $STACKSP, %sp
    mov %ax, %ss

    # set video mode(80x25, 4-bit, text mode)
    # and clear screen
    mov $0x0003, %ax
    int $0x10

    # print brief and loading
    mov $str_brief, %si
    call print

    mov $str_loading, %si
    call print
    
    cli

    # copy self to DEST and jump there
    # [ds:si] => [es:di]
    mov $BASE, %si
    mov $DEST, %di
    
    mov $0x80, %cx
    rep movsd
    jmp $0, $(copied_mbr + OFFSET)

copied_mbr:
    sti

    # check if int 0x13 extensions are supported
    mov $0x41, %ah
    mov $0x55aa, %bx
    mov (0x802), %dl
    int $0x13
    jnc search_active_partition
    error err_int13e

search_active_partition:
    # search the active partition
    mov $4, %cx
    mov $(pte + OFFSET), %di
loop_search:
    movb (%di), %bl
    cmpb $0x80, %bl # is active partition?
    je active_partition_found
    addw $0x10, %di
    loop loop_search
    error err_noActivePartition

active_partition_found:
    movl 0x08(%di), %ebx 
    movl %ebx, (DiskAddressPacket + OFFSET + 0x08) # LBA of partition start
    movw $INT13_RETRY_LIMIT, %bp
read_dbr_retry:
    movb $0x42, %ah
    movb (0x802), %dl
    movw $(DiskAddressPacket + OFFSET), %si
    int $0x13
    jc read_dbr_failed
    test %ah, %ah
    jz jmp_dbr
read_dbr_failed:
    decw %bp
    jnz read_dbr_retry
    error err_read

jmp_dbr:
    ljmp $DBRSEG, $DBROFFSET

.fill 0x01b8 - (. - _start) ,1,0

# Unique Disk ID / Signature
signature: .long 0
reserved: .word 0

# partition table entry format
# offset    size(bytes)    description
# 0x00      1              drive attributes (bit 7 set(0x80) = active or bootable)
# 0x01      3              CHS address of partition start
# 0x04      1              partition type
# 0x05      3              CHS address of last partition sector
# 0x08      4              LBA of partition start
# 0x0c      4              number of sectors in partition

# partition table
pte:
    .octa 0
    .octa 0
    .octa 0
    .octa 0

#BootSignature
.word 0xaa55
