#!/usr/bin/env bash
# Run the DS build in no$gba (DSi mode) under Wine, for DSi-mode testing. It boots the DSi menu from the NAND and
# starts the cartridge by pressing A (tools/xkey.py, XTest).
#
#   tools/nogba.sh setup NOGBA_DIR    once: copy your no$gba DSi setup (no$gba 2.8 with DSi BIOS and NAND, your own
#                                     dumps) to $FF9DS_NOGBA and build its DSi SD card image from disks/
#   tools/nogba.sh run [ROM] [SECS]   run ROM (default build/ff9ds.nds) for SECS seconds (default 120), save a
#                                     screenshot to build/nogba.png, then close no$gba
#
# Nothing here goes into the repo: the no$gba copy, its NAND and the SD image (with the game's disc image on it)
# live in $FF9DS_NOGBA (default ~/.local/share/ff9ds-nogba).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="${FF9DS_NOGBA:-$HOME/.local/share/ff9ds-nogba}"
SD_SIZE_MB=1024

case "${1:-}" in
setup)
    SRC="${2:?usage: tools/nogba.sh setup NOGBA_DIR}"
    mkdir -p "$DIR"
    cp -a "$SRC"/. "$DIR"/
    rm -f "$DIR/DSI-SD.ZIP"
    # The DSi SD card: FAT32, with ff9ds/raw/FF9 CD1.img as on a real card.
    IMG="$DIR/DSI-1.SD"
    rm -f "$IMG"
    truncate -s ${SD_SIZE_MB}M "$IMG"
    mkfs.fat -F 32 -n FF9DS "$IMG" > /dev/null
    mmd -i "$IMG" ::/ff9ds ::/ff9ds/raw
    mcopy -i "$IMG" "$ROOT/disks/us/raw/FF9 CD1.img" "::/ff9ds/raw/FF9 CD1.img"
    echo "no\$gba set up in $DIR"
    ;;
run)
    ROM="$(readlink -f "${2:-$ROOT/build/ff9ds.nds}")"
    SECS="${3:-120}"
    cd "$DIR"
    DISPLAY="${DISPLAY:-:1}" timeout "$SECS" wine 'NO$GBA.EXE' "Z:${ROM//\//\\}" > "$ROOT/build/nogba.log" 2>&1 &
    PID=$!
    # no$gba boots the DSi menu with the cartridge selected: once it's up, press A (no$gba's default key: Z)
    WID=""
    for _ in $(seq 1 30); do
        WID=$(DISPLAY="${DISPLAY:-:1}" xwininfo -root -tree | grep -i 'no\$gba' | grep -v "has no name" | head -1 | awk '{print $1}')
        [ -n "$WID" ] && break
        sleep 1
    done
    sleep 8
    [ -n "$WID" ] && DISPLAY="${DISPLAY:-:1}" python3 "$ROOT/tools/xkey.py" "$WID" z wait:2 z
    END=$(( $(date +%s) + SECS - 5 ))
    until [ "$(date +%s)" -ge "$END" ] || ! kill -0 $PID 2>/dev/null; do sleep 2; done
    WID=$(DISPLAY="${DISPLAY:-:1}" xwininfo -root -tree | grep -i 'no\$gba' | grep -v "has no name" | head -1 | awk '{print $1}')
    if [ -n "$WID" ]; then
        DISPLAY="${DISPLAY:-:1}" import -window "$WID" "$ROOT/build/nogba.png"
        echo "screenshot: build/nogba.png"
    fi
    # close no$gba (and Wine's helpers for it)
    kill $PID 2>/dev/null || true
    pkill -f 'NO\$GBA.EXE' 2>/dev/null || true
    wineserver -k 2>/dev/null || true
    ;;
*)
    sed -n 2,11p "$0"
    exit 1
    ;;
esac
