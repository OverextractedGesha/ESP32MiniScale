# Beanconqueror Pairing & Operational Guide: ESP32 Smart Coffee Scale

This document provides step-by-step instructions for connecting, calibrating, and operating your DIY ESP32 Headless Smart Coffee Scale with the **Beanconqueror** application on iOS and Android.

---

## 1. Overview & Protocol Compatibility

[Beanconqueror](https://beanconqueror.com/) is a leading open-source brew-tracking application designed for espresso and pour-over enthusiasts. It natively supports the **Decent Scale** Bluetooth Low Energy (BLE) protocol.

Because this ESP32 firmware precisely emulates the Decent Scale GATT service (`0xFFF0`), notifications (`0xFFF4`), and write control (`0xFFF3`), Beanconqueror treats this hardware as a genuine Decent Scale without requiring any custom plugins or app modifications.

---

## 2. Prerequisites & Smartphone Permissions

Before pairing:
1. **Bluetooth Enabled:** Ensure Bluetooth is toggled **ON** on your mobile phone or tablet.
2. **Location Services (Android only):** On Android devices, the OS requires **Location Services** to be active for apps scanning for BLE peripherals.
3. **App Permissions:** Grant Beanconqueror permissions for:
   * **Nearby Devices / Bluetooth**
   * **Location** (While using the app)
4. **Single-Connection Constraint:** BLE peripherals can only maintain **one central connection** at a time. Ensure that your PC or `test_client.py` script is disconnected before attempting to pair via your phone.

---

## 3. Step-by-Step Pairing Procedure

### Step 1: Power On the ESP32-C3 Scale
Plug the **USB-C powerbank** into the **ESP32-C3 SuperMini** USB-C port (or connect the board to your computer via USB-C for debugging). Open the Serial Monitor at 115200 baud and verify the following output:
```text
[PWR] Power: USB 5V (Keep-alive pulsed on GPIO 10)
[BLE] GATT Server active. Advertising as: 'Decent Scale'
```

### Step 2: Open Beanconqueror Settings
1. Launch **Beanconqueror** on your device.
2. Tap the **Menu / Settings** icon (gear icon on the top right or navigation drawer).
3. Scroll down to the **Hardware / Integrations** section and tap **Smartscales** (or **Bluetooth Scales** depending on app version).

```text
 Settings
  ├── Profiles
  ├── Beans
  └── Hardware
        ├── Smartscales  <--- Tap here
        └── Pressure Profiler
```

### Step 3: Select Decent Scale
1. In the list of supported scales (Acaia, Felicita, Hiroia, Decent, Skale, etc.), tap on **Decent Scale**.
2. Tap **Scan** or **Search for Devices**.
3. A list of nearby devices will appear. Select **`Decent Scale`**.

```text
 +-------------------------------------+
 | Connect Smartscale                  |
 +-------------------------------------+
 | Available Devices:                  |
 |  [o] Decent Scale                   |  <--- Tap to pair
 |      Signal: -54 dBm                |
 +-------------------------------------+
```

### Step 4: Verify Connection
* Once connected, the status in Beanconqueror will turn **Green** ("Connected").
* The ESP32 Serial Monitor will log:
  ```text
  [BLE] Central client connected (Beanconqueror / Test Client)
  ```
* The live weight readout in Beanconqueror will immediately mirror the scale's current platform weight at 10 Hz.

---

## 4. Operational Workflows During Brewing

### 4.1 Remote Tare
Beanconqueror allows remote taring so you never need physical buttons:
1. Place your portafilter or cup onto the scale platform.
2. In Beanconqueror, tap the **Tare** icon.
3. The app transmits the Decent tare command packet (`0x03 0x0F 0x00 0x00 0x00 0x00 0x0C`) to Characteristic `0xFFF3`.
4. The ESP32 receives the command and zeroes out the reading within one measurement cycle:
   ```text
   [BLE WRITE RX] Len: 7 | Data: 03 0F 00 00 00 00 0C 
   [BLE COMMAND] Tare request received from Beanconqueror.
   [SCALE] Tare complete. New offset: 124580
   ```
5. The display in Beanconqueror snaps to **0.00 g**.

---

### 4.2 Real-Time Flow Rate Graphing (Espresso & Pour-Over)
During an espresso shot or pour-over:
1. Tap **Start Brew** in Beanconqueror.
2. As liquid enters the cup, the 10 Hz weight stream provides high-resolution data points.
3. Beanconqueror renders two synchronized curves in real-time:
   * **Cumulative Yield Curve (g):** Total brewed weight over time.
   * **Flow Rate Curve (g/s):** The first derivative ($\frac{dw}{dt}$) of the weight signal.
4. The adaptive Exponential Moving Average (EMA) filter on the ESP32 ensures that the flow rate curve is responsive without high-frequency digital noise spikes.

---

### 4.3 Auto-Start & Auto-Stop Timers
In Beanconqueror **Settings -> Smartscales -> Decent Scale Settings**:
* **Auto-Start Timer on Drip:** Enable this feature and set the threshold to `0.5g`. The timer will automatically begin ticking when the first drops hit the cup.
* **Auto-Stop Timer on Flow End:** Automatically halts the brew timer when flow drops below `0.1 g/s` for more than 2 seconds.

---

## 5. Troubleshooting & Diagnostics

| Symptom | Probable Cause | Corrective Action |
| :--- | :--- | :--- |
| **"Decent Scale" not found during scan** | Another device is already connected to the scale. | Disconnect any running Python test scripts (`test_client.py`) or other phones. Only one central device can connect at a time. |
| **"Decent Scale" not found on Android** | Location / Nearby permissions denied. | Go to Android Settings -> Apps -> Beanconqueror -> Permissions. Enable **Nearby Devices** and **Location**. |
| **Weight jumps or drifts erratically** | Mechanical binding or electrical noise. | 1. Ensure the load cell has 1.5mm air clearance from the plates.<br>2. Keep HX711 wires away from the ESP32-C3 antenna.<br>3. Verify that a 100nF decoupling capacitor is installed across HX711 `VCC` and `GND` and powered from the `3V3` rail. |
| **Negative weight when placing cup** | Inverted load cell bridge polarity. | Swap the **A+ (Green)** and **A- (White)** wires on the HX711, or re-run the CLI calibration routine. |
| **Weight value is inaccurate** | Uncalibrated slope factor in NVS. | Open PlatformIO Serial Monitor at 115200 baud, type `'c'`, and complete the 2-step reference calibration with a known 100g or 500g weight. |
