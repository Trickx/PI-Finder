######################################################################
# Finder PixInsight module - macOS makefile
#
# Required environment (set by ../../build.sh):
#   PCLINCDIR, PCLSRCDIR, PCLLIBDIR64, PCLBINDIR64
#
# Usage: make ARCH=arm64|x64
# Compiler flags follow the PCL makefiles of PixInsight 1.9.x.
######################################################################

ARCH ?= arm64

ifeq ($(ARCH),x64)
   CLANG_ARCH = x86_64
   ISA_FLAGS  = -msse4.2
else
   CLANG_ARCH = arm64
   ISA_FLAGS  =
endif

MACOSX_SDK ?= $(shell xcrun --sdk macosx --show-sdk-path)
SDK     = $(MACOSX_SDK)
OBJ_DIR = ./$(ARCH)/Release
MODULE  = Finder-pxm.dylib

SRC_FILES = $(wildcard ../../src/*.cpp)

OBJ_FILES = $(patsubst ../../src/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))
DEP_FILES = $(OBJ_FILES:%.o=%.d)

CXXFLAGS = -c -pipe -pthread -arch $(CLANG_ARCH) -fPIC -isysroot $(SDK) -mmacosx-version-min=14 \
   -D_REENTRANT -D__PCL_MACOSX -I"$(PCLINCDIR)" -I"$(PCLSRCDIR)/3rdparty" $(ISA_FLAGS) \
   -minline-all-stringops -O3 -ffunction-sections -fdata-sections -ffast-math -ffp-contract=on \
   -Wno-overriding-option -fvisibility=hidden -fvisibility-inlines-hidden -std=c++20 -stdlib=libc++ \
   -Wall -Wno-parentheses -Wno-missing-braces -Wno-nan-infinity-disabled -Wno-extern-c-compat

LDFLAGS = -arch $(CLANG_ARCH) -fPIC -headerpad_max_install_names -Wl,-syslibroot,$(SDK) \
   -mmacosx-version-min=14 -stdlib=libc++ -Wl,-dead_strip -dynamiclib \
   -install_name @executable_path/$(MODULE) -L"$(PCLLIBDIR64)"

LIBS = -framework AppKit -lpthread -lPCL-pxi -llz4-pxi -lzstd-pxi -lzlib-pxi -lRFC6234-pxi -llcms-pxi

.PHONY: all
all: $(OBJ_DIR)/$(MODULE)
	mkdir -p $(PCLBINDIR64)
	cp $(OBJ_DIR)/$(MODULE) $(PCLBINDIR64)/

-include $(DEP_FILES)

$(OBJ_DIR)/$(MODULE): $(OBJ_FILES)
	clang++ $(LDFLAGS) -o $@ $(OBJ_FILES) $(LIBS)

$(OBJ_DIR)/%.o: ../../src/%.cpp
	@mkdir -p $(@D)
	clang++ $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -o"$@" "$<"

.PHONY: clean
clean:
	rm -f $(OBJ_FILES) $(DEP_FILES) $(OBJ_DIR)/$(MODULE)
