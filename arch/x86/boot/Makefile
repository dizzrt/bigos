ARCH = $(shell uname -s)
ifeq ($(findstring CYGWIN, $(ARCH)), CYGWIN)
	PWD_PATH = $(shell cygpath -w $(CURDIR))
else
	PWD_PATH = $(CURDIR)
endif

TOP_PATH = $(PWD_PATH)/../../..
BIN_PATH = $(TOP_PATH)/build/bin/x86/boot
TEMP_PATH = $(TOP_PATH)/build/temp/x86/boot
INCLUDE_PATH = $(TOP_PATH)/include
PATH_CHECK = $(TEMP_PATH) $(BIN_PATH)

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
AS = x86_64-elf-as

export TOP_PATH TEMP_PATH INCLUDE_PATH
export CC LD AS

# compile parms
CXXFLAGS = -mno-sse -mno-sse2 -mno-mmx -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -std=c++17
LDFLAGS = --oformat binary -nostdlib
INCLUDE_PARM = $(foreach d, $(INCLUDE_PATH), -I$d)

# debug parms
ifeq ($(ARCH), Darwin)
# for macos
BOCHS_RUN = bochs
VDISK_PATH = $(TOP_PATH)/test/os.dmg
else
# for windows
BOCHS_RUN = bochsdbg
VDISK_PATH = $(TOP_PATH)/test/os.vhd
endif

SECTOR_SIZE = 512
BOCHSRC_PATH = $(TOP_PATH)/test/bochsrc.bxrc

# install parms
INSTALL_FLAGS = --vdisk $(VDISK_PATH) --sector-size $(SECTOR_SIZE)

all: mbr dbr exdbr boot

mbr:
	make build-mbr
	make install-mbr

dbr:
	make build-dbr
	make install-dbr

exdbr:
	make build-exdbr
	make install-exdbr

boot: 
	make build-boot
	make install-boot

$(PATH_CHECK):
	mkdir -p $@

$(TEMP_PATH)/boot.s.o: boot.s
	$(AS) -c $< -o $@

$(TEMP_PATH)/boot.cc.o: boot.cc
	$(CC) -c $(CXXFLAGS) $(INCLUDE_PARM) $< -o $@

build-mbr: $(PATH_CHECK) mbr.s
	$(AS) mbr.s -o $(TEMP_PATH)/mbr.o
	$(LD) $(LDFLAGS) -e _start -Ttext 0x7c00 $(TEMP_PATH)/mbr.o -o $(BIN_PATH)/mbr.bin
	@test $$(wc -c < $(BIN_PATH)/mbr.bin) -le 512

build-dbr: $(PATH_CHECK) dbr_exfat.s
	$(AS) dbr_exfat.s -o $(TEMP_PATH)/dbr_exfat.o
	$(LD) $(LDFLAGS) -e _start -Ttext 0x7c00 $(TEMP_PATH)/dbr_exfat.o -o $(BIN_PATH)/dbr.bin
	@test $$(wc -c < $(BIN_PATH)/dbr.bin) -le 512

build-exdbr: $(PATH_CHECK) exdbr_exfat.s
	$(AS) exdbr_exfat.s -o $(TEMP_PATH)/exdbr_exfat.o
	$(LD) $(LDFLAGS) -e _start -Ttext 0x1000 $(TEMP_PATH)/exdbr_exfat.o -o $(BIN_PATH)/exdbr.bin
	@test $$(wc -c < $(BIN_PATH)/exdbr.bin) -le 4096

build-boot: $(PATH_CHECK) $(TEMP_PATH)/boot.s.o $(TEMP_PATH)/boot.cc.o
	$(LD) $(LDFLAGS) -e _start -Ttext 0x10000 $(TEMP_PATH)/boot.s.o $(TEMP_PATH)/boot.cc.o -o $(BIN_PATH)/boot.bin
	@test $$(wc -c < $(BIN_PATH)/boot.bin) -le 524288

.PHONY:install-mbr
install-mbr:
	@./install.py $(INSTALL_FLAGS) --with-mbr $(BIN_PATH)/mbr.bin

.PHONY:install-dbr
install-dbr:
	@./install.py $(INSTALL_FLAGS) --with-dbr $(BIN_PATH)/dbr.bin

.PHONY:install-exdbr
install-exdbr:
	@./install.py $(INSTALL_FLAGS) --with-exdbr $(BIN_PATH)/exdbr.bin

.PHONY:install-boot
install-boot:
	@./install.py $(INSTALL_FLAGS) --with-boot $(BIN_PATH)/boot.bin

.PHONY:run
run:
	@$(BOCHS_RUN) -f $(BOCHSRC_PATH) -q

.PHONY:clean
clean:
	@if ls $(TEMP_PATH)/*.o &> /dev/null; then rm $(TEMP_PATH)/*.o; fi
	@if ls ./*.bin &> /dev/null; then rm *.bin; fi
