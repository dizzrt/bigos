ARCH=$(findstring CYGWIN, $(shell uname -s))

ifeq ($(ARCH), CYGWIN)
	TOP_PATH = $(shell cygpath -w $(CURDIR)/../..)
else
	TOP_PATH = $(CURDIR)/../..
endif

OUTPUT_PATH = $(TOP_PATH)/lib

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
AS = x86_64-elf-as

all: crts

crts: crt0 crti crtn

crt0: crt0.s
	mkdir -p $(OUTPUT_PATH)
	$(AS) -c $< -o $(OUTPUT_PATH)/crt0.o

crti: crti.s
	mkdir -p $(OUTPUT_PATH)
	$(AS) -c $< -o $(OUTPUT_PATH)/crti.o

crtn: crtn.s
	mkdir -p $(OUTPUT_PATH)
	$(AS) -c $< -o $(OUTPUT_PATH)/crtn.o
