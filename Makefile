# TurkishDPI build (MinGW-w64 GCC or llvm-mingw).
#   make ARCH=x86_64 WINDIVERT=path/to/WinDivert-2.2.2-A [EMBED=1]
#   make ARCH=i686   WINDIVERT=path/to/WinDivert-2.2.2-A [EMBED=1]
# EMBED=1 packs WinDivert.dll + driver(s) into the exe (single-file build).
ARCH      ?= x86_64
CPREFIX   ?= $(ARCH)-w64-mingw32-
CC        := $(CPREFIX)gcc
WINDRES   := $(CPREFIX)windres
WINDIVERT ?= third_party/WinDivert
OUT       ?= build/$(ARCH)

ifeq ($(ARCH),x86_64)
WD_BIN := $(WINDIVERT)/x64
else
WD_BIN := $(WINDIVERT)/x86
endif

CFLAGS  := -std=gnu11 -O2 -pipe -Wall -Wextra -Wno-unused-parameter \
           -Wno-sign-compare -Wno-missing-field-initializers \
           -DWINDIVERTEXPORT=extern \
           -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 \
           -I$(WINDIVERT)/include -Isrc/engine -Isrc/app \
           -ffunction-sections -fdata-sections
LDFLAGS := -mwindows -s -static -Wl,--gc-sections \
           -Wl,--dynamicbase,--nxcompat
ifeq ($(ARCH),x86_64)
LDFLAGS += -Wl,--high-entropy-va
endif
LIBS    := -lws2_32 -lwinhttp -liphlpapi -lshell32 -lole32 -luser32 -ladvapi32 -luuid -lgdiplus -lcomctl32 -lgdi32

ENGINE_SRC := $(wildcard src/engine/*.c src/engine/utils/*.c)
APP_SRC    := $(wildcard src/app/*.c)
OBJS := $(patsubst src/%.c,$(OUT)/obj/%.o,$(ENGINE_SRC) $(APP_SRC)) $(OUT)/obj/res.o
ifeq ($(EMBED),1)
OBJS += $(OUT)/obj/embed.o
endif

.PHONY: all clean
all: $(OUT)/TurkishDPI.exe

$(OUT)/obj/%.o: src/%.c $(wildcard src/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/obj/res.o: src/app/TurkishDPI.rc src/app/TurkishDPI.exe.manifest $(wildcard src/app/res/*.ico) src/app/version.h
	@mkdir -p $(dir $@)
	$(WINDRES) -Isrc/app src/app/TurkishDPI.rc -O coff -o $@

$(OUT)/obj/embed.o: $(WD_BIN)/WinDivert.dll
	@mkdir -p $(dir $@)
	echo '101 RCDATA "$(WD_BIN)/WinDivert.dll"'   >  $(OUT)/obj/embed.rc
	echo '102 RCDATA "$(WD_BIN)/WinDivert64.sys"' >> $(OUT)/obj/embed.rc
	if [ -f "$(WD_BIN)/WinDivert32.sys" ]; then echo '103 RCDATA "$(WD_BIN)/WinDivert32.sys"' >> $(OUT)/obj/embed.rc; fi
	$(WINDRES) $(OUT)/obj/embed.rc -O coff -o $@

$(OUT)/TurkishDPI.exe: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -rf build
