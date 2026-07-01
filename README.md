# HSLock

## Building

```sh
./build.sh
```

## Install

```sh
picotool load build/hslock.elf -f
picotool reboot
```

## Connect

Use PuTTY to connect via serial USB.

```sh
putty -serial /dev/ttyACM0 -sercfg 115200,8,n,1,N
```
