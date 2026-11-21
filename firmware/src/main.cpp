#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <HX711.h>
#include "config.h"
#include "calibration.h"

// =============================================================================
// Global State & Hardware Instances
// =============================================================================
static HX711 scale;

// Calibration parameters (persisted in NVS)
static float g_calFactor = DEFAULT_CALIBRATION_FACTOR;
static long  g_zeroOffset = DEFAULT_ZERO_OFFSET;

// Tare offset (dynamic zeroing during operation)
static long  g_tareOffset = 0L;
static volatile bool g_tareRequested = false;

// Filtered weight tracking
static float g_currentRawWeight = 0.0f;
static float g_smoothedWeight   = 0.0f;
static bool  g_isStable         = true;

// BLE Server & Characteristics
static BLEServer*          g_pServer      = nullptr;
static BLECharacteristic*  g_pNotifyChar  = nullptr;
static BLECharacteristic*  g_pWriteChar   = nullptr;
static volatile bool       g_deviceConnected = false;
static bool                g_oldDeviceConnected = false;
static bool                g_bleRestartPending = false;
static uint32_t            g_bleDisconnectTimeMs = 0;

// Timing tickers
static uint32_t g_lastNotifyTimeMs      = 0;
static uint32_t g_lastStatusTimeMs      = 0;
static bool     g_keepaliveActive       = false;
static uint32_t g_keepaliveLastCycleMs  = 0;
static uint32_t g_keepalivePulseStartMs = 0;

// =============================================================================
// BLE Server Callbacks
// =============================================================================
class ScaleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        g_deviceConnected = true;
        Serial.println(F("[BLE] Central client connected (Beanconqueror / Test Client)"));
    }

    void onDisconnect(BLEServer* pServer) override {
        g_deviceConnected = false;
        Serial.println(F("[BLE] Central client disconnected"));
    }
};

// =============================================================================
// BLE Write Callback (Handles Tare & Remote Commands from Beanconqueror)
// =============================================================================
class ScaleWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        uint8_t* pData = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();

        if (len == 0 || pData == nullptr) return;

        // Print raw hex command received
        Serial.print(F("[BLE WRITE RX] Len: "));
        Serial.print(len);
        Serial.print(F(" | Data: "));
        for (size_t i = 0; i < len; i++) {
            if (pData[i] < 0x10) Serial.print('0');
            Serial.print(pData[i], HEX);
            Serial.print(' ');
        }
        Serial.println();

        // Validate Decent protocol header and command
        // Decent Tare command: Byte 0 = 0x03, Byte 1 = 0x0F
        if (len >= 2 && pData[0] == DECENT_HEADER_BYTE && pData[1] == DECENT_CMD_TARE) {
            Serial.println(F("[BLE COMMAND] Tare request received from Beanconqueror."));
            g_tareRequested = true;
        }
    }
};

// =============================================================================
// USB Powerbank Keep-Alive (Non-Blocking Pulse Generator)
// =============================================================================
static void updatePowerbankKeepalive(uint32_t now) {
#if KEEPALIVE_ENABLED
    if (g_keepaliveActive) {
        if (now - g_keepalivePulseStartMs >= KEEPALIVE_PULSE_MS) {
            digitalWrite(KEEPALIVE_PIN, LOW);
            g_keepaliveActive = false;
        }
    } else if (now - g_keepaliveLastCycleMs >= KEEPALIVE_INTERVAL_MS) {
        digitalWrite(KEEPALIVE_PIN, HIGH);
        g_keepaliveActive = true;
        g_keepalivePulseStartMs = now;
        g_keepaliveLastCycleMs = now;
    }
#else
    (void)now;
#endif
}

