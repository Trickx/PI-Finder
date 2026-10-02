#!/bin/bash
# ============================================================================
# build.sh - builds the Finder PixInsight module
# ----------------------------------------------------------------------------
# Platforms: macOS (arm64, x64), Linux (x64), Windows (x64, run from Git Bash
# with MSBuild / Visual Studio 2022 in PATH).
#
# PCL is taken from ./build/PCL. If that directory does not exist, the PCL
# headers and sources are copied from a local PixInsight installation (which
# ships them). Alternatively, clone https://gitlab.com/pixinsight/PCL.git into
# ./build/PCL beforehand (this is what the CI workflow does).
#
# The PCL static libraries are built once per platform/architecture into
# ./build/PCL/lib/<platform>-<arch>; the module ends up in
# ./bin/<platform>-<arch>/.
#
# Usage: ./build.sh [--arch=arm64|x64] [--pi=<PixInsight dir>] [clean]
# ============================================================================

set -eE

# Report the failing command; in GitHub Actions also as an error annotation.
trap 'rc=$?; msg="build.sh: line $LINENO: \"$BASH_COMMAND\" failed with exit code $rc"; echo "$msg" >&2; [ -n "$GITHUB_ACTIONS" ] && echo "::error::$msg"; exit $rc' ERR

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ACTION="all"
ARCH=""
PI_DIR=""

case "$(uname -s)" in
   Darwin*)               PLATFORM="macosx" ;;
   Linux*)                PLATFORM="linux" ;;
   MINGW*|MSYS*|CYGWIN*)  PLATFORM="windows" ;;
   *) echo "Unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac

for arg in "$@"; do
   case "$arg" in
      --arch=*) ARCH="${arg#*=}" ;;
      --pi=*)   PI_DIR="${arg#*=}" ;;
      clean)    ACTION="clean" ;;
      *) echo "Usage: $0 [--arch=arm64|x64] [--pi=<PixInsight dir>] [clean]"; exit 1 ;;
   esac
done

if [ -z "$ARCH" ]; then
   case "$(uname -m)" in
      arm64|aarch64) ARCH="arm64" ;;
      *)             ARCH="x64" ;;
   esac
fi
if [ "$ARCH" != "arm64" ] && [ "$ARCH" != "x64" ]; then
   echo "Unsupported architecture: $ARCH" >&2
   exit 1
fi
if [ "$PLATFORM" != "macosx" ] && [ "$ARCH" != "x64" ]; then
   echo "Only x64 is supported on $PLATFORM." >&2
   exit 1
fi

if [ -z "$PI_DIR" ]; then
   case "$PLATFORM" in
      macosx)  PI_DIR="/Applications/PixInsight" ;;
      linux)   PI_DIR="/opt/PixInsight" ;;
      windows) PI_DIR="/c/Program Files/PixInsight" ;;
   esac
fi

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
TARGET="$PLATFORM-$ARCH"

export PCLDIR="$REPO_ROOT/build/PCL"
export PCLINCDIR="$PCLDIR/include"
export PCLSRCDIR="$PCLDIR/src"
export PCLLIBDIR64="$PCLDIR/lib/$TARGET"
export PCLBINDIR64="$REPO_ROOT/bin/$TARGET"
export PCLLIBDIR="$PCLLIBDIR64"
export PCLBINDIR="$PCLBINDIR64"

if [ "$ACTION" = "clean" ]; then
   rm -rf "$REPO_ROOT/build" "$REPO_ROOT/bin" \
          "$REPO_ROOT/macosx/g++/arm64" "$REPO_ROOT/macosx/g++/x64" \
          "$REPO_ROOT/linux/g++/x64" "$REPO_ROOT/windows/vc17/x64"
   echo "Cleaned."
   exit 0
fi

# ----------------------------------------------------------------------------
# 1. PCL headers and sources
# ----------------------------------------------------------------------------
if [ ! -d "$PCLSRCDIR/pcl" ]; then
   if [ ! -d "$PI_DIR/include/pcl" ] || [ ! -d "$PI_DIR/src/pcl" ]; then
      echo "PCL not found in $PCLDIR or $PI_DIR." >&2
      echo "Use --pi=<PixInsight dir> or clone https://gitlab.com/pixinsight/PCL.git into build/PCL." >&2
      exit 1
   fi
   echo "Copying PCL from $PI_DIR ..."
   mkdir -p "$PCLDIR"
   cp -R "$PI_DIR/include" "$PCLDIR/"
   cp -R "$PI_DIR/src" "$PCLDIR/"
   # Drop object trees shipped with the installation.
   find "$PCLSRCDIR" -type d \( -name arm64 -o -name x64 \) \
        \( -path "*/g++/*" -o -path "*/vc17/*" \) -prune -exec rm -rf {} +
