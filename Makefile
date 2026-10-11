# ff9-ds: FF9 (PS1) on the DS, built from the ff9-decomp C source in decomp/.
# Build targets run in the BlocksDS container: tools/docker.sh make
# `make extract` runs on the host (it drives the decomp's own container).

DECOMP   := decomp
VERSION  := us
BUILD    := build
OBJ      := $(BUILD)/obj

CC       := arm-none-eabi-gcc
OBJCOPY  := arm-none-eabi-objcopy
NM       := arm-none-eabi-nm
PYTHON   := python3

# The decomp's binaries: main + the FF9.IMG dir-0 code overlays.
OVLS     := $(shell awk '/^[0-9a-f]/ {print $$1}' $(DECOMP)/config/overlays.$(VERSION).txt)
BINS     := main $(addprefix ovl_,$(OVLS))

# Decomp C and the port-only equivalents, by binary.
src_of    = $(wildcard $(DECOMP)/src/$(1)/*.c) $(wildcard $(DECOMP)/src_equiv/$(1)/*.c)
obj_of    = $(patsubst $(DECOMP)/%.c,$(OBJ)/%.o,$(call src_of,$(1)))
header_of = $(if $(filter main,$(1)),ff9.h,$(1).h)

# PS1 semantics: signed char, no strict aliasing, wrapping overflow; K&R-era C (gcc 2.8) needs gnu89 + permissive.
ARCH     := -mcpu=arm946e-s -mthumb -mthumb-interwork
CFLAGS   := -std=gnu89 -fpermissive -O2 -g $(ARCH) \
            -fsigned-char -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -fno-builtin-memcpy \
            -ffunction-sections -fdata-sections -w
INCLUDES := -I$(DECOMP)/include -Iport/include

NSOBJ    := $(BUILD)/nsobj
GEN      := $(BUILD)/gen
ELF      := $(BUILD)/ff9ds.elf
ROM      := $(BUILD)/ff9ds.nds

# Port code: modern C with warnings, built against libnds (DSi-mode memory map).
SPECS     := $(BLOCKSDS)/sys/crts/dsi_arm9.specs
PORT_SRCS := $(wildcard port/psx/*.c port/ds/*.c port/game/*.c)
PORT_OBJS := $(patsubst port/%.c,$(BUILD)/port/%.o,$(PORT_SRCS))
PORT_CFLAGS := -std=gnu17 -O2 -g $(ARCH) -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections \
               -specs=$(SPECS) -I$(BLOCKSDS)/libs/libnds/include -Iport/include -I$(DECOMP)/include \
               $(if $(AUTORUN),-DPORT_AUTORUN)
LDFLAGS   := $(ARCH) -specs=$(SPECS) -L$(BLOCKSDS)/libs/libnds/lib -Wl,--gc-sections
LIBS      := -lnds9 -lc
ARM7ELF   := $(BLOCKSDS)/sys/arm7/main_core/arm7_minimal.elf
NDSTOOL   := $(BLOCKSDS)/tools/ndstool/ndstool

DECOMP_OBJS := $(foreach b,$(BINS),$(call obj_of,$(b)))
NS_OBJS     := $(patsubst $(OBJ)/%,$(NSOBJ)/%,$(DECOMP_OBJS))
GEN_OBJS    := $(addprefix $(GEN)/,$(addsuffix .o,$(addprefix img_,$(BINS)) tramp))
LINK_OBJS   := $(NS_OBJS) $(GEN_OBJS) $(PORT_OBJS)

.PHONY: all decomp-objs extract clean
.SECONDARY:

all: $(ROM)

# --- decomp C -> ARM objects -------------------------------------------------------------------------
# Each file goes through tools/portify.py (drops MIPS rodata padding) into build/, then compiles with the port
# prelude force-included. The prelude includes the binary's header and replaces its hardware macros.
define bin_rules
$(OBJ)/src/$(1)/%.o $(OBJ)/src_equiv/$(1)/%.o: PORT_HEADER := $(call header_of,$(1))
$(OBJ)/src/$(1)/%.o $(OBJ)/src_equiv/$(1)/%.o: PORT_BIN := $(1)
endef
$(foreach b,$(BINS),$(eval $(call bin_rules,$(b))))

$(BUILD)/portified/%.c: $(DECOMP)/%.c tools/portify.py config/addr_plain.txt
	@mkdir -p $(dir $@)
	@$(PYTHON) tools/portify.py $< $@ --report $@.addr.txt

PORT_HDRS := $(wildcard port/include/*.h)

$(OBJ)/%.o: $(BUILD)/portified/%.c $(PORT_HDRS)
	@mkdir -p $(dir $@)
	@echo "CC   $*"
	@$(CC) $(CFLAGS) $(INCLUDES) -DPORT_HEADER='"$(PORT_HEADER)"' -DPORT_BIN_$(PORT_BIN) -include port_prelude.h -c $< -o $@

decomp-objs: $(DECOMP_OBJS)

# --- linking the 20 binaries into one image (docs/design.md §3.3-3.5) ---------------------------------------
# gen_link.py writes the data images, the PS1 data symbol addresses, the overlay namespace maps and the
# resident-overlay trampolines from the decomp's splat output (the user's disc) and the compiled objects.
$(GEN)/stamp: $(DECOMP_OBJS) tools/gen_link.py tools/ff9sym.py $(wildcard config/xrefs.txt)
	@mkdir -p $(GEN)
	@$(PYTHON) tools/gen_link.py $(OBJ)
	@touch $@

$(GEN)/%.s: $(GEN)/stamp ;

$(GEN)/%.o: $(GEN)/%.s
	@$(CC) $(ARCH) -c $< -o $@

# Overlay objects get their namespace prefix; every object gets its cross-binary references pointed at the
# right overlay (or trampoline).
# src_equiv/ (behaviourally equivalent, non-matching C) is weakened first, so a matched src/ definition of the
# same function always wins as the decomp progresses.
$(NSOBJ)/%.o: $(OBJ)/%.o $(GEN)/stamp
	@mkdir -p $(dir $@)
	@$(OBJCOPY) $(if $(filter src_equiv/%,$*),--weaken) $< $@.tmp
	@$(OBJCOPY) --redefine-syms=$(GEN)/redefine_$(word 2,$(subst /, ,$*)).txt $@.tmp $@
	@rm -f $@.tmp

$(BUILD)/port/%.o: port/%.c $(PORT_HDRS)
	@mkdir -p $(dir $@)
	@echo "CC   port/$*"
	@$(CC) $(PORT_CFLAGS) -c $< -o $@

# Two-phase link: the first pass lists what is still undefined (asm routines without C yet, PsyQ functions the
# port doesn't implement yet); gen_stubs.py turns those into stubs that stop with their name, and reports them.
$(GEN)/stubs.c: $(LINK_OBJS) tools/gen_stubs.py
	@$(CC) -o $(BUILD)/scan.elf $(LINK_OBJS) $(GEN)/psx_syms.ld $(LDFLAGS) -Wl,--warn-unresolved-symbols \
		-Wl,--start-group $(LIBS) -Wl,--end-group 2> $(BUILD)/scan.log || true
	@$(PYTHON) tools/gen_stubs.py $(BUILD)/scan.log $@

$(GEN)/stubs.o: $(GEN)/stubs.c
	@$(CC) $(PORT_CFLAGS) -w -c $< -o $@

$(ELF): $(LINK_OBJS) $(GEN)/stubs.o
	@echo "LD   $@"
	@$(CC) -o $@ $(LINK_OBJS) $(GEN)/stubs.o $(GEN)/psx_syms.ld $(LDFLAGS) -Wl,-Map,$(BUILD)/ff9ds.map \
		-Wl,--start-group $(LIBS) -Wl,--end-group

$(ROM): $(ELF)
	@echo "NDS  $@"
	@$(NDSTOOL) -c $@ -7 $(ARM7ELF) -9 $(ELF) -b $(BLOCKSDS)/sys/icon.bmp "FF9 DS;port build;not for distribution"

# --- disc data (host) ----------------------------------------------------------------------------------
# Needs the four disc images in disks/us/raw/ ("FF9 CD1.img" ... as in the decomp). The decomp's split only
# needs the exe and the code overlays, which are copied into the submodule (its container can't follow links).
extract:
	$(MAKE) -C $(DECOMP) extract extract-img extract-ovl DISC_DIR=$(CURDIR)/disks/$(VERSION)/raw
	cd $(DECOMP) && tools/docker.sh make split

clean:
	rm -rf $(BUILD)
