#pragma once
#include <limits>
#include <string>
namespace phosphor
{
namespace ipc
{
/** @struct ParamConfig
 *  @brief Structure to hold the sensor parameters
 */
struct ParamConfig
{
    std::string key;                              // service property name
    std::string valueType;                        // value type
    std::string operatorType;                     // operator type
    double criticalHigh =
        std::numeric_limits<double>::quiet_NaN(); // critical value
    double warningHigh =
        std::numeric_limits<double>::quiet_NaN(); // warning value
    std::string criticalTgt;                      // critical target
    std::string warningTgt;                       // warning target
};
} // namespace ipc
} // namespace phosphor
