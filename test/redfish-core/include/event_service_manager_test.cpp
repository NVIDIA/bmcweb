#include "event_service_manager.hpp"
#include "filter_expr_printer.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>

#include <gtest/gtest.h>

namespace redfish
{

static TestEvent createTestEvent()
{
    TestEvent testEvent;
    testEvent.eventGroupId = 1;
    testEvent.eventTimestamp = "2021-01";
    testEvent.message = "Test Message";
    testEvent.messageArgs = std::vector<std::string>{"arg1", "arg2"};
    testEvent.messageId = "Dummy message ID";
    testEvent.originOfCondition = "/redfish/v1/Chassis/GPU_SXM_1";
    testEvent.resolution = "custom resolution";
    testEvent.severity = "whatever";
    return testEvent;
}

TEST(EventServiceManager, eventMatchesFilter)
{
    boost::asio::io_context io;
    boost::urls::url url;

    {
        Subscription sub(url, io);
        nlohmann::json::object_t event;

        // Default constructed should always pass
        EXPECT_TRUE(sub.eventMatchesFilter(event, "Event"));
    }
    {
        nlohmann::json::object_t event;
        // Resource types filter
        Subscription sub(url, io);
        sub.resourceTypes.emplace_back("Task");
        EXPECT_FALSE(sub.eventMatchesFilter(event, "Event"));
        EXPECT_TRUE(sub.eventMatchesFilter(event, "Task"));
    }
    {
        nlohmann::json::object_t event;
        // Resource types filter
        Subscription sub(url, io);
        sub.registryMsgIds.emplace_back("OpenBMC.PostComplete");

        // Correct message registry
        event["MessageId"] = "OpenBMC.0.1.PostComplete";
        EXPECT_TRUE(sub.eventMatchesFilter(event, "Event"));

        // Different message registry
        event["MessageId"] = "Task.0.1.PostComplete";
        EXPECT_FALSE(sub.eventMatchesFilter(event, "Event"));

        // Different MessageId
        event["MessageId"] = "OpenBMC.0.1.NoMatch";
        EXPECT_FALSE(sub.eventMatchesFilter(event, "Event"));
    }
    {
        nlohmann::json::object_t event;
        // Resource types filter
        Subscription sub(url, io);
        event["MessageId"] = "OpenBMC.0.1.PostComplete";

        // Correct message registry
        sub.filter = parseFilter("MessageId eq 'OpenBMC.0.1.PostComplete'");
        EXPECT_TRUE(sub.eventMatchesFilter(event, "Event"));

        // Different message registry
        sub.filter = parseFilter("MessageId ne 'OpenBMC.0.1.PostComplete'");
        EXPECT_FALSE(sub.eventMatchesFilter(event, "Event"));
    }
    {
        nlohmann::json::object_t event;
        // Resource types filter
        Subscription sub(url, io);
        event["MessageId"] = "OpenBMC.0.1.PostComplete";

        // Correct message registry
        sub.registryPrefixes.emplace_back("OpenBMC");
        EXPECT_TRUE(sub.eventMatchesFilter(event, "Event"));

        // Different message registry
        event["MessageId"] = "Task.0.1.PostComplete";
        EXPECT_FALSE(sub.eventMatchesFilter(event, "Event"));
    }
    {
        nlohmann::json::object_t event;
        // Resource types filter
        {
            Subscription sub(url, io);
            event["OriginOfCondition"] = "/redfish/v1/Managers/bmc";

            // Correct origin
            sub.originResources.emplace_back("/redfish/v1/Managers/bmc");
            EXPECT_TRUE(sub.eventMatchesFilter(event, "Event"));
        }
        {
            Subscription sub(url, io);
            // Incorrect origin
            sub.originResources.clear();
            sub.originResources.emplace_back("/redfish/v1/Managers/bmc_not");
            EXPECT_FALSE(sub.eventMatchesFilter(event, "Event"));
        }
    }
}

TEST(EventServiceManager, submitTestEVent)
{
    boost::asio::io_context io;
    boost::urls::url url;
    EventServiceManager& evt = EventServiceManager::getInstance(&io);
    {
        TestEvent testEvent;
        EXPECT_TRUE(evt.sendTestEventLog(testEvent));
    }
    {
        TestEvent testEvent;
        testEvent.eventGroupId = 1;
        testEvent.eventTimestamp = "2021-01-01T00:00:00Z";
        testEvent.message = "Custom Message";
        testEvent.messageArgs = std::vector<std::string>{"GPU_SXM_1",
                                                         "Restart Recommended"};
        testEvent.messageId = "Base.1.13.ResetRecommended";
        testEvent.originOfCondition = "/redfish/v1/Chassis/GPU_SXM_1";
        testEvent.resolution =
            "Reset the GPU at the next service window since the ECC errors are contained";
        testEvent.severity = "Informational";
        EXPECT_TRUE(evt.sendTestEventLog(testEvent));
    }
    {
        TestEvent testEvent = createTestEvent();

        bool result = evt.sendTestEventLog(testEvent);

        EXPECT_TRUE(result);

        EXPECT_EQ(testEvent.eventGroupId.value(), 1);
        EXPECT_EQ(testEvent.eventTimestamp.value(), "2021-01");
        EXPECT_EQ(testEvent.message.value(), "Test Message");
        EXPECT_EQ(testEvent.messageId.value(), "Dummy message ID");
        EXPECT_EQ(testEvent.originOfCondition.value(),
                  "/redfish/v1/Chassis/GPU_SXM_1");
        EXPECT_EQ(testEvent.resolution.value(), "custom resolution");
        EXPECT_EQ(testEvent.severity.value(), "whatever");
    }
}
} // namespace redfish
