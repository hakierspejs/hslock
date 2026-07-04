#!/bin/bash
set -e

CLEAN=0
FLASH=0
ERASE=0

for arg in "$@"; do
    case $arg in
        --clean) CLEAN=1 ;;
        --flash) FLASH=1 ;;
        --erase) ERASE=1 ;;
    esac
done

cd "$(dirname "$0")"

if [ $CLEAN -eq 1 ]; then
    echo "cleaning build..."
    rm -rf build
    rm -f hslock.uf2
    exit 0
fi

mkdir -p build
cd build
cmake -DPICO_BOARD=pico_w ..
make -j$(nproc)
cd ..

cp build/hslock.uf2 hslock.uf2
echo "build ok - hslock.uf2 ready"

if [ $ERASE -eq 1 ]; then
    echo "erasing flash..."
    picotool erase -f
    echo "erase ok"
fi

if [ $FLASH -eq 1 ]; then
    echo "flashing..."
    picotool load build/hslock.elf -f
    echo "flash ok"
    echo "rebooting..."
    picotool reboot
fi
