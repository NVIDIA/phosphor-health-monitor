#include "config.h"

#include "ipcMonitor.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/sd_event.hpp>

#include <chrono>

/**
 * @brief Main
 */
int main()
{
    // The io_context is needed for the timer
    boost::asio::io_context io;

    // DBus connection
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);

    // Get a default event loop
    auto event = sdeventplus::Event::get_default();
    // Create an IPC monitor object
    std::shared_ptr<phosphor::ipc::IPCMonitor> ipcMon =
        std::make_shared<phosphor::ipc::IPCMonitor>(*conn, io);

    // Sleep until system boot
    ipcMon->sleepuntilSystemBoot();

    // create sensors
    ipcMon->createSensors();

    // Run the io_context
    io.run();
    return 0;
}
