# ESP32-C3 Headless Smart Coffee Scale

A headless Bluetooth Low Energy (BLE) coffee scale built on an **ESP32-C3 SuperMini** and **HX711** load cell amplifier. It emulates the **Decent Scale** BLE GATT protocol (`0xFFF0` / `0xFFF4` / `0xFFF3`) at 10 Hz for native compatibility with **Beanconqueror**, powered directly from a **USB-C powerbank**.

---

## Hardware Bill of Materials (BOM)

| Component | Specification | Qty | Purpose |
| :--- | :--- | :--- | :--- |
| **MCU** | ESP32-C3 SuperMini | 1 | BLE 5.0 controller, native USB-C (`USB-CDC`), onboard 3.3V LDO |
| **ADC / Amp** | HX711 24-bit Load Cell Amplifier | 1 | Reads 4-wire Wheatstone bridge load cell |
| **Load Cell** | 3 kg Micro Aluminum Bar Load Cell | 1 | Weight sensor |
| **Power** | USB-C Powerbank + USB-C Cable | 1 | Supplies 5V power via the ESP32-C3 SuperMini USB-C port |
| **Decoupling** | 100 nF Ceramic Capacitor | 1 | Recommended across HX711 `VCC` and `GND` |
| **Keep-Alive** *(Optional)* | 68 Ω 1 W resistor + `2N2222`/`S8050` NPN + 1 kΩ resistor | 1 set | Prevents powerbank auto-shutoff (only needed if bare powerbank turns off) |

---

## Pinout (ESP32-C3 SuperMini)

| Peripheral / Signal | ESP32-C3 SuperMini Pin | Notes |
| :--- | :--- | :--- |
| **HX711 `VCC`** | `3V3` | Use regulated 3.3V output — **never** 5V |
| **HX711 `GND`** | `GND` | Common ground |
| **HX711 `DOUT`** | `GPIO 6` | Non-strapping GPIO |
| **HX711 `SCK`** | `GPIO 7` | Non-strapping GPIO |
| **Keep-Alive Base** *(Optional)* | `GPIO 10` | Via 1 kΩ base resistor; pulsed 150 ms every 5 s |
| **Keep-Alive Load** *(Optional)* | `5V` (VBUS) | Via 68 Ω 1 W resistor to NPN collector |

> Avoid strapping pins **GPIO 2, 8, 9** and **GPIO 5** (ADC2). See [HARDWARE_SETUP.md](docs/HARDWARE_SETUP.md) for full wiring schematics and mechanical mounting details.

---

## Build, Upload & Calibrate (PlatformIO)

All firmware files live in [firmware/](firmware/).

```bash
cd firmware

# Build firmware for ESP32-C3 SuperMini
pio run

# Upload firmware over USB-C and open the Serial Monitor (115200 baud)
pio run -t upload -t monitor
```

### Serial CLI Commands (`115200` baud)
* **`c` + Enter** — Run interactive 2-step calibration (zero offset + known reference weight) and persist to NVS flash.
* **`t` + Enter** — Trigger a local tare.

---

## Documentation & Tools

* **[Hardware Setup Guide](docs/HARDWARE_SETUP.md)** — BOM, ESP32-C3 SuperMini pinout, optional fail-safe keep-alive circuit, and mechanical strain isolation.
* **[Beanconqueror Pairing Guide](docs/BEANCONQUEROR_GUIDE.md)** — Pairing steps, remote tare, flow-rate graphing, and troubleshooting.
* **[BLE Test Client](tools/test_client.py)** — Python (`bleak`) CLI tool to stream 10 Hz Decent Scale notifications, validate XOR checksums, and test remote tare (`python3 tools/test_client.py -t`).
