// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "network_adapter.hpp"

#include <boost/asio/error.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/linux_error.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

const boost::system::error_code mapperNotFound(
    boost::system::linux_error::bad_request_descriptor,
    boost::system::system_category());
const boost::system::error_code dbusFailure = boost::asio::error::timed_out;

constexpr const char* forceRestart =
    "xyz.openbmc_project.Control.Reset.ResetTypes.ForceRestart";

TEST(NetworkAdapterResetType, MappedValueIsAdvertisedAlone)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetNetworkAdapterResetType(response, "chassis", "nic", {},
                                    forceRestart);

    EXPECT_EQ(response->res.result(), boost::beast::http::status::ok);
    const nlohmann::json& action =
        response->res.jsonValue["Actions"]["#NetworkAdapter.Reset"];
    EXPECT_EQ(action["target"],
              "/redfish/v1/Chassis/chassis/NetworkAdapters/nic/Actions/"
              "NetworkAdapter.Reset");
    EXPECT_EQ(action["ResetType@Redfish.AllowableValues"],
              nlohmann::json::array({"ForceRestart"}));
}

TEST(NetworkAdapterResetType, EveryMappedValueIsAdvertisedAlone)
{
    const std::vector<std::pair<std::string, std::string>> mapped = {
        {"xyz.openbmc_project.Control.Reset.ResetTypes.ForceOff", "ForceOff"},
        {"xyz.openbmc_project.Control.Reset.ResetTypes.ForceOn", "ForceOn"},
        {forceRestart, "ForceRestart"},
        {"xyz.openbmc_project.Control.Reset.ResetTypes.GracefulRestart",
         "GracefulRestart"},
        {"xyz.openbmc_project.Control.Reset.ResetTypes.GracefulShutdown",
         "GracefulShutdown"},
    };

    for (const auto& [dbusValue, redfishValue] : mapped)
    {
        auto response = std::make_shared<bmcweb::AsyncResp>();

        afterGetNetworkAdapterResetType(response, "chassis", "nic", {},
                                        dbusValue);

        EXPECT_EQ(response->res.result(), boost::beast::http::status::ok)
            << dbusValue;
        EXPECT_EQ(response->res.jsonValue["Actions"]["#NetworkAdapter.Reset"]
                                         ["ResetType@Redfish.AllowableValues"],
                  nlohmann::json::array({redfishValue}))
            << dbusValue;
    }
}

TEST(NetworkAdapterResetType, EmptyValueOmitsAction)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetNetworkAdapterResetType(response, "chassis", "nic", {}, "");

    EXPECT_EQ(response->res.result(), boost::beast::http::status::ok);
    EXPECT_FALSE(response->res.jsonValue.contains("Actions"));
}

TEST(NetworkAdapterResetType, MapsOnlyKnownDbusValues)
{
    EXPECT_EQ(getNetworkAdapterResetType(forceRestart), "ForceRestart");
    // A bare Redfish name is not a D-Bus ResetTypes value.
    EXPECT_EQ(getNetworkAdapterResetType("ForceRestart"), "");
    EXPECT_EQ(getNetworkAdapterResetType(""), "");
}

TEST(NetworkAdapterResetPost, NoResetAsyncServiceIsActionNotSupported)
{
    // Same condition that hides the action on GET: not an internal error.
    const std::vector<std::pair<std::string, std::vector<std::string>>>
        otherInterfacesOnly = {{"xyz.openbmc_project.Service",
                                {"xyz.openbmc_project.Control.Reset"}}};

    for (const auto& serviceMap :
         {std::vector<std::pair<std::string, std::vector<std::string>>>{},
          otherInterfacesOnly})
    {
        auto response = std::make_shared<bmcweb::AsyncResp>();

        networkAdapterPostResetType(response, "nic", "/path", "ForceRestart",
                                    serviceMap);

        EXPECT_EQ(response->res.result(),
                  boost::beast::http::status::bad_request);
        const nlohmann::json& info =
            response->res.jsonValue["error"]["@Message.ExtendedInfo"][0];
        EXPECT_NE(
            info["MessageId"].get<std::string>().find("ActionNotSupported"),
            std::string::npos);
    }
}

TEST(NetworkAdapterResetType, UnmappedValueOmitsAction)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetNetworkAdapterResetType(response, "chassis", "nic", {},
                                    "xyz.openbmc_project.Control.Reset."
                                    "ResetTypes.Unknown");

    EXPECT_EQ(response->res.result(), boost::beast::http::status::ok);
    EXPECT_FALSE(response->res.jsonValue.contains("Actions"));
}

TEST(NetworkAdapterResetType, ReadErrorIsInternalError)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetNetworkAdapterResetType(response, "chassis", "nic", dbusFailure,
                                    "");

    EXPECT_EQ(response->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_FALSE(response->res.jsonValue.contains("Actions"));
}

TEST(NetworkAdapterResetObject, AbsentInterfaceOmitsActionWithoutError)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetNetworkAdapterResetObject(response, "chassis", "nic", "/path",
                                      mapperNotFound, {});

    EXPECT_EQ(response->res.result(), boost::beast::http::status::ok);
    EXPECT_FALSE(response->res.jsonValue.contains("Actions"));
}

TEST(NetworkAdapterResetObject, EmptyObjectOmitsActionWithoutError)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetNetworkAdapterResetObject(response, "chassis", "nic", "/path", {},
                                      {});

    EXPECT_EQ(response->res.result(), boost::beast::http::status::ok);
    EXPECT_FALSE(response->res.jsonValue.contains("Actions"));
}

TEST(NetworkAdapterResetObject, UnexpectedErrorIsInternalError)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetNetworkAdapterResetObject(response, "chassis", "nic", "/path",
                                      dbusFailure, {});

    EXPECT_EQ(response->res.result(),
              boost::beast::http::status::internal_server_error);
}

} // namespace
} // namespace redfish
