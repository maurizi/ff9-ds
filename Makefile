# ff9-ds: FF9 (PS1) on the DS, built from the ff9-decomp C source in decomp/.
# Build targets run in the container: tools/docker.sh make [ds|pc]
# `make extract` runs on the host (it drives the decomp's own container).
#
#   make / make ds    build/ff9ds.nds     DSi-mode ROM (BlocksDS)
#   make pc           build/pc/ff9ds      32-bit Linux host build of the same game code and port/psx layer:
#                                         fast iteration, gdb, lockstep comparison with PCSX-Redux
#   AUTORUN=1         DS: run the game straight away instead of waiting for START

DECOMP   := decomp
VERSION  := us
BUILD    := build
GEN      := $(BUILD)/gen
PYTHON   := python3
,        := ,

# The decomp's binaries: main + the FF9.IMG dir-0 code overlays.
OVLS     := $(shell awk '/^[0-9a-f]/ {print $$1}' $(DECOMP)/config/overlays.$(VERSION).txt)
BINS     := main $(addprefix ovl_,$(OVLS))

# Decomp C, as paths relative to decomp/ without .c, in priority order (the link keeps the first definition):
#   src/<bin>/*.c              matched C
#   src_equiv/<bin>/*.c        port-only equivalents of asm routines (weak)
#   docs/wip/<bin>_<addr>.c    near-miss drafts of the last non-matching functions (weak, lowest priority)
src_of    = $(patsubst $(DECOMP)/%.c,%,$(wildcard $(DECOMP)/src/$(1)/*.c) $(wildcard $(DECOMP)/src_equiv/$(1)/*.c))
WIP_SRCS  := $(patsubst $(DECOMP)/%.c,%,$(wildcard $(DECOMP)/docs/wip/*_[0-9A-F]*.c))
DECOMP_SRCS := $(foreach b,$(BINS),$(call src_of,$(b))) $(WIP_SRCS)
wip_bin   = $(patsubst %_$(lastword $(subst _, ,$(1))),%,$(1))
bin_of    = $(if $(filter docs/wip/%,$(1)),$(call wip_bin,$(notdir $(1))),$(word 2,$(subst /, ,$(1))))
weak_src  = $(filter src_equiv/% docs/wip/%,$(1))
header_of = $(if $(filter main,$(1)),ff9.h,$(1).h)

PORT_HDRS := $(wildcard port/include/*.h)
PSX_SRCS  := $(wildcard port/psx/*.c port/game/*.c)

# PS1 semantics: signed char, no strict aliasing, wrapping overflow; K&R-era C (gcc 2.8) needs gnu89 + permissive.
DECOMP_CFLAGS := -std=gnu89 -fpermissive -O2 -g -fsigned-char -fno-strict-aliasing -fwrapv \
                 -fno-delete-null-pointer-checks -fno-builtin-memcpy -ffunction-sections -fdata-sections -w \
                 -I$(DECOMP)/include -Iport/include -include port_prelude.h \
                 $(addprefix -I,$(wildcard $(DECOMP)/src_equiv/*/)) # their "../src_equiv.h" resolves from here
PORT_CFLAGS   := -std=gnu17 -O2 -g -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter -Wno-builtin-declaration-mismatch -ffunction-sections -fdata-sections \
                 -Iport/include -I$(DECOMP)/include

.PHONY: all ds pc decomp-objs extract clean
.SECONDARY:

all: ds

# --- shared front half: portified sources and the generated link data ----------------------------------------
# tools/portify.py adapts each decomp file into build/portified/ (MIPS padding out, PS1 addresses mapped).
$(BUILD)/portified/%.c: $(DECOMP)/%.c tools/portify.py config/addr_plain.txt config/patches.txt
	@mkdir -p $(dir $@)
	@$(PYTHON) tools/portify.py $< $@ --report $@.addr.txt