// =============================================================================
// Weight Filtering & Signal Conditioning
// =============================================================================
static void updateWeightFilter(long rawAdc) {
    // Convert ADC ticks to net grams relative to dynamic tare offset
    long netAdc = rawAdc - g_tareOffset;
    g_currentRawWeight = (float)netAdc / g_calFactor;

    // Adaptive Exponential Moving Average (EMA):
    // Use fast tracking alpha for large transient shifts (e.g. coffee stream hitting cup),
    // and standard smoothing alpha for steady-state flow and stability.
    float delta = fabsf(g_currentRawWeight - g_smoothedWeight);
    float alpha = (delta > DYNAMIC_ALPHA_THRESHOLD) ? FILTER_FAST_ALPHA : FILTER_EMA_ALPHA;

    g_smoothedWeight = (alpha * g_currentRawWeight) + ((1.0f - alpha) * g_smoothedWeight);

    // Resting noise window deadband around 0.0g
    if (fabsf(g_smoothedWeight) < NOISE_THRESHOLD_WINDOW) {
        g_smoothedWeight = 0.0f;
    }

    // Determine stability flag (0xCE = stable, 0xCA = moving)
    g_isStable = (delta < 0.08f);
}

// =============================================================================
// Decent Scale 7-Byte Notification Packet Transmitter
// =============================================================================
static void sendDecentNotification(float weightGrams) {
    if (!g_deviceConnected || g_pNotifyChar == nullptr) return;

    // Weight is expressed in signed tenths of a gram (grams * 10)
    int16_t weightDecigrams = (int16_t)roundf(weightGrams * 10.0f);

    uint8_t packet[DECENT_PACKET_LEN];
    packet[0] = DECENT_HEADER_BYTE;                                // 0x03 (Model ID)
    packet[1] = g_isStable ? DECENT_TYPE_WEIGHT_STABLE : DECENT_TYPE_WEIGHT_MOVING; // 0xCE or 0xCA
    packet[2] = (uint8_t)((weightDecigrams >> 8) & 0xFF);          // Weight MSB
    packet[3] = (uint8_t)(weightDecigrams & 0xFF);                 // Weight LSB
    packet[4] = 0x00;                                              // Reserved
    packet[5] = 0x00;                                              // Reserved

    // XOR Checksum over bytes 0 through 5
    packet[6] = packet[0] ^ packet[1] ^ packet[2] ^ packet[3] ^ packet[4] ^ packet[5];

    // Transmit packet via BLE notification
    g_pNotifyChar->setValue(packet, DECENT_PACKET_LEN);
    g_pNotifyChar->notify();
}

// =============================================================================
// Tare Routine
// =============================================================================
static void executeTare() {
    if (!scale.is_ready()) {
        return; // Retry on next loop iteration when HX711 sample is ready
    }

    Serial.println(F("[SCALE] Executing Tare operation..."));
    g_tareOffset = scale.read();
    g_smoothedWeight   = 0.0f;
    g_currentRawWeight = 0.0f;
    g_tareRequested    = false;

    Serial.printf("[SCALE] Tare complete. New offset: %ld\n", g_tareOffset);
}

// =============================================================================
// Setup Initializer
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000); // Allow ESP32-C3 USB-CDC to enumerate before boot logs

    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("   ESP32 Headless Smart Coffee Scale Initiating   "));
    Serial.println(F("    Decent Scale & Beanconqueror BLE Emulation   "));
    Serial.println(F("=================================================="));

    // 1. Configure USB Powerbank Keep-Alive Pin (active-HIGH pulse)
#if KEEPALIVE_ENABLED
    pinMode(KEEPALIVE_PIN, OUTPUT);
    digitalWrite(KEEPALIVE_PIN, LOW);
    Serial.printf("[PWR] Power: USB 5V (Keep-alive pulsed on GPIO %d)\n", KEEPALIVE_PIN);
#else
    Serial.println(F("[PWR] Power: USB 5V (Keep-alive disabled)"));
