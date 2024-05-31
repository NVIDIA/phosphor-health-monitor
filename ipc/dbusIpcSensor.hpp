#pragma once
#include "ipcHealthSensor.hpp"

namespace phosphor
{
namespace ipc
{

/** @class DBusIpcSensor
 *  @brief DBUS sensor class to read sensor data
 */
class DBusIpcSensor : public IPCHealthSensor
{
  public:
    /* Forcing explicit construction */
    DBusIpcSensor(sdbusplus::bus_t& bus, IPCConfig& ipcConfig,
                  boost::asio::io_context& io);
    /* Destructor */
    virtual ~DBusIpcSensor();

    // Read data implementation
    virtual void readSensor() override;

    // Process statistic data implementation
    void processdata();

    // Get unit name for connection name implementation
    template <typename CallbackFn>
    void convertConnectionToUnit(const std::string& connName,
                                 CallbackFn&& callback);

    // Callback function to handle logging and start unit
    void logAndExecuteAction(const std::string connName,
                             const std::string thresholdType,
                             struct ParamConfig paramConfig, const double value,
                             const std::string unitName);

    // Initialize IPC sensor implementation
    virtual void init() override;

    // Overide checkSensorThreshold implementation
    virtual void checkSensorThreshold(const double value,
                                      const std::string& connectionName,
                                      struct ParamConfig paramConfig) override;
};
} // namespace ipc
} // namespace phosphor
