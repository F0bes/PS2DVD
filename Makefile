EE_BIN ?= ps2dvd.elf
EE_OBJS = ps2dvd.o tex/dvd_tex.o microprogram.o
EE_LIBS = -lkernel -lgraph -lpacket -ldma -ldraw

EE_DVP = dvp-as

# Git version
GIT_VERSION := "$(shell git describe --abbrev=4 --always --tags)"

EE_CFLAGS = -I$(shell pwd) -Werror -DGIT_VERSION="\"$(GIT_VERSION)\""

all: $(EE_BIN)

tex/dvd_tex.c: tex/dvd_tex.raw
	bin2c $< tex/dvd_tex.c dvd_tex

%.o: %.vsm
	$(EE_DVP) $< -o $@

clean:
	rm -f $(EE_OBJS) $(EE_BIN)

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

wsl: $(EE_BIN)
	$(PCSX2) --elf="$(shell wslpath -w $(shell pwd))/$(EE_BIN)"

emu: $(EE_BIN)
	$(PCSX2) --elf="$(shell pwd)/$(EE_BIN)"

reset:
	ps2client reset
	ps2client netdump

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
