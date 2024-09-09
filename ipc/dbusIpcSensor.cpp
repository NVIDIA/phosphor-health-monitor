#include "dbusIpcSensor.hpp"

#include "asio_connection.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>

#include <filesystem>
#include <memory>
#include <regex>
#include <tuple>
// Implement the DBusIpcSensor class
namespace phosphor
{
namespace ipc
{

PHOSPHOR_LOG2_USING;

// Get statistics implementation
// Define the variant type that can hold any type. In practice, you would extend
// this to include all the types you expect to handle.
using VariantType =
    std::variant<unsigned int, double, std::string>; // Example variant types

// Define the type for 'a{sv}', an array of dictionaries with string keys and
// variant values.
using StringAndVariantType = std::map<std::string, VariantType>;

// Define the type for 'a{su}', an array of dictionaries with string keys and
// unsigned integer values.
using StringAndUnsignedType = std::map<std::string, unsigned int>;

// Finally, define the type for 'a(sa{sv}a{su})', which is an array of tuples.
// Each tuple contains a string, a StringAndVariantType, and a
// StringAndUnsignedType.
using PeerAccountingType = std::vector<
    std::tuple<std::string, StringAndVariantType, StringAndUnsignedType>>;

// GetStats returns a{sv} where v variant is of type a(sa{sv}a{su})
// therefore std::map<std::string, std::variant<ComplexDBusType>>

// Read data implementation
void DBusIpcSensor::readSensor()
{
    auto& connObject = AsioConnection::getAsioConnection();
    if (connObject == nullptr)
    {
        lg2::error("Connection object is null");
        return;
    }
    // DBus implementation of Debug/Stats
    // Make call to DBus Debug Service to get stats
    connObject->async_method_call(
        [this](const boost::system::error_code ec,
               std::map<std::string, std::variant<PeerAccountingType>>& resp) {
        asyncResp.clear();
        if (ec)
        {
            // TODO: Handle for specific error code
            lg2::error("GetStats resp_handler got error");
            return;
        }
        // GetStats returns a{sv} where v variant is of type a(sa{sv}a{su})
        // Here stat is e.g. org.bus1.DBus.Debug.Stats.PeerAccounting or
        // "org.bus1.DBus.Debug.Stats.UserAccounting" and type is a tuple of
        // service name, DictionaryOfStringAndVariant and
        // DictionaryOfStringAndUnsigned
        for (auto& [stat, type] : resp)
        {
            if (stat != "org.bus1.DBus.Debug.Stats.PeerAccounting")
            {
                continue;
            }

            for (auto& tuple : std::get<PeerAccountingType>(type))
            {
                std::string connectionName = std::get<0>(tuple);
                if (std::get<2>(tuple).size() > 0)
                {
                    // reading a{su} type 'data' type is
                    // std::map<std::string, unsigned int>
                    auto& data = std::get<2>(tuple);
                    std::vector<
                        std::pair<std::string,
                                  std::variant<long int, double, std::string>>>
                        stats;
                    // statistics is configured with <"Outgoingbytes",
                    // int>  and <"Incomingbytes", int>
                    for (auto& configuredStat : statistics)
                    {
                        // Here dbus return type as 'a{su}' therefore
                        // 'data' type is std::map<std::string, unsigned
                        // int> finding the key in the map
                        auto it = data.find(configuredStat.first);
                        if (it != data.end())
                        {
                            long int value = static_cast<long int>(it->second);
                            stats.push_back(
                                std::make_pair(configuredStat.first, value));
                        }
                    }
                    // Insert the service name and stats into the map
                    asyncResp.insert(std::make_pair(connectionName, stats));
                    stats.clear();
                }
            }
            if (asyncResp.size() > 0)
            {
                // storing asyncResp as class member to avoid copy
                // elison Therefore no need to copy the asyncResp map to
                // process the data
                processdata();
            }
        }
    },
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus.Debug.Stats", "GetStats");
}

// Process statistic data implementation
void DBusIpcSensor::processdata()
{
    // std::map<std::string, std::vector<std::pair<std::string,
    // std::variant<long int,double,std::string>>>> asyncResp;
    for (auto& [connName, sensorValue] : asyncResp)
    {
        for (auto& [key, value] : sensorValue)
        {
            auto it = std::find_if(
                ipcConfig.paramConfig.begin(), ipcConfig.paramConfig.end(),
                [key](const auto& param) { return param.key == key; });
            if (it != ipcConfig.paramConfig.end())
            {
                if (it->valueType == "int")
                {
                    if (std::holds_alternative<long int>(value))
                    {
                        long int val = std::get<long int>(value);

                        using ConnKeyType = std::pair<std::string, std::string>;
                        ConnKeyType connKey = std::make_pair(connName, key);

                        if (val > it->warningHigh ||
                            valQueueMap.find(connKey) != valQueueMap.end())
                        {
                            /* find property already exist in queue */
                            if (valQueueMap.find(connKey) == valQueueMap.end())
                            {
                                valQueueMap[connKey] = std::deque<
                                    std::variant<long int, double>>();
                            }

                            auto& queue = valQueueMap[connKey];
                            if (queue.size() >= ipcConfig.windowSize)
                            {
                                queue.pop_front();
                            }
                            /* Add new item at the back */
                            queue.push_back(val);

                            /* Wait until the queue is filled with enough
                             * reference*/
                            if (queue.size() < ipcConfig.windowSize)
                            {
                                continue;
                            }

                            /* Calculate average values for the given window
                             * size */
                            double avgValue = 0;
                            // Custom accumulate operation
                            avgValue = std::accumulate(
                                queue.begin(), queue.end(), avgValue,
                                [](double acc,
                                   const std::variant<long int, double>& val)
                                    -> double {
                                // Visit the variant to handle each possible
                                // type
                                return acc + std::visit(
                                                 [](auto&& arg) -> double {
                                    using T = std::decay_t<decltype(arg)>;
                                    if constexpr (std::is_same_v<T, long int>)
                                    {
                                        // If the variant holds a numeric type,
                                        // add it to the accumulator
                                        return arg;
                                    }
                                    else
                                    {
                                        // If the variant holds a non-numeric
                                        // type, add zero
                                        return 0;
                                    }
                                },
                                                 val);
                            });
                            avgValue = avgValue / ipcConfig.windowSize;
                            if (avgValue < it->warningHigh)
                            {
                                lg2::info(
                                    "Average value for {SERVICE} is {VALUE} less than warning threshold, therefore removing it",
                                    "SERVICE", connName, "VALUE", avgValue);
                                lg2::info("key is {KEY}", "KEY", key);
                                valQueueMap.erase(connKey);
                                if (logStatusMap.find(connKey) !=
                                    logStatusMap.end())
                                {
                                    logStatusMap.erase(connKey);
                                }
                                continue;
                            }
                            /* Check the sensor threshold  and log required
                             * message
                             */
                            checkSensorThreshold(avgValue, connName, *it);
                        }
                    }
                }
            }
        }
    }
}

// Get unit name for connection name implementation
template <typename CallbackFn>
void DBusIpcSensor::convertConnectionToUnit(const std::string& connName,
                                            CallbackFn&& callback)
{
    auto& connObject = AsioConnection::getAsioConnection();
    if (connObject == nullptr)
    {
        lg2::error("Connection object is null");
        return;
    }
    // DBus implementation of Debug/Stats
    // Make call to DBus Debug Service to get stats
    connObject->async_method_call(
        [this, &connObject, callback = std::forward<CallbackFn>(callback)](
            const boost::system::error_code ec, unsigned int pid) {
        if (ec)
        {
            // TODO: Handle for specific error code
            lg2::error("GetConnectionUnixProcessID resp_handler got error");
            return;
        }
        connObject->async_method_call(
            [this, callback = std::move(callback),
             pid](const boost::system::error_code ec,
                  sdbusplus::message::object_path path) {
            if (ec)
            {
                // TODO: Handle for specific error code
                lg2::error("GetUnitByPID resp_handler got error ");
                return;
            }
            std::string unit(path);
            if (unit.empty())
            {
                lg2::error("Not able to find unit name for PID: {PID}", "PID",
                           pid);
                return;
            }
            // e.g. unit obect path looks like
            // /org/freedesktop/systemd1/unit/phosphor_2dcertificate_2dmanager_40bmcweb_2eservice
            std::filesystem::path p(unit);
            std::string unitName = p.filename();
            // Replace encoded characters in the unit name
            // e.g. phosphor_2dcertificate_2dmanager_40bmcweb_2eservice
            unitName = std::regex_replace(unitName, std::regex("_2e"), ".");
            unitName = std::regex_replace(unitName, std::regex("_2d"), "-");
            unitName = std::regex_replace(unitName, std::regex("_5f"), "_");
            unitName = std::regex_replace(unitName, std::regex("_40"), "@");
            lg2::info("Unit name: {UNIT}", "UNIT", unitName);
            callback(unitName);
        },
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "GetUnitByPID", pid);
    },
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetConnectionUnixProcessID", connName);
}

// Callback function to handle logging and start unit
void DBusIpcSensor::logAndExecuteAction(const std::string connName,
                                        const std::string thresholdType,
                                        struct ParamConfig paramConfig,
                                        const double value,
                                        const std::string unitName)
{
    using ConnKeyType = std::pair<std::string, std::string>;
    ConnKeyType connKey = std::make_pair(connName, paramConfig.key);
    if (thresholdType == "critical")
    {
        lg2::info("Creating threshold log entry for critical");
        createThresholdLogEntry("critical", unitName, paramConfig.key, value,
                                paramConfig.criticalHigh);
        startUnit(paramConfig.criticalTgt, unitName,
                  "CriticalThresholdLimitCrossed");
        if (logStatusMap.find(connKey) != logStatusMap.end())
        {
            if (!logStatusMap[connKey].second)
            {
                logStatusMap[connKey].second = true;
                lg2::info("Updating Critical log status to {STATUS}", "STATUS",
                          logStatusMap[connKey].second);
            }
        }
    }
    else if (thresholdType == "warning")
    {
        lg2::info("Creating threshold log entry for warning");
        createThresholdLogEntry("warning", unitName, paramConfig.key, value,
                                paramConfig.warningHigh);
        if (logStatusMap.find(connKey) != logStatusMap.end())
        {
            if (!logStatusMap[connKey].first)
            {
                logStatusMap[connKey].first = true;
                lg2::info("Updating Warning log status to {STATUS}", "STATUS",
                          logStatusMap[connKey].first);
            }
        }
    }
}

// Initialize IPC sensor implementation
void DBusIpcSensor::init()
{
    readSensor();
}

// Overide checkSensorThreshold implementation
void DBusIpcSensor::checkSensorThreshold(const double value,
                                         const std::string& connName,
                                         struct ParamConfig paramConfig)
{
    using ConnKeyType = std::pair<std::string, std::string>;
    ConnKeyType connKey = std::make_pair(connName, paramConfig.key);
    if (std::isfinite(paramConfig.criticalHigh) &&
        (paramConfig.operatorType == "greater_than") &&
        (value > paramConfig.criticalHigh))
    {
        lg2::error(
            "ASSERT: Dbus connection {SERVICE} is above the upper threshold critical high and value is {VALUE}",
            "SERVICE", connName, "VALUE", value);

        bool criticalLogStatus = false;
        if (logStatusMap.find(connKey) != logStatusMap.end())
        {
            criticalLogStatus = logStatusMap[connKey].second;
        }
        else
        {
            // Insert the connection key and log status for fitrst time
            logStatusMap[connKey] = std::make_pair(false, false);
        }

        if (!criticalLogStatus)
        {
            auto callback = std::bind_front(&DBusIpcSensor::logAndExecuteAction,
                                            this, connName, "critical",
                                            paramConfig, value);
            convertConnectionToUnit(connName, callback);
        }
    }
    else if (std::isfinite(paramConfig.warningHigh) &&
             (paramConfig.operatorType == "greater_than") &&
             (value > paramConfig.warningHigh))
    {
        lg2::error(
            "ASSERT: Dbus connection {SERVICE} is above the upper threshold warning high and value is {VALUE}",
            "SERVICE", connName, "VALUE", value);
        bool warningLogStatus = false;
        if (logStatusMap.find(connKey) != logStatusMap.end())
        {
            warningLogStatus = logStatusMap[connKey].first;
        }
        else
        {
            // Insert the connection key and log status for fitrst time
            logStatusMap[connKey] = std::make_pair(false, false);
        }

        if (!warningLogStatus)
        {
            auto callback = std::bind_front(&DBusIpcSensor::logAndExecuteAction,
                                            this, connName, "warning",
                                            paramConfig, value);
            convertConnectionToUnit(connName, callback);
        }
    }
    else if (std::isfinite(paramConfig.criticalHigh) &&
             (paramConfig.operatorType == "less_than") &&
             (value < paramConfig.criticalHigh))
    {
        // TODO: Add code to handle less than operator
    }
    else if (std::isfinite(paramConfig.warningHigh) &&
             (paramConfig.operatorType == "less_than") &&
             (value < paramConfig.warningHigh))
    {
        // TODO: Add code to handle less than operator
    }
    else if (paramConfig.operatorType == "equal")
    {
        // TODO: Add code to handle equal than operator
    }
}
// Constructor implementation
DBusIpcSensor::DBusIpcSensor(sdbusplus::bus::bus& bus, IPCConfig& ipcConfig,
                             boost::asio::io_context& io) :
    IPCHealthSensor(bus, ipcConfig, io)
{}
// Destructor implementation
DBusIpcSensor::~DBusIpcSensor() {}

} // namespace ipc
} // namespace phosphor
