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
LDFLAGS := -mwindows -static -Wl,--gc-sections \
           -Wl,--dynamicbase,--nxcompat
ifeq ($(ARCH),x86_64)
LDFLAGS += -Wl,--high-entropy-va
endif
# winhttp.dll and iphlpapi.dll are deliberately NOT linked: they are not
# KnownDLLs, so as static imports of this elevated exe they would be looked up
# in its (possibly user-writable) folder first. src/app/sysdll.c loads them
# from System32 at run time.
LIBS    := -lws2_32 -lshell32 -lole32 -luser32 -ladvapi32 -luuid -lgdiplus -lcomctl32 -lgdi32

# SHA-256 of the WinDivert.dll that ships next to the exe; the engine refuses
# any other DLL (src/engine/windivert_dyn.c). Taken from the WinDivert folder
# used for the build (CI: the checksum-pinned official archive).
# REQUIRE_WINDIVERT_HASH=1 (CI) turns a missing DLL into a build error.
WDARCH := $(if $(filter x86_64,$(ARCH)),x64,x86)
WINDIVERT_DLL := $(WINDIVERT)/$(WDARCH)/WinDivert.dll
# read from stdin: sha256sum escapes its output line when the path holds a backslash
WINDIVERT_DLL_SHA256 ?= $(shell [ -f "$(WINDIVERT_DLL)" ] && sha256sum < "$(WINDIVERT_DLL)" | cut -c1-64)
ifneq ($(WINDIVERT_DLL_SHA256),)
CFLAGS += -DWINDIVERT_DLL_SHA256=\"$(WINDIVERT_DLL_SHA256)\"
else ifeq ($(REQUIRE_WINDIVERT_HASH),1)
$(error $(WINDIVERT_DLL) not found: cannot pin its SHA-256)
endif
# The drivers are pinned too: WinDivert.dll installs whichever .sys sits next
# to it as a kernel driver. x64 packages ship WinDivert64.sys; x86 packages
# ship both (WinDivert64.sys is used on 64-bit Windows).
WINDIVERT_SYS64 := $(WINDIVERT)/$(WDARCH)/WinDivert64.sys
WINDIVERT_SYS32 := $(WINDIVERT)/$(WDARCH)/WinDivert32.sys
WINDIVERT_SYS64_SHA256 ?= $(shell [ -f "$(WINDIVERT_SYS64)" ] && sha256sum < "$(WINDIVERT_SYS64)" | cut -c1-64)
WINDIVERT_SYS32_SHA256 ?= $(shell [ -f "$(WINDIVERT_SYS32)" ] && sha256sum < "$(WINDIVERT_SYS32)" | cut -c1-64)
ifneq ($(WINDIVERT_SYS64_SHA256),)
CFLAGS += -DWINDIVERT_SYS64_SHA256=\"$(WINDIVERT_SYS64_SHA256)\"
else ifeq ($(REQUIRE_WINDIVERT_HASH),1)
$(error $(WINDIVERT_SYS64) not found: cannot pin its SHA-256)
endif
ifneq ($(WINDIVERT_SYS32_SHA256),)
CFLAGS += -DWINDIVERT_SYS32_SHA256=\"$(WINDIVERT_SYS32_SHA256)\"
else ifeq ($(REQUIRE_WINDIVERT_HASH)$(WDARCH),1x86)
$(error $(WINDIVERT_SYS32) not found: cannot pin its SHA-256)
endif

ENGINE_SRC := $(wildcard src/engine/*.c src/engine/utils/*.c)
APP_SRC    := $(wildcard src/app/*.c)
OBJS := $(patsubst src/%.c,$(OUT)/obj/%.o,$(ENGINE_SRC) $(APP_SRC)) $(OUT)/obj/res.o

.PHONY: all clean
all: $(OUT)/TurkishDPI.exe

$(OUT)/obj/%.o: src/%.c $(wildcard src/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# the pinned hash is compiled in: rebuild when the DLL changes
# A stamp holding the pins, rewritten only when they change: the object is
# rebuilt when a pin appears, disappears or changes - file dates alone miss
# that (a zip keeps old dates).
WDPIN_STAMP := $(OUT)/obj/wdpin.stamp
$(WDPIN_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo "$(WINDIVERT_DLL_SHA256) $(WINDIVERT_SYS64_SHA256) $(WINDIVERT_SYS32_SHA256)" > $@.tmp
	@cmp -s $@.tmp $@ || cp -f $@.tmp $@
	@rm -f $@.tmp
$(OUT)/obj/engine/windivert_dyn.o: $(WDPIN_STAMP)
.PHONY: FORCE
FORCE:

$(OUT)/obj/res.o: src/app/TurkishDPI.rc src/app/TurkishDPI.exe.manifest $(wildcard src/app/res/*.ico) src/app/version.h
	@mkdir -p $(dir $@)
	$(WINDRES) -Isrc/app src/app/TurkishDPI.rc -O coff -o $@

$(OUT)/TurkishDPI.exe: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -rf build
