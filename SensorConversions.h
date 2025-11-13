#ifndef SENSOR_CONVERSIONS_H
#define SENSOR_CONVERSIONS_H

#include <cstdint>

/**
 * Sensor data conversion utilities
 * 
 * These functions provide consistent conversions for sensor data across the codebase.
 * Single source of truth for conversion constants.
 */

namespace SensorConversions {

// Conversion constants
constexpr double TEMP_SCALE = 0.4185;
constexpr double TEMP_OFFSET_F = 32.0;
constexpr float BATTERY_SCALE = 51.2f;

/**
 * Convert raw temperature reading to Fahrenheit
 * 
 * Optional clipping of negative values controlled by config:
 *   sensor.clip_negative_temperatures=true/false
 * 
 * @param raw_temp Raw temperature value from sensor (uint16_t)
 * @return Temperature in degrees Fahrenheit (clipped to 0°F if configured)
 */
double temperature_to_fahrenheit(uint16_t raw_temp);

/**
 * Convert raw battery reading to voltage
 * 
 * @param raw_battery Raw battery value from sensor (uint8_t)
 * @return Battery voltage in volts
 */
inline float battery_to_voltage(uint8_t raw_battery) {
    return raw_battery / BATTERY_SCALE;
}

} // namespace SensorConversions

#endif // SENSOR_CONVERSIONS_H
