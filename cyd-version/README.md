# ESP32 Micro Ecosystem Simulator - CYD (Cheap Yellow Display) Version

This directory contains the **Cheap Yellow Display (CYD / ESP32-2432S028)** port of the ESP32 Micro Ecosystem Simulator.

---

## Features

- **Designed for CYD (ESP32-2432S028)**: Specifically calibrated for the 2.8" TFT display and integrated resistive touch screen.
- **80MHz High-Speed SPI Support**: Tuned to run at up to **80MHz SPI frequency** (`SPI_FREQUENCY 80000000`) for maximum TFT frame rate and ultra-smooth rendering.
- **Enhanced HUD & UI**: Displays live ecosystem metrics (Plants, Herbivores, Carnivores, Apex Predators, Spores, Corpses, Decomposers) alongside population graphs and status indicators.
- **Interactive Touch Controls**: Tap on the screen to trigger environmental events (Spawn Rain/Plants, Clear Virus/Spores, Reset Population).
- **Dual-Core FreeRTOS Multithreading**: Core 0 handles simulation physics, genetic evolution, and AI logic, while Core 1 renders frame updates to the TFT display.

---

## Hardware Requirements

- **Board**: ESP32-2432S028 (Cheap Yellow Display / CYD) - 2-USB Port Version (Micro-USB + Type-C)
- **Display**: 2.8" TFT (240x320)
- **Touch**: XPT2046 Touch Controller

---

## Installation & Setup

1. Open `cyd-ecosystem-sim.ino` in Arduino IDE.
2. Install the required libraries:
   - **TFT_eSPI** (by Bodmer)
   - **XPT2046_Touchscreen**
3. Configure your `User_Setup.h` in the `TFT_eSPI` library to match CYD pin assignments and high-speed clock:
   - `TFT_MISO`: 12
   - `TFT_MOSI`: 13
   - `TFT_SCLK`: 14
   - `TFT_CS`: 15
   - `TFT_DC`: 2
   - `TFT_BL`: 21
   - `SPI_FREQUENCY`: `80000000` (80MHz high-speed bus)
4. Flash the sketch to your ESP32 CYD board.