# gen_link.py writes the data images, the PS1 data symbol addresses, the overlay namespace maps, the
# resident-overlay trampolines (ARM and x86) and the overlay table, from the decomp's splat output (the user's
# disc) and the ARM objects. Both targets share them.
$(GEN)/stamp: $(addprefix $(BUILD)/ds/obj/,$(addsuffix .o,$(DECOMP_SRCS))) tools/gen_link.py tools/ff9sym.py \
              $(wildcard config/xrefs.txt)
	@mkdir -p $(GEN)
	@$(PYTHON) tools/gen_link.py $(BUILD)/ds/obj
	@touch $@
$(GEN)/%.s: $(GEN)/stamp ;

# --- per-target back half ------------------------------------------------------------------------------------
# $(1) target, $(2) compiler, $(3) objcopy, $(4) arch flags, $(5) port platform dir, $(6) trampoline flavour,
# $(7) link flags, $(8) libs
define target_rules
$(1)_DIR      := $(BUILD)/$(1)
$(1)_DOBJS    := $$(addprefix $$($(1)_DIR)/obj/,$$(addsuffix .o,$(DECOMP_SRCS)))
$(1)_NSOBJS   := $$(patsubst $$($(1)_DIR)/obj/%,$$($(1)_DIR)/nsobj/%,$$($(1)_DOBJS))
$(1)_GENOBJS  := $$(addprefix $$($(1)_DIR)/gen/,$$(addsuffix .o,$$(addprefix img_,$(BINS)) tramp_$(6)))
$(1)_PORTOBJS := $$(patsubst port/%.c,$$($(1)_DIR)/port/%.o,$(PSX_SRCS) $$(wildcard port/$(5)/*.c))
$(1)_LINK     := $$($(1)_NSOBJS) $$($(1)_GENOBJS) $$($(1)_PORTOBJS)

$$($(1)_DIR)/obj/%.o: $(BUILD)/portified/%.c $(PORT_HDRS)
	@mkdir -p $$(dir $$@)
	@echo "CC   [$(1)] $$*"
	@$(2) $(4) $(DECOMP_CFLAGS) -DPORT_HEADER='"$$(call header_of,$$(call bin_of,$$*))"' \
		-DPORT_BIN_$$(call bin_of,$$*) -c $$< -o $$@ $$(if $$(filter docs/wip/%,$$*),2> $$@.log || \
		{ echo "draft $$* doesn't compile against current headers (skipped, see $$@.log)"; \
		  echo "" | $(2) $(4) -x c -c - -o $$@; })

# src_equiv/ is weakened first, so a matched src/ definition of the same function always wins; then overlay
# functions get their namespace prefix and cross-binary references are pointed at the right overlay; last, the
# functions the port replaces (config/overrides.txt) are weakened.
$$($(1)_DIR)/nsobj/%.o: $$($(1)_DIR)/obj/%.o $(GEN)/stamp config/overrides.txt
	@mkdir -p $$(dir $$@)
	@$(3) $$(if $$(call weak_src,$$*),--weaken) $$< $$@.tmp
	@$(3) --redefine-syms=$(GEN)/redefine_$$(call bin_of,$$*).txt $$@.tmp $$@.tmp2
	@$(3) --weaken-symbols=config/overrides.txt $$@.tmp2 $$@
	@rm -f $$@.tmp $$@.tmp2

$$($(1)_DIR)/gen/%.o: $(GEN)/%.s
	@mkdir -p $$(dir $$@)
	@$(2) $(4) -c $$< -o $$@

$$($(1)_DIR)/port/%.o: port/%.c $(PORT_HDRS)
	@mkdir -p $$(dir $$@)
	@echo "CC   [$(1)] port/$$*"
	@$(2) $(4) $(PORT_CFLAGS) $$(PORT_EXTRA_$(1)) -c $$< -o $$@

# Two-phase link: the first pass lists what is still undefined; gen_stubs.py turns those into stubs that stop
# with their name (the to-do list is $$($(1)_DIR)/stubs.txt).
$$($(1)_DIR)/stubs.c: $$($(1)_LINK) tools/gen_stubs.py
	@$(2) $(4) -o $$($(1)_DIR)/scan.elf $$($(1)_LINK) $(GEN)/psx_syms.ld $(7) -Wl,--warn-unresolved-symbols \
		-Wl,--start-group $(8) -Wl,--end-group 2> $$($(1)_DIR)/scan.log || true
	@$(PYTHON) tools/gen_stubs.py $$($(1)_DIR)/scan.log $$@

$$($(1)_DIR)/stubs.o: $$($(1)_DIR)/stubs.c
	@$(2) $(4) $(PORT_CFLAGS) $$(PORT_EXTRA_$(1)) -w -c $$< -o $$@

$$($(1)_DIR)/ff9ds.elf: $$($(1)_LINK) $$($(1)_DIR)/stubs.o
	@echo "LD   $$@"
	@$(2) $(4) -o $$@ $$($(1)_LINK) $$($(1)_DIR)/stubs.o $(GEN)/psx_syms.ld $(7) -Wl,-Map,$$($(1)_DIR)/ff9ds.map \
		-Wl,--start-group $(8) -Wl,--end-group
endef

# DS: BlocksDS, DSi-mode memory map, Thumb.
SPECS    := $(BLOCKSDS)/sys/crts/dsi_arm9.specs
DS_ARCH  := -mcpu=arm946e-s -mthumb -mthumb-interwork
PORT_EXTRA_ds := -specs=$(SPECS) -I$(BLOCKSDS)/libs/libnds/include $(if $(AUTORUN),-DPORT_AUTORUN)
$(eval $(call target_rules,ds,arm-none-eabi-gcc,arm-none-eabi-objcopy,$(DS_ARCH),ds,arm,\
	-specs=$(SPECS) -L$(BLOCKSDS)/libs/libnds/lib -Wl$(,)--gc-sections,-lnds9 -lc))

# PC: 32-bit, non-PIE (psx_ram is mapped at a fixed address, like the DS's main RAM), 8-byte doubles like
# ARM/MIPS. psx_ram is placed with --defsym and mapped by port/pc before anything runs.
PC_ARCH  := -m32 -malign-double -fno-pie
PORT_EXTRA_pc :=
$(eval $(call target_rules,pc,gcc,objcopy,$(PC_ARCH),pc,x86,-no-pie -Wl$(,)--defsym=psx_ram=0x02000000,-lm))

ARM7ELF  := $(BLOCKSDS)/sys/arm7/main_core/arm7_minimal.elf
NDSTOOL  := $(BLOCKSDS)/tools/ndstool/ndstool

ds: $(BUILD)/ff9ds.nds
pc: $(BUILD)/pc/ff9ds

$(BUILD)/ff9ds.nds: $(BUILD)/ds/ff9ds.elf
	@echo "NDS  $@"
	@$(NDSTOOL) -c $@ -7 $(ARM7ELF) -9 $< -b $(BLOCKSDS)/sys/icon.bmp "FF9 DS;port build;not for distribution"

$(BUILD)/pc/ff9ds: $(BUILD)/pc/ff9ds.elf
	@cp $< $@

decomp-objs: $(ds_DOBJS)

# --- disc data (host) ----------------------------------------------------------------------------------------
# Needs the four disc images in disks/us/raw/ ("FF9 CD1.img" ... as in the decomp). The decomp's split only needs
# the exe and the code overlays; they are copied into the submodule, whose container can't follow links.
extract:
	$(MAKE) -C $(DECOMP) extract extract-img extract-ovl DISC_DIR=$(CURDIR)/disks/$(VERSION)/raw
	cd $(DECOMP) && tools/docker.sh make split

clean:
	rm -rf $(BUILD)
