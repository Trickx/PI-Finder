#!/bin/bash
# ============================================================================
# sign.sh - signs the built Finder module(s) for PixInsight
# ----------------------------------------------------------------------------
# PixInsight only loads signed modules. This script signs every module found
# in ./bin/<platform>-<arch>/ (or the module files given as arguments) with
# the developer's XSSK key; PixInsight writes a .xsgn file next to each module.
# The key password is asked for interactively.
#
# Usage: ./sign.sh [--pi=<PixInsight dir>] [<module file> ...]
# ============================================================================

set -e

XSSK_FILE="/Volumes/DriveExtender/PixInsight/CPD/CPD_SvenKopetzki.xssk"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PI_DIR=""
MODULES=()

case "$(uname -s)" in
   Darwin*)               PLATFORM="macosx" ;;
   Linux*)                PLATFORM="linux" ;;
   MINGW*|MSYS*|CYGWIN*)  PLATFORM="windows" ;;
   *) echo "Unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac

for arg in "$@"; do
   case "$arg" in
      --pi=*) PI_DIR="${arg#*=}" ;;
      -*)     echo "Usage: $0 [--pi=<PixInsight dir>] [<module file> ...]"; exit 1 ;;
      *)      MODULES+=( "$arg" ) ;;
   esac
done

if [ -z "$PI_DIR" ]; then
   case "$PLATFORM" in
      macosx)  PI_DIR="/Applications/PixInsight" ;;
      linux)   PI_DIR="/opt/PixInsight" ;;
      windows) PI_DIR="/c/Program Files/PixInsight" ;;
   esac
fi

case "$PLATFORM" in
   macosx)  PI_EXE="$PI_DIR/PixInsight.app/Contents/MacOS/PixInsight" ;;
   linux)   PI_EXE="$PI_DIR/bin/PixInsight" ;;
   windows) PI_EXE="$PI_DIR/bin/PixInsight.exe" ;;
esac

if [ ! -x "$PI_EXE" ]; then
   echo "PixInsight not found: $PI_EXE (use --pi=<PixInsight dir>)" >&2
   exit 1
fi
if [ ! -f "$XSSK_FILE" ]; then
   echo "Signing key not found: $XSSK_FILE" >&2
   exit 1
fi

if [ ${#MODULES[@]} -eq 0 ]; then
   shopt -s nullglob
   MODULES=( "$REPO_ROOT"/bin/*/Finder-pxm.{dylib,so,dll} )
   shopt -u nullglob
   if [ ${#MODULES[@]} -eq 0 ]; then
      echo "No built modules found in $REPO_ROOT/bin; run ./build.sh first." >&2
      exit 1
   fi
fi

read -r -s -p "Password for $(basename "$XSSK_FILE"): " XSSK_PASSWORD
echo
if [ -z "$XSSK_PASSWORD" ]; then
   echo "No password given." >&2
   exit 1
fi

for module in "${MODULES[@]}"; do
   echo "Signing $module ..."
   # LC_ALL=C: the ISO 8601 time stamps in the key file are misread under
   # locales with a decimal comma.
   LC_ALL=C "$PI_EXE" --sign-module-file="$module" --xssk-file="$XSSK_FILE" --xssk-password="$XSSK_PASSWORD"
done

unset XSSK_PASSWORD
echo "Done."
