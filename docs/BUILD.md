# Building the Firmware

## Dependencies

Install these before proceeding:

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) — set `PICO_SDK_PATH` in your environment
- [CMake](https://cmake.org/) 3.13+
- [GNU Arm Embedded Toolchain](https://developer.arm.com/downloads/-/gnu-rm) (`arm-none-eabi-gcc`)
- [picotool](https://github.com/raspberrypi/picotool) — optional, for `--flash` and `--erase`

On Debian based systems all dependencies can be installed with:

```bash
# Toolchain + build tools
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib build-essential git python3

# Pico SDK
git clone -b master https://github.com/raspberrypi/pico-sdk.git ~/pico/pico-sdk
cd ~/pico/pico-sdk && git submodule update --init --recursive
echo 'export PICO_SDK_PATH=~/pico/pico-sdk' >> ~/.bashrc
source ~/.bashrc

# picotool (optional)
sudo apt install libusb-1.0-0-dev
git clone https://github.com/raspberrypi/picotool.git ~/pico/picotool
cd ~/pico/picotool && mkdir build && cd build
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH ..
make -j$(nproc) && sudo make install

# USB device permissions (no sudo needed for picotool/flash)
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0666"' | sudo tee /etc/udev/rules.d/99-pico.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Clone

```bash
git clone https://github.com/hakierspejs/hslock.git
cd hslock
git submodule update --init --recursive
```

The `--recursive` flag is required — LittleFS, qrcodegen and other libraries are included as submodules.

## Build

```bash
./build.sh
```

Output: `hslock.uf2` in the project root.

### Options

| Flag | Description |
| ------ | ------------- |
| `--clean` | Remove build directory before building |
| `--flash` | Flash firmware after building (device must be in BOOTSEL mode) |
| `--erase` | Erase entire flash (wipes all stored keys and config). Make a backup using `export-keys` before you use it! |

## Flashing

Hold BOOTSEL button while plugging the device in to put it in BOOTSEL mode.

**Via drag and drop** - drag `hslock.uf2` onto the `RPI-RP2` drive.

**Via build script** - put device in BOOTSEL mode, then:

```bash
./build.sh --flash
```
