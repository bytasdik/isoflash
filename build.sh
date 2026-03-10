#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  ISO Writer — Build Script
#  Usage:  chmod +x build.sh && ./build.sh
# ─────────────────────────────────────────────────────────────────────────────
set -e

echo ""
echo "  ⬡ ISO WRITER — Build Script"
echo "  ─────────────────────────────"

# Check compiler
if ! command -v g++ &>/dev/null; then
    echo "  ✖ g++ not found. Install build-essential:"
    echo "    sudo apt install build-essential"
    exit 1
fi

CXX=${CXX:-g++}
SRC=iso_writer.cpp
BIN=iso_writer

echo "  ● Compiling $SRC → $BIN …"
$CXX -std=c++17 -O2 -Wall -Wextra \
     -o "$BIN" "$SRC" \
     -lpthread

echo "  ✔ Build successful: ./$BIN"
echo ""
echo "  USAGE:"
echo "    sudo ./$BIN       (root required for USB write access)"
echo ""
echo "  KEYBOARD SHORTCUTS:"
echo "    Tab / ← →   Switch panels"
echo "    ↑ ↓         Navigate drive list"
echo "    Enter       Select / confirm"
echo "    F5          Refresh USB drives"
echo "    q / Esc     Quit"
echo ""
