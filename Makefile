.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif
include $(DEVKITARM)/ds_rules

TARGET		:=	dsfirmverify
BUILD		:=	obj

CFILES		:=	main.c aes.c sha256.c
BINFILES	:=	blowfish_retail.bin blowfish_dev.bin

ARCH		:=	-mthumb -mthumb-interwork
CFLAGS		:=	-g $(ARCH) -O2 -fdiagnostics-color=always -D_GNU_SOURCE -DARM9 \
				-Wall -Wextra -pedantic -std=gnu11 \
				-march=armv5te -mtune=arm946e-s \
				-fomit-frame-pointer -ffast-math \
				-I$(LIBNDS)/include -I$(TOPDIR)/libncgc/include
ASFLAGS		:=	-g $(ARCH)
LDFLAGS		=	-specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS		:=	-lnds9 -lncgc

ifneq ($(BUILD),$(notdir $(CURDIR)))

export TOPDIR	:=	$(CURDIR)

.PHONY: $(BUILD) libncgc clean

$(BUILD): libncgc
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

libncgc:
	@$(MAKE) PLATFORM=ntr -C $(CURDIR)/libncgc

clean:
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

else

export LD		:=	$(CC)
export VPATH	:=	$(TOPDIR)/src $(TOPDIR)/src/aes
export OUTPUT	:=	$(TOPDIR)/$(TARGET)
export DEPSDIR	:=	$(TOPDIR)/$(BUILD)
export OFILES	:=	$(BINFILES:.bin=.o) $(CFILES:.c=.o)
export LIBPATHS	:=	-L$(LIBNDS)/lib -L$(TOPDIR)/libncgc/out/ntr
DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

%.o: %.bin
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

endif
