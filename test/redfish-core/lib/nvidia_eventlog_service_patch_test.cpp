// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "http_request.hpp"
#include "nvidia_log_services.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <nlohmann/json.hpp>

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

crow::Request eventLogPatchRequest(std::string_view body)
{
    std::error_code ec;
    crow::Request req(
        crow::Request::Body{
            boost::beast::http::verb::patch,
            std::format("/redfish/v1/Systems/{}/LogServices/EventLog/",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME),
            11, std::string(body)},
        ec);
    req.addHeader("Content-Type", "application/json");
    return req;
}

const std::string* firstMessageId(const nlohmann::json& json)
{
    const nlohmann::json::object_t* object =
        json.get_ptr<const nlohmann::json::object_t*>();
    if (object == nullptr)
    {
        return nullptr;
    }
    auto error = object->find("error");
    if (error == object->end())
    {
        return nullptr;
    }
    const nlohmann::json::object_t* errorObject =
        error->second.get_ptr<const nlohmann::json::object_t*>();
    if (errorObject == nullptr)
    {
        return nullptr;
    }
    auto info = errorObject->find("@Message.ExtendedInfo");
    if (info == errorObject->end())
    {
        return nullptr;
    }
    const nlohmann::json::array_t* messages =
        info->second.get_ptr<const nlohmann::json::array_t*>();
    if (messages == nullptr || messages->empty())
    {
        return nullptr;
    }
    const nlohmann::json::object_t* message =
        messages->front().get_ptr<const nlohmann::json::object_t*>();
    if (message == nullptr)
    {
        return nullptr;
    }
    auto id = message->find("MessageId");
    if (id == message->end())
    {
        return nullptr;
    }
    return id->second.get_ptr<const std::string*>();
}

// {"Oem": {}} has no Nvidia object, so there is nothing to change.
TEST(NvidiaEventLogServicePatch, OemWithoutNvidiaChangesNothing)
{
    App app;
    crow::Request req = eventLogPatchRequest(R"({"Oem":{}})");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleEventLogServicePatch(app, req, asyncResp,
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(firstMessageId(asyncResp->res.jsonValue), nullptr);
}

TEST(NvidiaEventLogServicePatch, EmptyNvidiaChangesNothing)
{
    App app;
    crow::Request req = eventLogPatchRequest(R"({"Oem":{"Nvidia":{}}})");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleEventLogServicePatch(app, req, asyncResp,
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(firstMessageId(asyncResp->res.jsonValue), nullptr);
}

TEST(NvidiaEventLogServicePatch, UnknownOemPropertyIsRejected)
{
    App app;
    crow::Request req = eventLogPatchRequest(R"({"Oem":{"Bogus":true}})");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleEventLogServicePatch(app, req, asyncResp,
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    const std::string* id = firstMessageId(asyncResp->res.jsonValue);
    ASSERT_NE(id, nullptr);
    EXPECT_TRUE(id->ends_with(".PropertyUnknown")) << *id;
}

TEST(NvidiaEventLogServicePatch, UnknownNvidiaPropertyIsRejected)
{
    App app;
    crow::Request req =
        eventLogPatchRequest(R"({"Oem":{"Nvidia":{"Bogus":true}}})");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleEventLogServicePatch(app, req, asyncResp,
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    const std::string* id = firstMessageId(asyncResp->res.jsonValue);
    ASSERT_NE(id, nullptr);
    EXPECT_TRUE(id->ends_with(".PropertyUnknown")) << *id;
}

TEST(NvidiaEventLogServicePatch, NonObjectOemIsRejected)
{
    App app;
    crow::Request req = eventLogPatchRequest(R"({"Oem":"Nvidia"})");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleEventLogServicePatch(app, req, asyncResp,
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

} // namespace
} // namespace redfish
