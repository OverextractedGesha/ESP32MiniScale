#include "calibration.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;

bool loadCalibrationFromNVS(float &calFactor, long &zeroOffset) {
    if (!prefs.begin(NVS_NAMESPACE, true)) { // Read-only mode
        Serial.println(F("[NVS] Failed to open preferences namespace. Using defaults."));
        calFactor = DEFAULT_CALIBRATION_FACTOR;
        zeroOffset = DEFAULT_ZERO_OFFSET;
        return false;
    }

    if (prefs.isKey(NVS_KEY_CAL_FACTOR) && prefs.isKey(NVS_KEY_ZERO_OFFSET)) {
        calFactor = prefs.getFloat(NVS_KEY_CAL_FACTOR, DEFAULT_CALIBRATION_FACTOR);
        zeroOffset = prefs.getLong(NVS_KEY_ZERO_OFFSET, DEFAULT_ZERO_OFFSET);
        prefs.end();
        Serial.printf("[NVS] Loaded Calibration: Factor = %.4f, Zero Offset = %ld\n", calFactor, zeroOffset);
        return true;
    } else {
        Serial.println(F("[NVS] No saved calibration found in NVS. Using factory defaults."));
        calFactor = DEFAULT_CALIBRATION_FACTOR;
        zeroOffset = DEFAULT_ZERO_OFFSET;
        prefs.end();
        return false;
    }
}

bool saveCalibrationToNVS(float calFactor, long zeroOffset) {
    if (!prefs.begin(NVS_NAMESPACE, false)) { // Read/Write mode
        Serial.println(F("[NVS] Failed to open preferences namespace for writing."));
        return false;
    }

    size_t writtenFactor = prefs.putFloat(NVS_KEY_CAL_FACTOR, calFactor);
    size_t writtenOffset = prefs.putLong(NVS_KEY_ZERO_OFFSET, zeroOffset);
    prefs.end();

    if (writtenFactor > 0 && writtenOffset > 0) {
        Serial.printf("[NVS] Successfully stored Calibration: Factor = %.4f, Offset = %ld\n", calFactor, zeroOffset);
        return true;
    } else {
        Serial.println(F("[NVS] Error writing calibration data to flash memory."));
        return false;
    }
}

static void flushSerialInput() {
    while (Serial.available() > 0) {
        Serial.read();
        delay(5);
    }
}

static void waitForEnter() {
    flushSerialInput();
    while (true) {
        if (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '\r' || c == '\n') {
                break;
            }
        }
        delay(10);
    }
    flushSerialInput();
}

static float readFloatFromSerial() {
    flushSerialInput();
    String input = "";
    while (true) {
        if (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '\r' || c == '\n') {
                if (input.length() > 0) {
                    break;
                }
            } else if ((c >= '0' && c <= '9') || c == '.') {
                input += c;
                Serial.print(c); // Echo character
            } else if (c == 8 || c == 127) { // Backspace
                if (input.length() > 0) {
                    input.remove(input.length() - 1);
                    Serial.print("\b \b");
                }
            }
        }
        delay(10);
    }
    Serial.println();
    return input.toFloat();
}

static long readAverageRaw(HX711 &scale, int sampleCount) {
    long sum = 0;
    int validSamples = 0;
    int attempts = 0;

    while (validSamples < sampleCount && attempts < sampleCount * 4) {
        attempts++;
        if (scale.is_ready()) {
            sum += scale.read();
            validSamples++;
            delay(20);
        } else {
            delay(10);
        }
    }

    if (validSamples == 0) return 0;
    return sum / validSamples;
}

