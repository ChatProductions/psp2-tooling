VITASDK ?= $(HOME)/vitasdk
PREFIX := $(VITASDK)/bin/arm-vita-eabi
CC := $(PREFIX)-gcc

TITLE_ID := VTA2DP001
TARGET := VitaA2DP
SRC := main.c ../vendor/libsbc/src/sbc.c ../vendor/libsbc/src/bits.c
OBJS := build/main.o build/sbc.o build/bits.o

CFLAGS := -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I../vendor/libsbc/include
LIBS := -L../vita-a2dp-probe/stubs -lvita_a2dp_probe_stub_weak \
	-lvita2d -lfreetype -lpng -ljpeg -lz -lbz2 \
	-lSceDisplay_stub -lSceGxm_stub -lSceCtrl_stub -lSceAudio_stub \
	-lScePgf_stub -lSceCommonDialog_stub -lSceAppMgr_stub -lSceAppUtil_stub \
	-lSceSysmodule_stub -lSceTouch_stub -lScePower_stub \
	-lpthread -lm -lc

all: $(TARGET).vpk

build:
	mkdir -p build

build/main.o: main.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/sbc.o: ../vendor/libsbc/src/sbc.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/bits.o: ../vendor/libsbc/src/bits.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/$(TARGET).elf: $(OBJS) ../vita-a2dp-probe/stubs/libvita_a2dp_probe_stub_weak.a
	$(CC) -Wl,-q $(OBJS) $(LIBS) -o $@

build/$(TARGET).velf: build/$(TARGET).elf
	$(VITASDK)/bin/vita-elf-create $< $@

build/eboot.bin: build/$(TARGET).velf
	$(VITASDK)/bin/vita-make-fself -s $< $@

build/param.sfo: | build
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=$(TITLE_ID) -s APP_VER=00.70 "Vita A2DP SBC" $@

$(TARGET).vpk: build/eboot.bin build/param.sfo ../vita-a2dp-probe/a2dp_probe.skprx
	$(VITASDK)/bin/vita-pack-vpk -s build/param.sfo -b build/eboot.bin \
		-a ../vita-a2dp-probe/a2dp_probe.skprx=a2dp_probe.skprx $@

clean:
	rm -rf build $(TARGET).vpk

.PHONY: all clean
