# TurkishDPI build (MinGW-w64 GCC or llvm-mingw).
#   make ARCH=x86_64
#   make ARCH=i686
# The exe never contains WinDivert: its signed DLL and driver ship next to
# it in the release zip (see .github/workflows/build.yml).
ARCH      ?= x86_64
CPREFIX   ?= $(ARCH)-w64-mingw32-
CC        := $(CPREFIX)gcc
WINDRES   := $(CPREFIX)windres
WINDIVERT ?= third_party/WinDivert
OUT       ?= build/$(ARCH)


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

.PHONY: all clean
all: $(OUT)/TurkishDPI.exe

$(OUT)/obj/%.o: src/%.c $(wildcard src/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/obj/res.o: src/app/TurkishDPI.rc src/app/TurkishDPI.exe.manifest $(wildcard src/app/res/*.ico) src/app/version.h
	@mkdir -p $(dir $@)
	$(WINDRES) -Isrc/app src/app/TurkishDPI.rc -O coff -o $@

$(OUT)/TurkishDPI.exe: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -rf build
