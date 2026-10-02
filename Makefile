CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -D__USE_MINGW_ANSI_STDIO=1 -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00
LDFLAGS = -shared -static-libgcc -s

DIST = dist
BUILD = build

COMMON_SOURCES = src/common/pe_image.c src/common/import_patch.c
PROXY_SOURCES = $(wildcard src/proxy/*.c) $(COMMON_SOURCES)
MINHOOK_SOURCES = $(wildcard third_party/minhook/src/*.c) third_party/minhook/src/hde/hde64.c
CORE_SOURCES = $(wildcard src/core/*.c) $(COMMON_SOURCES) $(MINHOOK_SOURCES)
# The fields mt2-mappings names, built into the core so plugins can use them by name (mt2sdk mappings fields).
MAPPINGS = ../mt2-mappings
MAPPED_FIELDS = $(BUILD)/mapped_fields.c
HEADERS = $(wildcard src/*/*.h) sdk/include/mt2loader.h sdk/include/mt2loader.hpp

# MSYS2's static cJSON still carries dllexport directives; this copy without them keeps cJSON out of the core's exports.
CJSON = $(BUILD)/libcjson_private.a

SDK_TOOL_SOURCES = $(wildcard tools/mt2sdk/*.c) src/core/symbols.c src/core/game_build.c src/core/manifest.c src/common/pe_image.c
# Zydis is compiled once on its own: it's one big generated file, and its code isn't held to this project's warnings.
ZYDIS = $(BUILD)/zydis.o
ZYDIS_FLAGS = -DZYDIS_STATIC_BUILD -DZYCORE_STATIC_BUILD

PROXY = $(DIST)/zlib1.dll
CORE = $(DIST)/mt2loader/mt2loader.dll
SDK = $(DIST)/sdk
SDK_TOOL = $(SDK)/mt2sdk.exe

.PHONY: all clean test package sdk

all: $(PROXY) $(CORE) sdk

$(PROXY): $(PROXY_SOURCES) $(HEADERS) src/proxy/zlib1.def
	@mkdir -p $(DIST)
	$(CC) $(CFLAGS) $(PROXY_SOURCES) src/proxy/zlib1.def $(LDFLAGS) -l:libz.a -o $@

$(CJSON):
	@mkdir -p $(BUILD)
	objcopy --remove-section=.drectve $(shell $(CC) -print-file-name=libcjson.a) $@

# The demangler is libiberty's cp-demangle.o alone (GPL with a linking exception, see THIRD_PARTY.txt).
CORE_LIBS = $(CJSON) -l:libiberty.a -lshell32
CORE_INCLUDES = -Ithird_party/minhook/include -Isrc/core

$(MAPPED_FIELDS): $(SDK_TOOL) $(wildcard $(MAPPINGS)/*/*.mapping)
	@mkdir -p $(BUILD)
	$(SDK_TOOL) mappings fields $(MAPPINGS) $@

$(CORE): $(CORE_SOURCES) $(MAPPED_FIELDS) $(HEADERS) $(CJSON)
	@mkdir -p $(DIST)/mt2loader
	$(CC) $(CFLAGS) $(CORE_INCLUDES) $(CORE_SOURCES) $(MAPPED_FIELDS) $(LDFLAGS) $(CORE_LIBS) -o $@

# The SDK folder as plugin authors get it: mt2sdk.exe, the headers, and the project template.
sdk: $(SDK_TOOL)
	@mkdir -p $(SDK)/include
	cp sdk/include/mt2loader.hpp sdk/include/mt2loader.h $(SDK)/include/
	rm -rf $(SDK)/template
	cp -r sdk/template $(SDK)/template
	rm -rf $(SDK)/template/mod/native $(SDK)/template/build
	rm -rf $(SDK)/ghidra
	@mkdir -p $(SDK)/ghidra
	cp tools/ghidra/*.java $(SDK)/ghidra/

$(ZYDIS): third_party/zydis/Zydis.c third_party/zydis/Zydis.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -O2 -w $(ZYDIS_FLAGS) -Ithird_party/zydis -c $< -o $@

$(SDK_TOOL): $(SDK_TOOL_SOURCES) $(HEADERS) $(wildcard tools/mt2sdk/*.h) $(CJSON) $(ZYDIS)
	@mkdir -p $(SDK)
	$(CC) $(CFLAGS) $(ZYDIS_FLAGS) -municode $(SDK_TOOL_SOURCES) $(ZYDIS) -static-libgcc -s $(CJSON) -l:libiberty.a -o $@

test: all
	bash tests/run_tests.sh
	bash tests/manager_tests.sh
	bash tests/plugin_tests.sh

package: all
	python scripts/package.py

clean:
	rm -rf $(DIST) $(BUILD)
