#include "health_metric.hpp"

#include <sdbusplus/server/interface.hpp>
#include <sdbusplus/test/sdbus_mock.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <cerrno>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ConfigIntf = phosphor::health::metric::config;
using PathIntf =
    sdbusplus::server::xyz::openbmc_project::metric::Value::namespace_path;
using namespace phosphor::health::metric;
using namespace phosphor::health::utils;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Pair;
using ::testing::StrEq;

class HealthMetricTest : public ::testing::Test
{
  public:
    sdbusplus::SdBusMock sdbusMock;
    sdbusplus::bus_t bus = sdbusplus::get_mocked_new(&sdbusMock);
    static constexpr auto busName = "xyz.openbmc_project.test.HealthMon";
    const std::set<std::string> properties = {"Value", "MaxValue", "MinValue",
                                              "Unit"};
    const std::string objPath =
        std::string(PathIntf::value) + "/bmc/" + PathIntf::kernel_cpu;
    ConfigIntf::HealthMetric config;

    void SetUp() override
    {
        config.name = "CPU_Kernel";
        config.subType = SubType::cpuKernel;
        config.windowSize = 1;
        config.thresholds = {
            {{ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Upper},
             {.value = 90.0, .log = true, .target = ""}},
            {{ThresholdIntf::Type::Warning, ThresholdIntf::Bound::Upper},
             {.value = 80.0, .log = false, .target = ""}}};
        config.path = "";
        HealthMetric::setwaitForActionDelay(false);
    }
};

TEST_F(HealthMetricTest, TestLogSendFailure)
{
    auto unmockedBus = sdbusplus::bus::new_bus();
    EXPECT_CALL(sdbusMock, sd_bus_get_fd(IsNull()))
        .WillOnce(testing::Return(unmockedBus.get_fd()));
    sdbusplus::async::context ctx(sdbusplus::get_mocked_new(&sdbusMock));

    EXPECT_CALL(sdbusMock,
                sd_bus_message_new_method_call(
                    IsNull(), NotNull(), StrEq("xyz.openbmc_project.Logging"),
                    StrEq("/xyz/openbmc_project/logging"),
                    StrEq("xyz.openbmc_project.Logging.Create"),
                    StrEq("Create")))
        .Times(2)
        .WillRepeatedly(testing::Return(-ENOTCONN));

    EXPECT_THROW(
        sdbusplus::async::execution::sync_wait(asyncCreateRFLogEntry(
            ctx, "OpenBMC.0.5.ServiceRestart", "pcmd",
            "xyz.openbmc_project.Logging.Entry.Level.Informational", "none")),
        sdbusplus::exception::exception);

    EXPECT_NO_THROW(sdbusplus::async::execution::sync_wait(
        asyncCreateRFLogEntry(
            ctx, "OpenBMC.0.5.ServiceRestart", "pcmd",
            "xyz.openbmc_project.Logging.Entry.Level.Informational", "none") |
        log_and_ignore_errors()));
}

TEST(BackgroundTaskTest, LogsDbusFailure)
{
    sdbusplus::async::context ctx;
    // Own the destination without exporting Create, so the bus returns an
    // error.
    ctx.request_name("xyz.openbmc_project.Logging");
    bool completed = false;
    testing::internal::CaptureStderr();

    ctx.spawn(
        asyncCreateRFLogEntry(
            ctx, "OpenBMC.0.5.ServiceRestart", "lpulog",
            "xyz.openbmc_project.Logging.Entry.Level.Informational", "none") |
        log_and_ignore_errors() |
        sdbusplus::async::execution::then([&]() noexcept {
            completed = true;
            ctx.request_stop();
        }));

    ctx.run();
    const auto output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(completed);
    EXPECT_THAT(output, testing::HasSubstr("Background task failed:"));
    EXPECT_THAT(output, testing::HasSubstr("org.freedesktop.DBus.Error."));
}

