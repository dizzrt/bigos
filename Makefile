ARCH = $(shell uname -s)
ifeq ($(findstring CYGWIN, $(ARCH)), CYGWIN)
	ROOT_PATH = $(shell cygpath -w $(CURDIR))
else
	ROOT_PATH = $(CURDIR)
endif

# debug parms
ifeq ($(ARCH), Darwin)
# for macos
BOCHS_RUN = bochs
else
# for windows
BOCHS_RUN = bochsdbg
endif
BOCHSRC_PATH = $(ROOT_PATH)/test/bochsrc.bxrc

.PHONY:run
run:
	@$(BOCHS_RUN) -f $(BOCHSRC_PATH) -q

.PHONY:boot-debug
boot-debug:
	@python3 tools/boot_debug.py run

.PHONY:boot-debug-gui
boot-debug-gui:
	@python3 tools/boot_debug.py run --bochs-extra "display_library: sdl2"

.PHONY:boot-debug-user-gui
boot-debug-user-gui:
	@python3 tools/boot_debug.py run --user-program-smoke --bochs-extra "display_library: sdl2"