#endif

    // 2. Load Persistent Calibration from NVS
    loadCalibrationFromNVS(g_calFactor, g_zeroOffset);
    g_tareOffset = g_zeroOffset;

    // 3. Initialize HX711 Load Cell Interface
    scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
    Serial.println(F("[HX711] Waiting for ADC to become ready..."));
    
    uint32_t hxTimeout = millis() + 3000;
    while (!scale.is_ready() && millis() < hxTimeout) {
        delay(50);
    }

    if (scale.is_ready()) {
        Serial.println(F("[HX711] Sensor ready. Initializing baseline zero offset."));
        scale.set_scale(g_calFactor);
        scale.set_offset(g_zeroOffset);
        g_tareOffset = scale.read();
    } else {
        Serial.println(F("[HX711 WARNING] Sensor not responding. Check DOUT/SCK wiring!"));
    }

    // 4. Initialize Bluetooth Low Energy (Decent Scale GATT Server)
    BLEDevice::init(BLE_DEVICE_NAME);
    
    // Create Server
    g_pServer = BLEDevice::createServer();
    g_pServer->setCallbacks(new ScaleServerCallbacks());

    // Create Primary Decent Service (0xFFF0)
    BLEService* pService = g_pServer->createService(BLE_SERVICE_UUID);

    // Create Weight Notification Characteristic (0xFFF4)
    g_pNotifyChar = pService->createCharacteristic(
        BLE_CHAR_NOTIFY_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    g_pNotifyChar->addDescriptor(new BLE2902());

    // Create Command Write Characteristic (0xFFF3)
    g_pWriteChar = pService->createCharacteristic(
        BLE_CHAR_WRITE_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    g_pWriteChar->setCallbacks(new ScaleWriteCallbacks());

    // Start Service
    pService->start();

    // Configure Advertising Payload
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // Functions that help with iPhone connections
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println(F("[BLE] GATT Server active. Advertising as: 'Decent Scale'"));
    Serial.println(F("[INFO] Type 'c' + ENTER in Serial Monitor to begin Calibration."));
    Serial.println(F("[INFO] Type 't' + ENTER in Serial Monitor to trigger local Tare."));

    g_keepaliveLastCycleMs = millis();
    g_lastStatusTimeMs = g_keepaliveLastCycleMs;
}

// =============================================================================
// Main Supervisory Loop
// =============================================================================
void loop() {
    uint32_t now = millis();

    // 0. Run non-blocking USB powerbank keep-alive pulse
    updatePowerbankKeepalive(now);

    // 1. Check Serial commands (CLI triggers)
    if (Serial.available() > 0) {
        char cmd = (char)Serial.read();
        if (cmd == 'c' || cmd == 'C') {
            runInteractiveCalibration(scale);
            // Refresh in-memory calibration variables
            loadCalibrationFromNVS(g_calFactor, g_zeroOffset);
            g_tareOffset = g_zeroOffset;
        } else if (cmd == 't' || cmd == 'T') {
            g_tareRequested = true;
        }
    }

    // 2. Handle Tare requests (from BLE write or Serial CLI)
    if (g_tareRequested) {
        executeTare();
    }

    // 3. Non-blocking HX711 acquisition & digital filtering
    if (scale.is_ready()) {
        long rawVal = scale.read();
        updateWeightFilter(rawVal);
    }

    // 4. Stream 7-Byte Decent Scale notifications at 10 Hz (every 100 ms)
    if (now - g_lastNotifyTimeMs >= BLE_NOTIFY_INTERVAL_MS) {
        g_lastNotifyTimeMs = now;
        if (g_deviceConnected) {
            sendDecentNotification(g_smoothedWeight);
        }
    }

    // 5. Periodic Diagnostics Status Log (every 5 seconds)
    if (now - g_lastStatusTimeMs >= STATUS_LOG_INTERVAL_MS) {
        g_lastStatusTimeMs = now;

        Serial.printf("[STATUS] W: %6.2fg (Raw: %6.2fg) | Pwr: USB 5V | BLE: %s\n",
            g_smoothedWeight,
            g_currentRawWeight,
            g_deviceConnected ? "CONNECTED" : "ADVERTISING"
        );
    }

    // 6. Handle BLE disconnection reconnect cycling (non-blocking)
    if (!g_deviceConnected && g_oldDeviceConnected) {
        g_bleDisconnectTimeMs = now;
        g_bleRestartPending = true;
        g_oldDeviceConnected = g_deviceConnected;
    }
    if (g_bleRestartPending && !g_deviceConnected && (now - g_bleDisconnectTimeMs >= 500)) {
        g_pServer->startAdvertising();
        Serial.println(F("[BLE] Advertising restarted. Waiting for reconnect..."));
        g_bleRestartPending = false;
    }
    if (g_deviceConnected && !g_oldDeviceConnected) {
        g_bleRestartPending = false;
        g_oldDeviceConnected = g_deviceConnected;
    }

    // Yield short duration to allow ESP32 FreeRTOS background tasks (BLE radio, IDLE) to run
    delay(5);
}
