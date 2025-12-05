#pragma once

#include "em_interface.hpp"

#include <optional>
#include <string>

namespace phosphor::device::manager
{

// PropertyValue and PropertyMap are defined in em_interface.hpp

/**
 * @brief Simple device node - stores only needed EM fields
 */
struct DeviceNode
{
    std::string name;              // From "Name"
    std::string physicalInterface; // From "PhysicalInterface" (e.g.,
                                   // "usb:0:0x0955:0xcf11:0:0x02:0x81:1.3.4")
    std::optional<uint8_t>
        eid; // Extracted from "DeviceAddress" (e.g., "MCTP:19" -> 19)

    /**
     * @brief Construct DeviceNode from EntityManager properties
     * @param emProperties Properties from EM
     */
    explicit DeviceNode(const PropertyMap& emProperties);

    /**
     * @brief Check if device has a physical interface to verify
     * @return true if physicalInterface is not empty
     */
    bool hasPhysicalInterface() const
    {
        return !physicalInterface.empty();
    }

  private:
    /**
     * @brief Extract string property from EM properties
     * @param properties EM properties map
     * @param key Property key
     * @param defaultValue Default if not found
     * @return Property value or default
     */
    static std::string getStringProperty(const PropertyMap& properties,
                                         const std::string& key,
                                         const std::string& defaultValue = "");

    /**
     * @brief Extract EID from DeviceAddress property (e.g., "MCTP:19" -> 19)
     * @param deviceAddress DeviceAddress string from EM
     * @return EID if found, nullopt otherwise
     */
    static std::optional<uint8_t> extractEID(const std::string& deviceAddress);
};

} // namespace phosphor::device::manager
