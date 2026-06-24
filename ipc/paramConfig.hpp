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
    std::string criticalErrorId;    // Redfish EventId for critical crossing
    std::string warningErrorId;     // Redfish EventId for warning crossing
    std::string criticalResolution; // Resolution text for critical crossing
    std::string warningResolution;  // Resolution text for warning crossing
};
} // namespace ipc
} // namespace phosphor
