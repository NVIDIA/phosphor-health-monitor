#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>

#include <functional>
#include <memory>

namespace phosphor::device::manager
{

// MCTP D-Bus constants
static constexpr const char* MCTP_SERVICE = "au.com.codeconstruct.MCTP1";
static constexpr const char* MCTP_PATH =
    "/au/com/codeconstruct/mctp1/networks/1";
static constexpr const char* MCTP_ENDPOINT_INTERFACE =
    "au.com.codeconstruct.MCTP.Endpoint1";

/**
 * @brief MCTP Interface Monitor - watches for MCTP endpoint lifecycle events
 */
class MCTPInterface
{
  public:
    /**
     * @brief Constructor
     * @param bus D-Bus connection
     */
    explicit MCTPInterface(sdbusplus::bus_t& bus);

    /**
     * @brief Destructor
     */
    ~MCTPInterface() = default;

  private:
    /**
     * @brief Handler for MCTP InterfacesAdded signals
     * @param msg D-Bus message containing the signal data
     */
    void handleInterfacesAdded(sdbusplus::message_t& msg);

    /**
     * @brief Extract EID from MCTP D-Bus object path
     * @param objectPath MCTP endpoint object path
     * @return Extracted EID value, or 0 if parsing fails
     */
    uint8_t extractEIDFromPath(const std::string& objectPath);

    /** @brief D-Bus connection reference */
    sdbusplus::bus_t& bus;

    /** @brief Match for MCTP InterfacesAdded signals */
    std::unique_ptr<sdbusplus::bus::match_t> interfacesAddedMatch;
};

} // namespace phosphor::device::manager
