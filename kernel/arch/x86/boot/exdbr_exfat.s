.file "dbr_exfat.s"
.code16
.text

.macro error msg
    movw $(\msg), %si
    call err_print
.endm

# bootloader
.set BOOTOFFSET32,   0x10000
.set INT13_RETRY_LIMIT, 3
.set ATA_POLL_LIMIT, 1000000
.set ATA_STATUS_BSY, 0x80
.set ATA_STATUS_DRQ, 0x08
.set ATA_STATUS_DF,  0x20
.set ATA_STATUS_ERR, 0x01

.globl _start
_start:
    jmp exdbr

DiskAddressPacket:
    .byte 0x10  # 0x00 size of packet
    .byte 0     # 0x01 reserved
    .word 0     # 0x02 number of sectors to transfer
    .word 0     # 0x04 offset of transfer buffer
    .word 0     # 0x06 segment of transfer buffer
    .long 0     # 0x08 lower 32-bits of 48-bits strating LBA
    .long 0     # 0x0c upper 16-bits of 48-bits strating LBA

gdt_32:
    .quad 0x0000000000000000 # zero
    .quad 0x00cf98000000ffff # code
    .quad 0x00cf92000000ffff # data
    .quad 0x0040960900000000 # stack

    .equ SELECTOR_CODE_32, 0x01<<3
    .equ SELECTOR_DATA_32, 0x02<<3
    .equ SELECTOR_STACK_32, 0x03<<3

gdt_attribute_32:
    .word 0xff
    .quad gdt_32

data_offset: .long 0
load_buffer: .long 0x0f00

err_boot_not_found: .asciz "bootloader not found"
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

int13h_extension_read:
    push %cx
    mov $INT13_RETRY_LIMIT, %cx
int13h_retry:
    mov $0x42, %ah
    mov (0x802), %dl
    movw $DiskAddressPacket, %si
    int $0x13
    jc int13h_failed
    test %ah, %ah
    jz int13h_ok
int13h_failed:
    loop int13h_retry
    pop %cx
    error err_read
int13h_ok:
    pop %cx
    ret

next_directory_entry:
    xor %eax, %eax
    mov %es:0x01(%di), %al
    add $0x01, %al
    mov $0x20, %bl
    mul %bl
    add %ax, %di
    ret

exdbr:
    # make bp point to exfat infos
    push %dx
    mov %sp, %bp

    #prepare registers
    xor %ax, %ax
    mov %ax, %es
    mov %ax, %ds
    mov %ax, %si

    # detect_memory_e820
    movl $0, %ebx
    movl $0x18, %ecx    
    movw $0x500, %di
detect_memory_loop:
    movl $0xe820, %eax
    movl $0x534d4150, %edx
    int $0x15
    incw %si
    addw $0x18, %di
    # check is the last
    cmpl $0, %ebx
    jne detect_memory_loop
    movw %si, (0x800)

    # get video mode
    mov $0x0f, %ah
    int $0x10
    mov %ah, (0x803)
    mov %al, (0x804)
    mov %bh, (0x805)

    # video configuration information
    mov $0x12, %ah
    mov $0x10, %bl
    int $0x10
    mov %bh, (0x806)
    mov %bl, (0x807)
    mov %ch, (0x808)
    mov %cl, (0x809)

    # get hd0 data
    xor %ax, %ax
    mov %ax, %ds
    lds (4*0x41), %si
    xor %ax, %ax
    mov %ax, %es
    mov $0x810, %di
    mov $0x10, %cx
    rep movsb

    # get hd1 data
    xor %ax, %ax
    mov %ax, %ds
    lds (4*0x46), %si
    xor %ax, %ax
    mov %ax, %es
    mov $0x820, %di
    mov $0x10, %cx
    rep movsb

    # check that there is a hd1
    mov $0x1500, %ax
    mov $0x81, %dl
    int $0x13
    jc no_hd1
    cmp $0x03, %ah
    je L0

no_hd1:
    xor %ax, %ax
    mov %ax, %es
    mov %ax, %ds
    mov $0x820, %di
    mov $0x10, %cx
    rep stosb

L0:
    mov (%bp), %bx
    movl 0x55(%bx), %eax
    movl %eax, (data_offset)
    movl 0x3d(%bx), %eax
    addl %eax, (data_offset)
    
    # save data area offset
    movl (data_offset), %eax
    movl %eax, (0x830)

    # search root directory
    # first cluster of root directory
    movl 0x5d(%bx), %eax
    # 8 sectors per cluster
    movl $0x08, %ebx
    sub $2, %eax
    mul %ebx
    # the offset of root directory
    addl (data_offset), %eax

    # prepare DAP to load root directory to 0xf000
    movl %eax, (DiskAddressPacket + 0x08)
    mov (load_buffer), %dx
    mov %dx, (DiskAddressPacket + 0x06)
    mov %dx, %es
    mov $0x08, %dx
    mov %dx, (DiskAddressPacket + 0x02)
    # load root directory
    call int13h_extension_read

    # search boot directory
    xor %di, %di
search_boot_directory_loop:
    cmp $0x1000, %di
    jb in_bounds_L0
    error err_boot_not_found
in_bounds_L0:
    movb %es:(%di), %al
    cmp $0x85, %al
    je L1
    add $0x20, %di
    jmp search_boot_directory_loop

L1:
    # check if first two words are "bo"
    movl %es:0x42(%di), %eax
    cmp $0x006f0062, %eax
    jne L2

    # check if next two words are "ot"
    movl %es:0x46(%di), %eax
    cmp $0x0074006f, %eax
    je load_boot_directory

L2:
    call next_directory_entry
    jmp search_boot_directory_loop

