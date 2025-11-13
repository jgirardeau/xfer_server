#include "SensorConversions.h"
#include "ConfigManager.h"

namespace SensorConversions {

double temperature_to_fahrenheit(uint16_t raw_temp) {
    double temp_raw = static_cast<double>(raw_temp);
    double retval = (temp_raw * TEMP_SCALE * 9.0 / 5.0) + TEMP_OFFSET_F;
    
    // Check config to see if we should clip negative temperatures
    if (ConfigManager::instance().get_clip_negative_temperatures()) {
        if (retval < 0.0) {
            retval = 0.0;
        }
    }
    
    return retval;
}

} // namespace SensorConversions
