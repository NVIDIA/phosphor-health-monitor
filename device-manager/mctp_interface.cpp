#include "mctp_interface.hpp"

#include "device_error_logger.hpp"

#include <phosphor-logging/lg2.hpp>

#include <filesystem>

namespace phosphor::device::manager
{

MCTPInterface::MCTPInterface(sdbusplus::bus_t& bus) : bus(bus)
{
    lg2::info("Initializing MCTP interface monitoring");

    try
    {
        // Monitor MCTP InterfacesAdded signals for endpoint discovery
        interfacesAddedMatch = std::make_unique<sdbusplus::bus::match_t>(
            bus,
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::sender(MCTP_SERVICE),
            [this](sdbusplus::message_t& msg) { handleInterfacesAdded(msg); });

        lg2::info("MCTP interface monitoring initialized successfully");
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to initialize MCTP interface monitoring: {ERROR}",
                   "ERROR", e.what());
        throw;
    }
}

void MCTPInterface::handleInterfacesAdded(sdbusplus::message_t& msg)
{
    try
    {
        lg2::info("Received MCTP InterfacesAdded signal");

        // Check if message is valid before reading
        if (msg.is_method_error())
        {
            lg2::warning("MCTP InterfacesAdded signal contains method error");
            return;
        }

        sdbusplus::message::object_path objectPath;
        using PropertyValue =
            std::variant<bool, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                         int64_t, uint64_t, double, std::string,
                         std::vector<uint8_t>, std::vector<std::string>>;
        using PropertyMap = std::map<std::string, PropertyValue>;
        using InterfaceMap = std::map<std::string, PropertyMap>;

        InterfaceMap interfaces;

        // Read the D-Bus message
        msg.read(objectPath, interfaces);

        lg2::info(
            "Successfully parsed MCTP signal for path: {PATH} with {COUNT} interfaces",
            "PATH", objectPath.str, "COUNT", interfaces.size());

        // Check if this is an MCTP Endpoint interface
        if (interfaces.find(MCTP_ENDPOINT_INTERFACE) != interfaces.end())
        {
            uint8_t eid = extractEIDFromPath(objectPath.str);
            if (eid != 0)
            {
                lg2::info("MCTP endpoint added: {OBJECT_PATH}, EID: {EID}",
                          "OBJECT_PATH", objectPath.str, "EID", eid);

                // Clear errors for this device when MCTP endpoint comes online
                DeviceErrorLogger::clearDeviceErrors(bus, eid);
            }
            else
            {
                lg2::warning(
                    "Could not extract valid EID from MCTP path: {PATH}",
                    "PATH", objectPath.str);
            }
        }
        else
        {
            lg2::debug("InterfacesAdded signal not for MCTP endpoint: {PATH}",
                       "PATH", objectPath.str);
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("D-Bus error handling MCTP InterfacesAdded signal: {ERROR}",
                   "ERROR", e.what());
    }
    catch (const std::exception& e)
    {
        lg2::error("Error handling MCTP InterfacesAdded signal: {ERROR}",
                   "ERROR", e.what());
    }
}

uint8_t MCTPInterface::extractEIDFromPath(const std::string& objectPath)
{
    try
    {
        // MCTP endpoint paths look like:
        // /au/com/codeconstruct/mctp1/networks/1/endpoints/10 where the last
        // number (10) is the EID
        std::filesystem::path path(objectPath);
        std::string filename = path.filename().string();

        int eid = std::stoi(filename);
        if (eid >= 0 && eid <= 255)
        {
            return static_cast<uint8_t>(eid);
        }

        lg2::info("Failed to extract valid EID from MCTP object path: {PATH}",
                  "PATH", objectPath);
        return 0;
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception extracting EID from path {PATH}: {ERROR}", "PATH",
                   objectPath, "ERROR", e.what());
        return 0;
    }
}

} // namespace phosphor::device::manager
