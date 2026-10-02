######################################################################
# Finder PixInsight module - Linux makefile
#
# Required environment (set by ../../build.sh):
#   PCLINCDIR, PCLSRCDIR, PCLLIBDIR64, PCLBINDIR64
#
# Usage: make
# Compiler flags follow the PCL makefiles of PixInsight 1.9.x.
######################################################################

ARCH   ?= x64
OBJ_DIR = ./$(ARCH)/Release
MODULE  = Finder-pxm.so

SRC_FILES = $(wildcard ../../src/*.cpp)
OBJ_FILES = $(patsubst ../../src/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))
DEP_FILES = $(OBJ_FILES:%.o=%.d)

CXXFLAGS = -c -pipe -pthread -m64 -fPIC -D_REENTRANT -D__PCL_LINUX -D__PCL_AVX2 -D__PCL_FMA \
   -I"$(PCLINCDIR)" -I"$(PCLSRCDIR)/3rdparty" -mavx2 -mfma -minline-all-stringops -O3 \
   -ffunction-sections -fdata-sections -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden \
   -fnon-call-exceptions -std=c++20 -Wall -Wno-parentheses

LDFLAGS = -m64 -fPIC -pthread -Wl,--enable-new-dtags -Wl,-z,noexecstack -Wl,-O1 -Wl,--gc-sections \
   -rdynamic -shared -L"$(PCLLIBDIR64)"

LIBS = -lpthread -lPCL-pxi -llz4-pxi -lzstd-pxi -lzlib-pxi -lRFC6234-pxi -llcms-pxi

.PHONY: all
all: $(OBJ_DIR)/$(MODULE)
	mkdir -p $(PCLBINDIR64)
	cp $(OBJ_DIR)/$(MODULE) $(PCLBINDIR64)/

-include $(DEP_FILES)

$(OBJ_DIR)/$(MODULE): $(OBJ_FILES)
	g++ $(LDFLAGS) -o $@ $(OBJ_FILES) $(LIBS)

$(OBJ_DIR)/%.o: ../../src/%.cpp
	@mkdir -p $(@D)
	g++ $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -o"$@" "$<"

.PHONY: clean
clean:
	rm -f $(OBJ_FILES) $(DEP_FILES) $(OBJ_DIR)/$(MODULE)
