/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bmcweb_aggregation_sse.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace redfish
{

// Minimal test subclass to capture forwarded events
class SseEventAggregatorForTest : public SseEventAggregator
{
  public:
    struct ForwardedEvent
    {
        nlohmann::json event;
        std::string origin;
        std::string resourceType;
    };
    std::vector<ForwardedEvent> forwardedEvents;

    explicit SseEventAggregatorForTest(boost::asio::io_context& iocIn) :
        SseEventAggregator(iocIn)
    {}

    void forwardSseEvent(const nlohmann::json::object_t& eventObject,
                         const std::string& origin,
                         const std::string& resourceType) override
    {
        forwardedEvents.push_back({eventObject, origin, resourceType});
    }
};

class SseEventAggregatorTest : public ::testing::Test
{
  protected:
    boost::asio::io_context ioc;
    std::unique_ptr<SseEventAggregatorForTest> aggregator;

    void SetUp() override
    {
        aggregator = std::make_unique<SseEventAggregatorForTest>(ioc);
    }

    void TearDown() override
    {
        if (aggregator)
        {
            aggregator->stop();
            aggregator.reset();
        }
    }

    std::string createSseEvent(const std::string& id, const std::string& data)
    {
        return "id: " + id + "\ndata: " + data + "\n\n";
    }

    // create a basic event JSON
    nlohmann::json createBasicEvent()
    {
        nlohmann::json event;
        event["@odata.type"] = "#Event.v1_9_0.Event";
        event["Events"] = nlohmann::json::array();

        nlohmann::json eventEntry;
        eventEntry["EventId"] = "1";
        eventEntry["EventType"] = "Alert";
        eventEntry["MessageId"] = "Test.1.0.Event";
        eventEntry["EventTimestamp"] = "2025-04-21T00:00:00Z";
        eventEntry["OriginOfCondition"] = "/redfish/v1/Systems/HGX_Baseboard_0";
        eventEntry["Message"] = "Test event";
        eventEntry["Severity"] = "OK";

        event["Events"].push_back(eventEntry);
        return event;
    }
};

TEST_F(SseEventAggregatorTest, GetInstanceCreatesAndInitializes)
{
    auto instance1 = std::make_unique<SseEventAggregator>(ioc);
    auto instance2 = std::make_unique<SseEventAggregator>(ioc);

    // Each instance is independent
    EXPECT_NE(instance1.get(), instance2.get());
}

TEST_F(SseEventAggregatorTest, handleSseData)
{
    aggregator->setAggregationPrefix("HGX");

    nlohmann::json event = createBasicEvent();
    event["Events"][0]["OriginOfCondition"] = {
        {"@odata.id", "/redfish/v1/Systems/Baseboard_0"}};

    std::string sseData = createSseEvent("1", event.dump());

    nlohmann::json expectedEvent = event["Events"][0];
    expectedEvent["OriginOfCondition"]["@odata.id"] =
        "/redfish/v1/Systems/HGX_Baseboard_0";

    aggregator->handleSseData(sseData);

    ASSERT_EQ(aggregator->forwardedEvents.size(), 1);
    const auto& fwdEvent = aggregator->forwardedEvents[0];

    EXPECT_EQ(fwdEvent.event, expectedEvent);
    EXPECT_EQ(fwdEvent.origin, "/redfish/v1/Systems/HGX_Baseboard_0");
    EXPECT_EQ(fwdEvent.resourceType, "Event");
}

TEST_F(SseEventAggregatorTest, ExtractAndProcessEvent)
{
    aggregator->setAggregationPrefix("HGX");

    nlohmann::json event = createBasicEvent();
    event["Events"][0]["OriginOfCondition"] = {
        {"@odata.id", "/redfish/v1/Systems/Baseboard_0"}};

    std::string sseData = createSseEvent("1", event.dump());
    nlohmann::json expectedEvent = event["Events"][0];
    expectedEvent["OriginOfCondition"]["@odata.id"] =
        "/redfish/v1/Systems/HGX_Baseboard_0";

    aggregator->extractAndProcessEvent(sseData);

    ASSERT_EQ(aggregator->forwardedEvents.size(), 1);
    const auto& fwdEvent = aggregator->forwardedEvents[0];
    EXPECT_EQ(fwdEvent.event, expectedEvent);
    EXPECT_EQ(fwdEvent.origin, "/redfish/v1/Systems/HGX_Baseboard_0");
    EXPECT_EQ(fwdEvent.resourceType, "Event");
}

TEST_F(SseEventAggregatorTest, EmptyEventData)
{
    // Test with empty event data,
    std::string emptyData = "";
    aggregator->handleSseData(emptyData);
    ASSERT_EQ(aggregator->forwardedEvents.size(), 0);
}

TEST_F(SseEventAggregatorTest, InvalidEventData)
{
    // Test with invalid JSON,
    std::string invalidData = createSseEvent("1", "{invalid json}");
    aggregator->handleSseData(invalidData);
    ASSERT_EQ(aggregator->forwardedEvents.size(), 0);
}

TEST_F(SseEventAggregatorTest, MalformedEventHandling)
{
    // Malformed JSON data
    std::string malformedData =
        "data: {\"EventId\": \"1\", \"EventType\": \"Alert\", \"MessageId\": \"Test.1.0.Event\", \"EventTimestamp\": \"2025-04-21T00:00:00Z\", \"OriginOfCondition\": \"/redfish/v1/Systems/HGX_Baseboard_0\", \"Message\": \"Test event\", \"Severity\": \"OK\""; // Missing closing curly brace
    aggregator->handleSseData(malformedData);
    EXPECT_EQ(aggregator->forwardedEvents.size(), 0);
}

TEST_F(SseEventAggregatorTest, OriginOfConditionPrefixObject)
{
    aggregator->setAggregationPrefix("HGX");
    nlohmann::json event = createBasicEvent();

    event["Events"][0]["OriginOfCondition"] = {
        {"@odata.id", "/redfish/v1/Chassis/BMC_0"}};

    std::string sseData = createSseEvent("1", event.dump());
    nlohmann::json expectedEvent = event["Events"][0];
    expectedEvent["OriginOfCondition"]["@odata.id"] =
        "/redfish/v1/Chassis/HGX_BMC_0";

    aggregator->handleSseData(sseData);

    ASSERT_EQ(aggregator->forwardedEvents.size(), 1);
    const auto& fwdEvent = aggregator->forwardedEvents[0];
    EXPECT_EQ(fwdEvent.event, expectedEvent);
    EXPECT_EQ(fwdEvent.origin, "/redfish/v1/Chassis/HGX_BMC_0");
    EXPECT_EQ(fwdEvent.resourceType, "Event");
}

TEST_F(SseEventAggregatorTest, OriginOfConditionInvalidFormat)
{
    aggregator->setAggregationPrefix("HGX");
    nlohmann::json event = createBasicEvent();

    // Set invalid format for OriginOfCondition (missing @odata.id)
    event["Events"][0]["OriginOfCondition"] = {
        {"invalid_field", "invalid_value"}};

    std::string sseData = createSseEvent("1", event.dump());
    const nlohmann::json expectedEvent = event["Events"][0];

    aggregator->handleSseData(sseData);

    ASSERT_EQ(aggregator->forwardedEvents.size(), 1);
    const auto& fwdEvent = aggregator->forwardedEvents[0];
    EXPECT_EQ(fwdEvent.event, expectedEvent);
    EXPECT_EQ(fwdEvent.origin, "");
    EXPECT_EQ(fwdEvent.resourceType, "Event");
}

TEST_F(SseEventAggregatorTest, OriginOfConditionEmptyObject)
{
    aggregator->setAggregationPrefix("HGX");
    nlohmann::json event = createBasicEvent();

    // Set OriginOfCondition as empty object
    event["Events"][0]["OriginOfCondition"] = {};

    std::string sseData = createSseEvent("1", event.dump());
    const nlohmann::json expectedEvent = event["Events"][0];

    aggregator->handleSseData(sseData);

    ASSERT_EQ(aggregator->forwardedEvents.size(), 1);
    const auto& fwdEvent = aggregator->forwardedEvents[0];
    EXPECT_EQ(fwdEvent.event, expectedEvent);
    EXPECT_EQ(fwdEvent.origin, "");
    EXPECT_EQ(fwdEvent.resourceType, "Event");
}

TEST_F(SseEventAggregatorTest, MultipleEvents)
{
    aggregator->setAggregationPrefix("HGX");

    // First event
    nlohmann::json event1 = createBasicEvent();
    event1["Events"][0]["OriginOfCondition"] = {
        {"@odata.id", "/redfish/v1/Systems/Baseboard_0"}};

    std::string sseEventData1 = createSseEvent("1", event1.dump());

    nlohmann::json expectedEvent1 = event1["Events"][0];
    expectedEvent1["OriginOfCondition"]["@odata.id"] =
        "/redfish/v1/Systems/HGX_Baseboard_0";

    // Second event
    nlohmann::json event2 = createBasicEvent();
    event2["Events"][0]["EventId"] = "2";
    event2["Events"][0]["EventType"] = "StatusChange";
    event2["Events"][0]["MessageId"] = "Test.1.0.StatusChange";
    event2["Events"][0]["OriginOfCondition"] = {
        {"@odata.id", "/redfish/v1/Managers/BMC_0"}};
    event2["Events"][0]["Message"] = "Status changed";

    std::string sseEventData2 = createSseEvent("2", event2.dump());

    nlohmann::json expectedEvent2 = event2["Events"][0];
    expectedEvent2["OriginOfCondition"]["@odata.id"] =
        "/redfish/v1/Managers/HGX_BMC_0";

    aggregator->handleSseData(sseEventData1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    aggregator->handleSseData(sseEventData2);

    ASSERT_EQ(aggregator->forwardedEvents.size(), 2);
    EXPECT_EQ(aggregator->forwardedEvents[0].event, expectedEvent1);
    EXPECT_EQ(aggregator->forwardedEvents[0].origin,
              "/redfish/v1/Systems/HGX_Baseboard_0");
    EXPECT_EQ(aggregator->forwardedEvents[0].resourceType, "Event");

    EXPECT_EQ(aggregator->forwardedEvents[1].event, expectedEvent2);
    EXPECT_EQ(aggregator->forwardedEvents[1].origin,
              "/redfish/v1/Managers/HGX_BMC_0");
    EXPECT_EQ(aggregator->forwardedEvents[1].resourceType, "Event");
}

TEST_F(SseEventAggregatorTest, MultipleEventsSentInOneRequest)
{
    aggregator->setAggregationPrefix("HGX");

    // Create event with multiple entries
    nlohmann::json event = createBasicEvent();
    // Add second event entry
    nlohmann::json secondEntry = event["Events"][0];
    secondEntry["EventId"] = "2";
    event["Events"].push_back(secondEntry);

    std::string sseEventData = createSseEvent("1", event.dump());

    aggregator->handleSseData(sseEventData);
    ASSERT_EQ(aggregator->forwardedEvents.size(), 2);
    EXPECT_EQ(aggregator->forwardedEvents[0].event["EventId"], "1");
    EXPECT_EQ(aggregator->forwardedEvents[1].event["EventId"], "2");
}

TEST_F(SseEventAggregatorTest, HandleStreamingData)
{
    aggregator->setAggregationPrefix("HGX");

    nlohmann::json event1 = createBasicEvent();
    event1["Events"][0]["EventId"] = "1";
    event1["Events"][0]["OriginOfCondition"] = {
        {"@odata.id", "/redfish/v1/Systems/Baseboard_0"}};

    nlohmann::json event2 = createBasicEvent();
    event2["Events"][0]["EventId"] = "2";
    event2["Events"][0]["OriginOfCondition"] = {
        {"@odata.id", "/redfish/v1/Systems/Baseboard_1"}};

    std::string sseEventData =
        createSseEvent("1", event1.dump()) + createSseEvent("2", event2.dump());

    aggregator->handleSseData(sseEventData);

    ASSERT_EQ(aggregator->forwardedEvents.size(), 2);
    EXPECT_EQ(aggregator->forwardedEvents[0].event["EventId"], "1");
    EXPECT_EQ(aggregator->forwardedEvents[1].event["EventId"], "2");
}

} // namespace redfish
