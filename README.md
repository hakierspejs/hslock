# HSLock

## Building

```bash
./build.sh [options]
```

| Option | Description |
| -------- | ------------- |
| `--clean` | Remove build directory before building |
| `--flash` | Flash firmware after building |
| `--erase` | Erase entire flash (remove all keys) |

Device must have the board connected in BOOTSEL mode for `--flash` and `--erase`.

## Connect

Use PuTTY or screen to connect via serial USB.

```sh
putty -serial /dev/ttyACM0 -sercfg 115200,8,n,1,N
```

```sh
screen /dev/ttyACM0 115200
```
