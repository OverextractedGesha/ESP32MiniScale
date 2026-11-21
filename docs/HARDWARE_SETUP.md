# Hardware Setup Guide: ESP32-C3 Headless Smart Coffee Scale

This document provides the hardware architecture, wiring specifications, schematics, and mechanical assembly guidelines for building a high-precision, headless smart coffee scale powered by an **ESP32-C3 SuperMini**, an **HX711** 24-bit ADC load cell amplifier, and a **USB-C powerbank**.

---

## 1. System Bill of Materials (BOM)

| Component | Specification / Model | Qty | Notes |
| :--- | :--- | :--- | :--- |
| **MCU Board** | ESP32-C3 SuperMini | 1 | RISC-V single-core, native USB-C (`USB-CDC`), BLE 5.0, onboard 3.3V LDO |
| **ADC / Amplifier** | HX711 24-bit ADC Breakout Board | 1 | Standard dual-channel breakout (Channel A used) |
| **Load Cell** | 3kg Micro Aluminum Bar Load Cell | 1 | 4-wire Wheatstone bridge (rated output: 1.0±0.15 mV/V) |
| **Power Source** | USB-C Powerbank (5V output) + USB-C Cable | 1 | Powers the ESP32-C3 SuperMini directly via its USB-C port |
| **Capacitor (Recommended)** | 100 nF (0.1 µF) Ceramic | 1 | Decoupling across HX711 `VCC` and `GND` pins |
| **Keep-Alive Load Resistor** *(Optional)* | 68 Ω, 1 W Metal Film / Power Resistor | 1 | Prevents auto-shutoff on powerbanks with low-current cutoff |
| **Keep-Alive Transistor** *(Optional)* | `2N2222` or `S8050` NPN Transistor | 1 | Low-side switch controlled by `GPIO 10` |
| **Keep-Alive Base Resistor** *(Optional)* | 1 kΩ, 1/4 W Resistor | 1 | Limits `GPIO 10` base drive current (~2.6 mA) |
| **Mechanical Plates** | Acrylic / Aluminum 3mm plates | 2 | Top weighing platform & base plate |
| **Mounting Hardware** | M3 / M4 Allen bolts, spacers, rubber feet | 1 set | Strain isolation mounting |

---

## 2. Complete Pin Connection Table

### 2.1 Load Cell to HX711 Breakout

The 3kg miniature load cell forms a four-arm Wheatstone strain-gauge bridge. Connect the 4-wire color-coded pigtail to the input terminal of the HX711 breakout:

| Load Cell Wire Color | Function | HX711 Pin | Description |
| :--- | :--- | :--- | :--- |
| **Red** | Excitation Positive ($E+$) | **E+** / **AVDD** | Bridge excitation supply (+3.3V regulated) |
| **Black** | Excitation Negative ($E-$) | **E-** / **AGND** | Bridge excitation return (Analog Ground) |
| **Green** | Signal Positive ($A+$) | **A+** | Channel A non-inverting analog differential input |
| **White** | Signal Negative ($A-$) | **A-** | Channel A inverting analog differential input |
| **Bare Wire / Yellow** (if present) | Cable Shield | **GND** / Chassis | Ground shield drain (suppresses RFI/EMI) |

> [!NOTE]
> Load cell wiring colors can occasionally vary between manufacturers. If weight readings decrease when weight is placed on the platform, either reverse the **A+ (Green)** and **A- (White)** connections or invert the sign of the software calibration factor.

---

### 2.2 HX711 Breakout to ESP32-C3 SuperMini

| HX711 Pin | ESP32-C3 SuperMini Pin | ESP32-C3 GPIO | Description / Notes |
| :--- | :--- | :--- | :--- |
| **VCC / VDD** | **3V3** | 3.3V Rail | Regulated 3.3V from ESP32-C3 onboard LDO (DO NOT connect to 5V) |
| **GND** | **GND** | Ground | Common system ground |
| **DT / DOUT** | **6** | **GPIO 6** | Serial Data Output (safe non-strapping GPIO) |
| **SCK / CLK** | **7** | **GPIO 7** | Serial Clock Input (safe non-strapping GPIO) |