load_boot_directory:
    # 8 sectors per cluster
    movl $0x08, %ebx
    movl %es:0x34(%di), %eax
    sub $0x02, %eax
    mul %ebx
    # the offset of boot directory
    addl (data_offset), %eax
    mov %eax, (DiskAddressPacket + 0x08)
    # load boot directory to 0xf000
    call int13h_extension_read

    xor %di, %di
search_boot_loop:
    cmp $0x1000, %di
    jb in_bounds_L1
    error err_boot_not_found
in_bounds_L1:
    movb %es:(%di), %al
    cmp $0x85, %al
    je L3
    add $0x20, %di
    jmp search_boot_loop

L3:
    # check if first two words are "bo"
    movl %es:0x42(%di), %eax
    cmp $0x006f0062, %eax
    jne L4

    # check if next two words are "ot"
    movl %es:0x46(%di), %eax
    cmp $0x0074006f, %eax
    jne L4

    # check if next two words are ".b"
    movl %es:0x4a(%di), %eax
    cmp $0x0062002e, %eax
    jne L4

    # check if last two words are "in"
    movl %es:0x4e(%di), %eax
    cmp $0x006e0069, %eax
    je entering_protected_mode

L4:
    call next_directory_entry
    jmp search_boot_loop

entering_protected_mode:
    # save the boot.bin offset
    movl (load_buffer), %eax
    shl $0x04, %eax
    add %di, %ax
    movl %eax, %edi

    cli
    lgdt (gdt_attribute_32)

    # open A20 address line
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    # set PE(protected enable) flag
    movl %cr0, %eax
    bts $0x0, %eax
    movl %eax, %cr0
    ljmp $SELECTOR_CODE_32, $protected_mode

.code32
protected_mode:
    movw $SELECTOR_DATA_32, %ax
    movw %ax, %ds
    movw %ax, %es

    movw $SELECTOR_STACK_32, %ax
    movw %ax, %ss
    movl $0xfbff, %esp

    mov $0x08, %ebx
    mov 0x34(%edi), %eax
    sub $0x02, %eax
    mul %ebx
    # the offset of boot.bin
    add (data_offset), %eax
    push %eax

    mov 0x38(%edi), %eax
    mov 0x3c(%edi), %edx
    mov $0x200, %ebx
    div %ebx
    cmp $0, %edx
    je no_remainder
    incl %eax
no_remainder:
    # ebx => lba offset of boot.bin
    pop %ebx
    # ecx => nr of sectors to load
    mov %eax, %ecx
    push %eax

    # prepare disk to load boot.bin
    # The protected-mode loader below uses ATA primary-master PIO. With an
    # additional persistent test disk attached, some BIOSes may number the
    # booted hard disk above 0x80 even though the QEMU/Bochs topology still keeps
    # the boot image on primary master. Accept hard-disk BIOS numbers here; the
    # loader continues to read from primary master and rejects floppies.
    cmpb $0x80, (0x802)
    jb unsupported_boot_drive

    # prepare disk to load boot.bin
    # select master disk
    mov $0x40, %al
    mov $0x01f6, %dx
    outb %al, %dx
    mov $ATA_POLL_LIMIT, %esi
wait_drive_select:
    call wait_nops
    mov $0x01f7, %dx
    inb %dx, %al
    test $ATA_STATUS_BSY, %al
    jz drive_selected
    decl %esi
    jnz wait_drive_select
    jmp ata_timeout
drive_selected:

    # the lower byte of nr of sectors
    mov %ch, %al
    mov $0x01f2, %dx
    outb %al, %dx

    # lba-4
    mov %ebx, %eax
    shr $0x18, %eax
    mov $0x01f3, %dx
    outb %al, %dx

    # lba-5
    mov $0, %al
    mov $0x01f4, %dx
    outb %al, %dx

    # lba-6
    mov $0x01f5, %dx
    outb %al, %dx

    # the upper byte of nr of sectors
    mov %cl, %al
    mov $0x01f2, %dx
    outb %al, %dx

    # lba-1
    mov %bl, %al
    mov $0x01f3, %dx
    outb %al, %dx

    # lab-2
    mov %bh, %al
    mov $0x01f4, %dx
    outb %al, %dx

    # lba-3
    mov %ebx, %eax
    shr $0x10, %eax
    mov $0x01f5, %dx
    outb %al, %dx

    # read cmd
    mov $0x24, %al
    mov $0x01f7, %dx
    outb %al, %dx

    pop %ebp
    mov $BOOTOFFSET32, %edi

read_sector_loop:
    mov $ATA_POLL_LIMIT, %esi
wait_disk:
    call wait_nops
    mov $0x01f7, %dx
    inb %dx, %al
    test $(ATA_STATUS_ERR | ATA_STATUS_DF), %al
    jnz ata_controller_error
    and $(ATA_STATUS_BSY | ATA_STATUS_DRQ), %al
    cmp $0x08, %al
    je disk_ready
    decl %esi
    jnz wait_disk
    jmp ata_timeout

disk_ready:
    # ATA PIO asserts DRQ per sector on QEMU, so wait before each 512-byte read.
    mov $0x100, %ecx
    mov $0x01f0, %dx
pio_loop:
    inw %dx, %ax
    movw %ax, (%edi)
    add $0x02, %edi
    loop pio_loop

    decl %ebp
    jnz read_sector_loop

    # jmp to bootloader
    ljmp $SELECTOR_CODE_32, $BOOTOFFSET32

unsupported_boot_drive:
    movw $0x4f55, 0xb8000
    jmp halt32

ata_timeout:
    movw $0x4f54, 0xb8000
    jmp halt32

ata_controller_error:
    movw $0x4f45, 0xb8000

halt32:
    hlt
    jmp halt32

wait_nops:
    .rept 8
        nop
    .endr
    ret
