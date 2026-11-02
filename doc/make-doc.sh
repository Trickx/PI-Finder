#!/bin/bash
# ============================================================================
# make-doc.sh - prepares the compilation of the Finder documentation
# ----------------------------------------------------------------------------
# Creates doc/build/system/, an empty PIDoc system (as the compiler's
# "Generate new PIDoc system") and prints the command that compiles doc/Finder.pidoc in PixInsight.
#
# The compiler writes into the target system selected the last time in its
# dialog (Script > Development > DocumentationCompiler, Target System), also
# when run from the Process Console. It is only saved after a compilation in
# the dialog: select doc/build/system there and compile Finder.pidoc once
# from the dialog; closing it without compiling discards the selection.
#
# Result: doc/build/system/tools/Finder/Finder.html (and images/), installed
# with the module as doc/tools/Finder/; Browse Documentation opens it.
#
# Usage: ./doc/make-doc.sh [--pi=<PixInsight dir>]
# ============================================================================

set -euo pipefail

DOC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM="$DOC/build/system"
PI_DIR="/Applications/PixInsight"

for arg in "$@"; do
   case "$arg" in
      --pi=*) PI_DIR="${arg#*=}" ;;
      *)      echo "Usage: $0 [--pi=<PixInsight dir>]"; exit 1 ;;
   esac
done

if [ ! -d "$SYSTEM/pidoc/css" ]; then
   mkdir -p "$SYSTEM/docs" "$SYSTEM/pjsr/objects" "$SYSTEM/scripts" "$SYSTEM/tools"
   for d in css graphics icons images scripts titles; do
      mkdir -p "$SYSTEM/pidoc/$d"
      if [ -d "$PI_DIR/doc/pidoc/$d" ]; then
         find "$PI_DIR/doc/pidoc/$d" -maxdepth 1 -type f -exec cp {} "$SYSTEM/pidoc/$d/" \;
      fi
   done
   echo "Created PIDoc system: $SYSTEM"
fi

# The compiler copies the images referenced by the document (doc/images/)
# from the folder of the source file.
cat <<EOF

Compile it in PixInsight, in the Process Console:

   run -a="--no-backup" -a="$DOC/Finder.pidoc" -x=auto "$PI_DIR/src/scripts/PIDocCompiler/pidoc.js"

The target system must be set in the dialog of the compiler (run it without
arguments, Target System), to:

   $SYSTEM

and saved by compiling Finder.pidoc there once; closing the dialog without
compiling discards it. The console prints the paths written: they must start
with this folder.

Result: $SYSTEM/tools/Finder/
EOF
