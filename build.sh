#!/bin/bash
rm -rf build
rm -rf hslock.uf2
mkdir build && cd build
cmake -DPICO_BOARD=pico_w ..
make -j$(nproc)
cd ..
picotool load build/hslock.elf