> [!IMPORTANT]
> Power the HX711 from the **3V3** pin (onboard LDO output), **not** the **5V** pin! Supplying 5V to the HX711 causes its `DOUT` line to output 5V logic levels, which can permanently damage the ESP32-C3's 3.3V-tolerant GPIO pins.

---

### 2.3 ESP32-C3 SuperMini Pinout & Strapping Pin Warnings

```text
                +-------[ USB-C ]-------+
        GPIO 5 -| 1   (ADC2)       5V  |- USB 5V (VBUS)
 [DOUT] GPIO 6 -| 2               GND  |- Ground
 [SCK]  GPIO 7 -| 3               3V3  |- Regulated 3.3V Out
        GPIO 8 -| 4 (STRAP/LED)  GPIO 4 |-
        GPIO 9 -| 5 (STRAP/BOOT) GPIO 3 |-
[PULSE] GPIO 10-| 6              GPIO 2 |- (STRAP)
        GPIO 20-| 7 (RX)         GPIO 1 |-
        GPIO 21-| 8 (TX)         GPIO 0 |-
                +-----------------------+
```

> [!WARNING]
> **Avoid Strapping & Conflicting Pins on ESP32-C3 SuperMini:**
> * **GPIO 2, GPIO 8, and GPIO 9** are **boot strapping pins** (GPIO 9 also doubles as the `BOOT` button; GPIO 8 drives the onboard LED on most SuperMini boards). External pull-ups/pull-downs or active loads on these pins can prevent normal flash booting or force USB download mode.
> * **GPIO 5** is mapped to **ADC2** (`ADC2_CH0`), which has hardware restrictions/arbitration conflicts alongside the active BLE/Wi-Fi radio.
> * **GPIO 6, GPIO 7, and GPIO 10** are clean, general-purpose digital GPIOs with no boot-strapping side effects.

---

## 3. Power Architecture & Optional Pulsed Powerbank Keep-Alive

### 3.1 USB-C Powerbank & Bare Testing First

Plug the USB-C powerbank directly into the ESP32-C3 SuperMini USB-C port:
* **USB 5V (`5V` / VBUS):** Feeds the ESP32-C3 SuperMini onboard 3.3V LDO regulator (and powers the optional keep-alive load resistor directly from `5V`, bypassing the 3.3V LDO).
* **Regulated `3V3` Output:** Powers the ESP32-C3 silicon and the HX711 `VCC` rail. Place a **100 nF ceramic capacitor** directly across HX711 `VCC` and `GND` to suppress RF switching noise during BLE transmissions.

> [!TIP]
> **Test your powerbank bare first!**
> Many modern USB-C powerbanks stay awake with the ESP32-C3 + BLE radio (~40–80 mA average draw) or offer a low-current mode. Before soldering the keep-alive circuit, run the scale powered solely by your powerbank for 5–10 minutes. Only build the keep-alive circuit below if your powerbank shuts itself off automatically.

---

### 3.2 Optional Pulsed Powerbank Keep-Alive Circuit (Safest Design)

If your powerbank has an auto-shutoff threshold (typically 50–100 mA minimum sustained or periodic draw), enable the firmware's pulsed load circuit on **GPIO 10** (`KEEPALIVE_ENABLED 1` in `config.h`). Every **5 seconds** (`KEEPALIVE_INTERVAL_MS = 5000`), firmware pulses `GPIO 10` `HIGH` for **150 ms** (`KEEPALIVE_PULSE_MS = 150`).

```text
  ESP32-C3 5V Pin (USB VBUS)
           |
          [ ] R_load = 68 Ohm, 1 W
           |
           +--- Collector (C)
                        \  NPN Transistor
  GPIO 10 ---[ 1 kOhm ]--+ Base (B)   (2N2222 or S8050)
                        /
           +--- Emitter (E)
           |
          GND (Common Ground)
```

### Electrical & Fail-Safe Analysis

* **Pulse Current:** With `5.0 V` VBUS and $V_{\text{CE(sat)}} \approx 0.2\text{ V}$, the pulse current through the `68 Ω` resistor is:
  $$I_{\text{pulse}} \approx \frac{5.0\text{ V} - 0.2\text{ V}}{68\text{ }\Omega} \approx 70.6\text{ mA}$$
