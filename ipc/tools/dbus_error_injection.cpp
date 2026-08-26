#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>
PHOSPHOR_LOG2_USING;
// Callback function to handle signals
void signalHandler(sdbusplus::message_t& msg)
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
try
{
    // Create a new default system bus connection
    auto bus = sdbusplus::bus::new_default_system();

    // Define the match rule for signals
    std::string matchRule = "type='signal', "
                            "path_namespace='/xyz/openbmc_project'";

    // std::make_shared either succeeds or throws std::bad_alloc — never
    // returns a null shared_ptr, so a defensive 'if (matchDbusLogging)'
    // check would be tautological dead code (Coverity CID 22297031).
    auto matchDbusLogging = std::make_shared<sdbusplus::bus::match_t>(
        bus, matchRule, signalHandler);
    lg2::info("Match rule created successfully");

    // Enter the processing loop to handle incoming signals
    while (true)
    {
        bus.process_discard();
        bus.wait();
    }

    return 0;
}
catch (const std::exception& e)
{
    lg2::error("dbus_error_injection: unhandled exception in main: {ERR}",
               "ERR", e.what());
    return EXIT_FAILURE;
}
catch (...)
{
    lg2::error("dbus_error_injection: unknown unhandled exception in main");
    return EXIT_FAILURE;
}
