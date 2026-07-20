# ESP32 Micro Ecosystem Simulator

This is a fork of [https://github.com/ootake0914-dotcom/esp32-ecosystem-sim](https://github.com/ootake0914-dotcom/esp32-ecosystem-sim) for the ESP32-S3 ES3N28P board, which has a 320x240 display (and a whole bunch of other stuff), and yet only costs around €14 (see e.g. [Aliexpress](https://www.aliexpress.com/item/1005010254147612.html)). I like to call it the "cheap obsidian display" as a nod toward the "cheap yellow display"

### Real Hardware (ESP32 + TFT)
![Real ESP32 Hardware](esp32_hardware_demo.gif)

### Python Simulator
![Ecosystem Animation](ecosystem_anim_v2.gif)

A highly optimized artificial life / ecosystem simulation for ESP32-class boards with a SPI TFT. Watch a completely autonomous ecosystem unfold on a desk toy display.

## Features

- **Multi-Core Optimization**: FreeRTOS splits work — Core 0 runs physics / AI, Core 1 pushes pixels via SPI.
- **7 Trophic Levels / Entities**: Plants, herbivores, carnivores, apex predators, spores, garbage/corpses, decomposers.
- **Boids-like AI & Physics**: Vision ranges, targeting, fleeing, friction.
- **Dynamic Tail Rendering**: Custom `drawWedgeLine` tails from movement history.
- **Zero-Player Game**: Plug in and watch the balance emerge.

## Hardware (default target)

**LCDwiki ES3N28P** (“cheap obsidian display"):

| Item | Value |
|------|-------|
| MCU | ESP32-S3 N16R8 (16 MB flash, 8 MB OPI PSRAM) |
| Display | 2.8" IPS, 240×320, ILI9341V |
| Sim world | **320×240** landscape (`setRotation(1)`) |

SPI pins (on-board, no wiring needed):

| Signal | GPIO |
|--------|------|
| MOSI | 11 |
| SCLK | 12 |
| MISO | 13 |
| CS | 10 |
| DC | 46 |
| RST | chip reset (-1) |
| Backlight | 45 |

Firmware source lives in [`src/main.cpp`](src/main.cpp) (PlatformIO layout).

## How to build & flash (PlatformIO)

Install platformio if needed:
```bash
sudo apt-get install pipx
pipx install platformio
```

Build the firmware and upload it:

```bash
./build.sh              # compile
./build.sh upload       # compile + flash (set UPLOAD_PORT if needed)
./build.sh monitor      # serial monitor
```

Or directly:

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor
```

If auto-flash fails on the Obsidian board: hold **BOOT**, tap **RESET**, release **RESET**, then release **BOOT**.

### Color tweak

Red/blue were swapped compared to the original code, so I set `SWAP_RB = false` near the top of `src/main.cpp`.

## Python Simulator

Test ecosystem balance without flashing:

```bash
pip install matplotlib
python sim.py
```

## Contributing

Feel free to fork and add creatures, weather, or environmental mechanics.
