TITLEID := SWRV00003
TARGET := swrc_vita_viewer
CC := arm-vita-eabi-gcc
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wl,-q
LIBS := -lvitaGL -lstdc++ -lc -lSceCommonDialog_stub -lm -lSceGxm_stub \
        -lSceDisplay_stub -lSceCtrl_stub -lSceAppMgr_stub \
        -lSceKernelThreadMgr_stub -lmathneon -lvitashark -lSceShaccCgExt \
        -ltaihen_stub -lSceShaccCg_stub -lSceKernelDmacMgr_stub
all: $(TARGET).vpk

$(TARGET).vpk: eboot.bin
	vita-mksfoex -s TITLE_ID=$(TITLEID) "SWRC Performance Viewer" param.sfo
	vita-pack-vpk -s param.sfo -b eboot.bin "$@"

eboot.bin: $(TARGET).velf
	vita-make-fself -s "$<" "$@"

$(TARGET).velf: $(TARGET).elf
	vita-elf-create "$<" "$@"

$(TARGET).elf: src/main.o
	$(CC) $(CFLAGS) "$^" $(LIBS) -o "$@"

src/main.o: src/main.c
	$(CC) $(CFLAGS) -c "$<" -o "$@"

clean:
	rm -f src/main.o $(TARGET).elf $(TARGET).velf $(TARGET).vpk eboot.bin param.sfo
.PHONY: all clean