fi
mkdir -p "$PCLLIBDIR64" "$PCLBINDIR64"

# The generated PCL makefiles hardcode the default Xcode SDK location.
if [ "$PLATFORM" = "macosx" ]; then
   XCODE_SDK="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
   export MACOSX_SDK="$(xcrun --sdk macosx --show-sdk-path)"
   if [ ! -d "$XCODE_SDK" ]; then
      find "$PCLSRCDIR" -path "*/macosx/g++/makefile-*" -exec sed -i '' "s|$XCODE_SDK|$MACOSX_SDK|g" {} +
   fi
fi

# ----------------------------------------------------------------------------
# 2. Third-party libraries and PCL
# ----------------------------------------------------------------------------
winpath()
{
   cygpath -w "$1"
}

# build_make_lib <makefile dir> <library file>
build_make_lib()
{
   local dir="$1" lib="$2" makefile="makefile-$ARCH"
   if [ -f "$PCLLIBDIR64/$lib" ]; then
      echo "$lib: up to date"
      return
   fi
   echo "Building $lib ..."
   # The generated makefiles do not create object subdirectories themselves.
   ( cd "$dir" && grep -oE "\./$ARCH/Release/[^ ]+\.o" "$makefile" | xargs -n1 dirname | sort -u | xargs mkdir -p )
   ( cd "$dir" && make -f "$makefile" -j"$JOBS" --no-print-directory > /dev/null )
}

# Locates MSBuild of Visual Studio 2022, which is not necessarily in the PATH
# of Git Bash.
find_msbuild()
{
   if command -v msbuild > /dev/null 2>&1; then
      MSBUILD="msbuild"
      return
   fi
   local vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
   if [ -f "$vswhere" ]; then
      local path
      path="$("$vswhere" -latest -version "[17.0,18.0)" -requires Microsoft.Component.MSBuild \
                -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | head -n1 | tr -d '\r')"
      if [ -n "$path" ]; then
         MSBUILD="$(cygpath -u "$path")"
         return
      fi
   fi
   echo "MSBuild (Visual Studio 2022) not found." >&2
   return 1
}

# build_msbuild_lib <vcxproj> <library file>
build_msbuild_lib()
{
   local project="$1" lib="$2"
   if [ -f "$PCLLIBDIR64/$lib" ]; then
      echo "$lib: up to date"
      return
   fi
   echo "Building $lib ..."
   "$MSBUILD" "$(winpath "$project")" -nologo -v:minimal -m -p:Configuration=Release -p:Platform=x64
}

LIBS3RD="lcms lz4 RFC6234 zlib zstd"

case "$PLATFORM" in
   macosx|linux)
      for lib in $LIBS3RD; do
         build_make_lib "$PCLSRCDIR/3rdparty/$lib/$PLATFORM/g++" "lib$lib-pxi.a"
      done
      build_make_lib "$PCLSRCDIR/pcl/$PLATFORM/g++" "libPCL-pxi.a"
      ;;
   windows)
      find_msbuild
      echo "Using MSBuild: $MSBUILD"
      # MSBuild reads these as properties; it needs native Windows paths.
      export PCLDIR="$(winpath "$PCLDIR")"
      export PCLINCDIR="$(winpath "$PCLINCDIR")"
      export PCLSRCDIR="$(winpath "$PCLSRCDIR")"
      export PCLLIBDIR64="$(winpath "$PCLLIBDIR64")"
      export PCLBINDIR64="$(winpath "$PCLBINDIR64")"
      export PCLLIBDIR="$PCLLIBDIR64"
      export PCLBINDIR="$PCLBINDIR64"
      for lib in $LIBS3RD; do
         build_msbuild_lib "$REPO_ROOT/build/PCL/src/3rdparty/$lib/windows/vc17/$lib.vcxproj" "$lib-pxi.lib"
      done
      # The public PCL repository does not include the Windows project for
      # the PCL library itself, so we provide our own.
      build_msbuild_lib "$REPO_ROOT/windows/vc17/PCL.vcxproj" "PCL-pxi.lib"
      ;;
esac

# ----------------------------------------------------------------------------
# 3. Module
# ----------------------------------------------------------------------------
echo "Building Finder module for $TARGET ..."
case "$PLATFORM" in
   macosx|linux)
      ( cd "$REPO_ROOT/$PLATFORM/g++" && make ARCH="$ARCH" -j"$JOBS" --no-print-directory )
      ;;
   windows)
      "$MSBUILD" "$(winpath "$REPO_ROOT/windows/vc17/Finder.vcxproj")" -nologo -v:minimal -m -p:Configuration=Release -p:Platform=x64
      ;;
esac

echo "Done: bin/$TARGET/"
ls -l "$REPO_ROOT/bin/$TARGET"