void runInteractiveCalibration(HX711 &scale) {
    Serial.println();
    Serial.println(F("======================================================"));
    Serial.println(F("     ESP32 COFFEE SCALE - INTERACTIVE CALIBRATION     "));
    Serial.println(F("======================================================"));
    Serial.println(F("Ensure the scale is on a flat, vibration-free surface."));
    Serial.println();

    // -----------------------------------------------------------------
    // Step 1: Zero / Tare with empty platform
    // -----------------------------------------------------------------
    Serial.println(F("--- STEP 1: ZERO OFFSET (TARE) ---"));
    Serial.println(F("Remove ALL objects from the scale platform."));
    Serial.println(F("Press [ENTER] when the scale is completely empty..."));
    waitForEnter();

    Serial.print(F("Sampling zero offset (30 samples)... "));
    long zeroOffset = readAverageRaw(scale, 30);
    scale.set_offset(zeroOffset);
    Serial.printf("Done! Zero offset raw: %ld\n\n", zeroOffset);

    // -----------------------------------------------------------------
    // Step 2: Reference Weight Input
    // -----------------------------------------------------------------
    Serial.println(F("--- STEP 2: KNOWN REFERENCE WEIGHT ---"));
    Serial.println(F("Enter the reference weight in grams (e.g., 100, 200, 500):"));
    Serial.print(F("> "));
    float refWeight = readFloatFromSerial();

    while (refWeight <= 0.0f || refWeight > 3000.0f) {
        Serial.println(F("Invalid weight! Please enter a value between 1.0g and 3000.0g:"));
        Serial.print(F("> "));
        refWeight = readFloatFromSerial();
    }
    Serial.printf("Target reference weight set to: %.2f grams\n\n", refWeight);

    // -----------------------------------------------------------------
    // Step 3: Place Reference Weight
    // -----------------------------------------------------------------
    Serial.println(F("--- STEP 3: MEASURE REFERENCE WEIGHT ---"));
    Serial.printf("Place the %.2fg weight onto the center of the platform.\n", refWeight);
    Serial.println(F("Press [ENTER] once placed and stabilized..."));
    waitForEnter();

    Serial.print(F("Stabilizing and acquiring 30 samples... "));
    delay(1000); // Allow mechanical oscillation to dampen
    long rawWithWeight = readAverageRaw(scale, 30);
    Serial.printf("Done! Loaded raw: %ld\n\n", rawWithWeight);

    // -----------------------------------------------------------------
    // Step 4: Calculate Slope Factor
    // -----------------------------------------------------------------
    long rawDelta = rawWithWeight - zeroOffset;
    if (abs(rawDelta) < 100) {
        Serial.println(F("[ERROR] Raw reading delta is too low! Check load cell wiring or calibration weight."));
        Serial.println(F("Calibration aborted. Previous settings preserved."));
        return;
    }

    float calFactor = (float)rawDelta / refWeight;
    Serial.printf("Calculated Slope Factor: %.4f (Raw Delta: %ld for %.2fg)\n", calFactor, rawDelta, refWeight);

    // -----------------------------------------------------------------
    // Step 5: Save to NVS
    // -----------------------------------------------------------------
    Serial.println(F("Saving parameters to persistent flash memory (NVS)..."));
    if (saveCalibrationToNVS(calFactor, zeroOffset)) {
        scale.set_scale(calFactor);
        scale.set_offset(zeroOffset);
        Serial.println(F("SUCCESS: Calibration saved and active!"));
    } else {
        Serial.println(F("FAILED: Could not write calibration to NVS!"));
    }

    // -----------------------------------------------------------------
    // Step 6: Live Verification Test
    // -----------------------------------------------------------------
    Serial.println();
    Serial.println(F("--- VERIFICATION READINGS (5 samples) ---"));
    for (int i = 0; i < 5; i++) {
        delay(200);
        if (scale.is_ready()) {
            float testWeight = (scale.read() - zeroOffset) / calFactor;
            Serial.printf("Sample %d: %6.2f g\n", i + 1, testWeight);
        }
    }

    Serial.println(F("======================================================"));
    Serial.println(F("Calibration Complete. Returning to normal scale mode. "));
    Serial.println(F("======================================================"));
    Serial.println();
}