* **Base Drive Current:** Driven from `GPIO 10` at `3.3 V` through a `1 kΩ` resistor ($V_{\text{BE}} \approx 0.7\text{ V}$):
  $$I_{\text{B}} = \frac{3.3\text{ V} - 0.7\text{ V}}{1000\text{ }\Omega} = 2.6\text{ mA}$$
  This safely saturates a `2N2222` or `S8050` NPN transistor without overloading the ESP32-C3 GPIO pin.
* **Fail-Safe Maximum Dissipation (0.37 W):** Even under a worst-case firmware lockup where `GPIO 10` remains stuck `HIGH` continuously (or the transistor shorts collector-to-emitter), the maximum power dissipated in the `68 Ω` resistor is:
  $$P_{\max} = \frac{V_{\text{USB}}^2}{R_{\text{load}}} = \frac{(5.0\text{ V})^2}{68\text{ }\Omega} \approx 0.368\text{ W} \text{ (0.37 W)}$$
  Using a **1 W rated `68 Ω` resistor** provides nearly a **3× thermal safety margin**, ensuring it stays safe and cool even in a continuous-ON fault state.
* **Average Power Overhead:** At a 150 ms / 5000 ms (3%) duty cycle, average power loss is only **~11 mW** (~2.2 mA average draw), adding negligible drain to the powerbank.

---

## 4. Power & Signal Wiring Overview

```text
 +------------------------+
 |   USB-C Powerbank      |
 |      (5V Output)       |
 +-----------+------------+
             | USB-C Cable
             v
 +-----------+------------------------------------+
 |          ESP32-C3 SuperMini                    |
 |                                                |
 |  [5V] -------------------+                     |
 |  [3V3] --------+         |                     |
 |  [GPIO 6] <----+----+    |                     |
 |  [GPIO 7] -----+----+----+----+                |
 |  [GPIO 10] ----+----+----+----+----+           |
 |  [GND] --------+----+----+----+----+----+      |
 +----------------+----+----+----+----+----+------+
                  |    |    |    |    |    |
                  v    |    v    |    |    |
         +--------+----+----+----+    |    |
         |  HX711 Breakout  |         |    |
         | VCC  DOUT  SCK   |         |    |
         |  |               |         |    |
         | === 100nF        |         |    |
         |  |               |         v    |
         | GND -------------+      [1kOhm] |
         +---+--------------+         |    |
             |                        v    |
       [Load Cell]          [68 Ohm 1W] -> NPN (2N2222/S8050) -> GND
```

---

## 5. Mechanical Load Cell Assembly & Strain Isolation

To achieve 0.1g coffee brewing resolution without drift:

```text
    [Weighing Platform / Drip Tray Surface]
         ||                      |
         || (M3 Bolt into Top)   | (Air Gap: ~1.5 - 2mm clearance)
      +--++----------------------v---+
      |  (o)                       | |  <-- Arrow pointing DOWN
      |      3kg Load Cell Beam    | |
      |                        (o) | |
      +-------------------------++---+
                                || (M3 Bolt into Base)
     ===========================++=========================
     [Rigid Base Plate with Vibration-Dampening Rubber Feet]
```

1. **Direction of Force:** Note the directional arrow sticker on the side of the 3kg load cell. Ensure the arrow points downwards towards the base plate.
2. **Cantilever Clearance:** Mount the load cell with precision metal or nylon spacers (1.5mm to 2mm thick) at both ends:
   - One end bolted rigidly to the bottom base plate.
   - The opposite end bolted to the top weighing platform.
   - The center portion of the aluminum bar must float freely with zero contact against either plate.
3. **Cable Routing:** Fasten the 4-wire cable to the base plate using a strain relief clip or zip tie before routing to the HX711. Mechanical tugging or movement of the cable while pouring will produce false weight transients.
4. **Thermal Drift Shielding:** If espresso cups are placed directly on the platform, use a silicone rubber heat insulation pad on top of the plate to shield the aluminum load cell from direct heat conduction.
