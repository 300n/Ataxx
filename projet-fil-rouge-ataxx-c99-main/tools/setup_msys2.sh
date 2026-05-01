#!/usr/bin/env bash
# Setup script for MSYS2 UCRT64: updates the system and installs gcc + make.
# Run this inside the MSYS2 UCRT64 terminal.
set -e
echo "=== Mise a jour de MSYS2 ==="
yes | pacman -Syu --noconfirm || true
echo "=== Installation de gcc et make ==="
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc make
echo "=== Termine ! Verifiez avec: gcc --version ==="
gcc --version
