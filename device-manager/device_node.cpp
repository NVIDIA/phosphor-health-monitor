#include "device_node.hpp"

#include <phosphor-logging/lg2.hpp>

namespace phosphor::device::manager
{

DeviceNode::DeviceNode(const PropertyMap& emProperties)
{
    // Extract only the fields we need
    name = getStringProperty(emProperties, "Name");
    physicalInterface = getStringProperty(emProperties, "PhysicalInterface");

    std::string deviceAddress =
        getStringProperty(emProperties, "DeviceAddress");
    eid = extractEID(deviceAddress);

    lg2::info(
        "Created DeviceNode: {NAME}, Address: {ADDR}, Interface: {IFACE}, EID: {EID}",
        "NAME", name, "ADDR", deviceAddress, "IFACE", physicalInterface, "EID",
        eid ? std::to_string(*eid) : "none");
}

std::string DeviceNode::getStringProperty(const PropertyMap& properties,
                                          const std::string& key,
                                          const std::string& defaultValue)
{
    auto it = properties.find(key);
    if (it != properties.end())
    {
        try
        {
            return std::get<std::string>(it->second);
        }
        catch (const std::bad_variant_access& e)
        {
            lg2::debug("Type mismatch for property {KEY}: {ERROR}", "KEY", key,
                       "ERROR", e.what());
        }
    }
    return defaultValue;
}

std::optional<uint8_t> DeviceNode::extractEID(const std::string& deviceAddress)
{
    if (deviceAddress.find("MCTP:") == 0)
    {
        try
        {
            uint8_t extractedEID =
                std::stoi(deviceAddress.substr(5)); // Skip "MCTP:"
            lg2::info("Extracted EID {EID} from DeviceAddress {ADDR}", "EID",
                      static_cast<int>(extractedEID), "ADDR", deviceAddress);
            return extractedEID;
        }
        catch (const std::exception& e)
        {
            lg2::warning(
                "Failed to extract EID from DeviceAddress {ADDR}: {ERROR}",
                "ADDR", deviceAddress, "ERROR", e.what());
        }
    }
    return std::nullopt;
}

} // namespace phosphor::device::manager
