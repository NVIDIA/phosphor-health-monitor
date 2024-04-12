#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
PHOSPHOR_LOG2_USING;
// Callback function to handle signals
void signalHandler(sdbusplus::message::message& msg)
{
    std::string interfaceName;
    std::string signalName;
    if (msg.get_type() != SD_BUS_MESSAGE_SIGNAL)
    {
        lg2::info("Dbus logging signal error.");
        return;
    }
    interfaceName = msg.get_interface();
    signalName = msg.get_member();

    lg2::info("Received signal: {SIGNAL} from interface: {INTERFACE}", "SIGNAL",
              signalName, "INTERFACE", interfaceName);
    while (1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main()
{
    // Create a new default system bus connection
    auto bus = sdbusplus::bus::new_default_system();

    // Define the match rule for signals
    std::string matchRule = "type='signal', "
                            "path_namespace='/xyz/openbmc_project'";

    std::shared_ptr<sdbusplus::bus::match_t> matchDbusLogging =
        std::make_shared<sdbusplus::bus::match_t>(bus, matchRule,
                                                  signalHandler);
    if (matchDbusLogging)
    {
        lg2::info("Match rule created successfully");
    }
    else
    {
        error("Failed to create match rule");
    }
    // Enter the processing loop to handle incoming signals
    while (true)
    {
        bus.process_discard();
        bus.wait();
    }

    return 0;
}
