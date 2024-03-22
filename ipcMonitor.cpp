#include "config.h"

#include "ipcMonitor.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
namespace phosphor
{
namespace ipc
{
PHOSPHOR_LOG2_USING;

/* Create dbus utilization sensor object */
void IPCMonitor::createSensors()
{
    for (auto& cfg : configs)
    {
        const std::shared_ptr<IPCHealthSensor>& ipcSensor =
            IPCHealthSensor::getIPCHealthSensor(bus, cfg, ioc);
        if (ipcSensor != nullptr)
        {
            ipcSensors.emplace(cfg.name, ipcSensor);
            ipcSensor->initSensor();
            lg2::info("{IPC} Ipc Sensor created", "IPC", cfg.name);
        }
        else
        {
            lg2::error("{IPC} not supported", "IPC", cfg.name);
        }
    }
}

const std::vector<IPCConfig> IPCMonitor::getIPCConfig()
{
    lg2::info("Reading IPC config file");
    std::vector<IPCConfig> ipcConfigs;
    try
    {
        // Open the ipc health config file
        std::ifstream file(IPC_CONFIG_FILE);
        if (!file.is_open())
        {
            std::string configFile = IPC_CONFIG_FILE;
            lg2::error("config readings JSON parser failure: {PATH}", "PATH",
                       configFile);
            return std::vector<IPCConfig>();
        }

        // Parse the JSON content
        Json j;
        file >> j;
        lg2::info("copying file contents to JSON object");

        // Close the file
        file.close();
        // Iterate through the IpcConfig entries
        for (const auto& ipcConfigEntry : j)
        {
            if (ipcConfigEntry.contains("bootDelay") &&
                ipcConfigEntry["bootDelay"].contains("Duration"))
            {
                bootDelay = ipcConfigEntry["bootDelay"]["Duration"].get<int>();
                lg2::info("bootDelay Value: {IPC}", "IPC", bootDelay);
            }
            if (ipcConfigEntry.contains("IpcConfig"))
            {
                auto ipcConfigJson = ipcConfigEntry["IpcConfig"];
                IPCConfig ipcConfig;
                ipcConfig.name = ipcConfigJson.value(
                    "Ipc_name", ipcConfigJson.value("Ipc_type", "N/A"));
                ipcConfig.freq = ipcConfigJson["Frequency"];
                ipcConfig.windowSize = ipcConfigJson["Window_size"];
                ipcConfig.logRateLimit = ipcConfigJson["Log_rate_limit"];

                // Iterate through the Parameters array
                for (const auto& paramJson : ipcConfigJson["Parameters"])
                {
                    ParamConfig paramConfig;
                    paramConfig.key = paramJson["Key"];
                    paramConfig.valueType = paramJson["Value_type"];
                    paramConfig.operatorType = paramJson["Operator"];
                    paramConfig.criticalHigh =
                        paramJson["Threshold"]["Critical"]["Value"];
                    paramConfig.criticalTgt =
                        paramJson["Threshold"]["Critical"]["Target"];
                    paramConfig.warningHigh =
                        paramJson["Threshold"]["Warning"]["Value"];
                    paramConfig.warningTgt =
                        paramJson["Threshold"]["Warning"]["Target"];

                    ipcConfig.paramConfig.push_back(paramConfig);
                }
                ipcConfigs.push_back(ipcConfig);
            }
        }
#ifdef PRINT_HELTH_CONFIG_ENABLED
        // Print the parsed data
        for (const auto& config : ipcConfigs)
        {
            lg2::info("Ipc name : {IPC}", "IPC", config.name);
            lg2::info("Frequency: {IPC}", "IPC", config.freq);
            lg2::info("Window Size: {IPC}", "IPC", config.windowSize);
            lg2::info("Log Rate Limit: {IPC}", "IPC", config.logRateLimit);
            for (const auto& param : config.paramConfig)
            {
                lg2::info("Key: {IPC} ", "IPC", param.key);
                lg2::info("Value Type: {IPC} ", "IPC", param.valueType);
                lg2::info("Operator: {IPC}", "IPC", param.operatorType);
                lg2::info("Critical Value:{IPC}", "IPC", param.criticalHigh);
                lg2::info("Critical Target: {IPC}", "IPC", param.criticalTgt);
                lg2::info("Warning Value: {IPC}", "IPC", param.warningHigh);
                lg2::info("Warning Target: {IPC}", "IPC", param.warningTgt);
            }
        }
#endif
    }
    catch (const Json::exception& e)
    {
        std::string configFile = IPC_CONFIG_FILE;
        lg2::error("Exception occurred while reading file {FILE}: {ERROR}",
                   "FILE", configFile, "ERROR", e.what());
        return std::vector<IPCConfig>();
    }

    return ipcConfigs;
}

void IPCMonitor::sleepuntilSystemBoot()
{
    lg2::info("ipc monitor is waiting for system boot");
    // wait for the system to boot up
    sleep(bootDelay);
}

} // namespace ipc
} // namespace phosphor
