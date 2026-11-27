#!/bin/sh
# Compile the ColdfireEMAC language and link it into Ghidra's user Extensions.
#
# The module stays in the repo (tools/coldfire/ghidra/ColdfireEMAC, see its README).
# Ghidra follows a symlink in <UserSettings>/Extensions in both analyzeHeadless
# and pyghidra, so a Homebrew upgrade does not remove the module. The settings
# folder name contains the Ghidra version, so re-run this after an upgrade, and
# after the checkout the link points into moves or is removed (the link then
# dangles and the language is gone). The settings folder is ~/Library/ghidra on
# macOS and $XDG_CONFIG_HOME/ghidra (~/.config/ghidra) on Linux.
set -e
GHIDRA="${GHIDRA_INSTALL_DIR:-/opt/homebrew/Cellar/ghidra/12.1.3/libexec}"
HERE=$(cd "$(dirname "$0")" && pwd)
MOD="$HERE/ColdfireEMAC"
PROPS="$GHIDRA/Ghidra/application.properties"
VER=$(sed -n 's/^application.version=//p' "$PROPS")
REL=$(sed -n 's/^application.release.name=//p' "$PROPS")
"$GHIDRA/support/sleigh" "$MOD/data/languages/coldfire_emac.slaspec"
cat > "$MOD/extension.properties" <<EOF
name=ColdfireEMAC
description=ColdFire language with MOVCLR and corrected EMAC instructions
author=digikit
createdOn=
version=$VER
EOF
case $(uname -s) in
Darwin) SETTINGS="$HOME/Library/ghidra" ;;
*) SETTINGS="${XDG_CONFIG_HOME:-$HOME/.config}/ghidra" ;;
esac
EXT="$SETTINGS/ghidra_${VER}_${REL}/Extensions"
mkdir -p "$EXT"
ln -sfn "$MOD" "$EXT/ColdfireEMAC"
echo "linked $EXT/ColdfireEMAC -> $MOD (68000:BE:32:ColdfireEMAC)"