namespace
{
int createLogEntry(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    auto& calls = *static_cast<unsigned int*>(userdata);
    ++calls;
    sdbusplus::message_t request(message);
    const auto [messageId, level, additionalData] =
        request.unpack<std::string, std::string,
                       std::map<std::string, std::string>>();
    EXPECT_EQ(messageId, "OpenBMC.0.5.ServiceRestart");
    EXPECT_EQ(level, "xyz.openbmc_project.Logging.Entry.Level.Informational");
    const std::map<std::string, std::string> expectedData = {
        {"REDFISH_MESSAGE_ID", "OpenBMC.0.5.ServiceRestart"},
        {"REDFISH_MESSAGE_ARGS", "lpulog"},
        {"xyz.openbmc_project.Logging.Entry.Resolution", "none"}};
    EXPECT_EQ(additionalData, expectedData);

    auto reply = request.new_method_return();
    reply.append(sdbusplus::message::object_path(
        "/xyz/openbmc_project/logging/entry/1"));
    reply.method_return();
    return 1;
}
} // namespace

TEST(BackgroundTaskTest, ConsumesSuccessfulLoggingReply)
{
    sdbusplus::async::context ctx;
    unsigned int calls = 0;
    bool completed = false;
    static const sdbusplus::vtable_t vtable[] = {
        sdbusplus::vtable::start(),
        sdbusplus::vtable::method("Create", "ssa{ss}", "o", createLogEntry,
                                  sdbusplus::vtable::common_::unprivileged),
        sdbusplus::vtable::end()};
    sdbusplus::server::interface_t logging(
        ctx.get_bus(), "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", vtable, &calls);
    ctx.request_name("xyz.openbmc_project.Logging");

    ctx.spawn(
        asyncCreateRFLogEntry(
            ctx, "OpenBMC.0.5.ServiceRestart", "lpulog",
            "xyz.openbmc_project.Logging.Entry.Level.Informational", "none") |
        sdbusplus::async::execution::then([&]() noexcept {
            completed = true;
            ctx.request_stop();
        }));

    ctx.run();
    EXPECT_EQ(calls, 1u);
    EXPECT_TRUE(completed);
}

TEST_F(HealthMetricTest, TestMetricUnmockedObjectAddRemove)
{
    sdbusplus::bus_t unmockedBus = sdbusplus::bus::new_bus();
    unmockedBus.request_name(busName);
    auto metric = std::make_unique<HealthMetric>(unmockedBus, Type::cpu, config,
                                                 paths_t());
}

TEST_F(HealthMetricTest, TestMetricThresholdChange)
{
    sdbusplus::server::manager_t objManager(bus, objPath.c_str());
    bus.request_name(busName);
    const auto thresholdProperties = std::set<std::string>{"Value", "Asserted"};

    EXPECT_CALL(sdbusMock, sd_bus_emit_properties_changed_strv(
                               IsNull(), StrEq(objPath),
                               StrEq(ValueIntf::interface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
                EXPECT_THAT(properties, testing::Contains(names[0]));
                return 0;
            }));
    EXPECT_CALL(sdbusMock, sd_bus_emit_properties_changed_strv(
                               IsNull(), StrEq(objPath),
                               StrEq(ThresholdIntf::interface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
                EXPECT_THAT(thresholdProperties, testing::Contains(names[0]));
                return 0;
            }));
    EXPECT_CALL(sdbusMock,
                sd_bus_message_new_signal(_, _, StrEq(objPath),
                                          StrEq(ThresholdIntf::interface),
                                          StrEq("AssertionChanged")))
        .Times(4);

    auto metric =
        std::make_unique<HealthMetric>(bus, Type::cpu, config, paths_t());
    // Exceed the critical threshold
    metric->update(MValue(1351, 1500));
    // Go below critical threshold but above warning threshold
    metric->update(MValue(1399, 1500));
    // Go below warning threshold
    metric->update(MValue(1199, 1500));
}
