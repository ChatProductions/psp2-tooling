VITASDK ?= $(HOME)/vitasdk
PREFIX := $(VITASDK)/bin/arm-vita-eabi
CC := $(PREFIX)-gcc
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -nostdlib -fno-builtin
LIBS := -lSceIofilemgrForDriver_stub -lSceSysclibForDriver_stub \
	-lSceThreadmgrForDriver_stub -ltaihenForKernel_stub \
	-lSceModulemgrForKernel_363_stub

all: a2dp_probe.skprx

build:
	mkdir -p build

build/a2dp_probe.elf: a2dp_probe.c | build
	$(CC) $(CFLAGS) -Wl,-q $< $(LIBS) -o $@

build/a2dp_probe.velf: build/a2dp_probe.elf exports.yml
	$(VITASDK)/bin/vita-elf-create -e exports.yml $< $@

a2dp_probe.skprx: build/a2dp_probe.velf
	$(VITASDK)/bin/vita-make-fself -c $< $@

clean:
	rm -rf build a2dp_probe.skprx

.PHONY: all clean
