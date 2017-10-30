.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif
include $(DEVKITARM)/ds_rules

TARGET		:=	dsfirmverify

CFILES		:=	main.c aes/aes.c sha256.c

ARCH		:=	-marm
CFLAGS		:=	-g $(ARCH) -O2 -fdiagnostics-color=always -D_GNU_SOURCE -DARM9 \
				-Wall -Wextra -pedantic -std=gnu11 \
				-march=armv5te -mtune=arm946e-s \
				-fomit-frame-pointer -ffast-math \
				-ffunction-sections -fdata-sections \
				-I$(LIBNDS)/include -Ilibncgc/include/
ASFLAGS		:=	-g $(ARCH)
LDFLAGS		=	-specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(@).map \
				-L$(LIBNDS)/lib -Llibncgc/out/ntr
LIBS		:=	-lnds9 -lncgc

# ------------------------------------------------------------------------------

OBJFILES	:=	$(OBJFILES) $(patsubst %,obj/%.o,$(CFILES)) \
				obj/blowfish_retail.bin.o obj/blowfish_dev.bin.o

$(TARGET).nds: obj/$(TARGET).nds
	@cp $^ $@
	@echo Built $@

obj/$(TARGET).elf: $(OBJFILES) libncgc/out/ntr/libncgc.a
	@echo Linking $@
	@$(CC) $(LDFLAGS) -o $@ $(OBJFILES) $(LIBS)

obj/%.c.o: src/%.c
	@mkdir -p $(dir $@)
	@echo $^ =\> $@
	@$(CC) -MMD -MP -MF obj/$*.d $(CFLAGS) -c $< -o $@ $(ERROR_FILTER)

obj/%.bin.o: src/%.bin
	@mkdir -p $(dir $@)
	@echo $^ =\> $@
	@$(PREFIX)ld -r -b binary $< -o $@

libncgc/out/ntr/libncgc.a:
	@$(MAKE) PLATFORM=ntr -C libncgc

clean:
	@rm -vrf obj $(TARGET).nds

.PHONY: clean

-include $(patsubst %,obj/%.d,$(CFILES))
