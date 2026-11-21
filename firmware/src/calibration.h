#pragma once

#include <Arduino.h>
#include <HX711.h>

/**
 * Loads the calibration slope factor and zero offset from ESP32 NVS.
 * Returns true if valid values were found; false if defaults were used.
 */
bool loadCalibrationFromNVS(float &calFactor, long &zeroOffset);

/**
 * Persists the calibration slope factor and zero offset into ESP32 NVS.
 */
bool saveCalibrationToNVS(float calFactor, long zeroOffset);

/**
 * Interactive Serial CLI calibration routine.
 * Prompts user via UART to zero the scale, place a known reference weight,
 * computes the calibration slope, and saves parameters to NVS.
 */
void runInteractiveCalibration(HX711 &scale);
