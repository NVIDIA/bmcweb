// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "nvidia_igx.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

using boost::beast::http::status;

boost::system::error_code errc(boost::system::errc::errc_t code)
{
    return boost::system::errc::make_error_code(code);
}

TEST(NvidiaIgxSerialConsoleTest, SourceFromDbus)
{
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("Tegra"),
              "HostProcessor");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("SMCU"),
              "SafetyController");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus(""), std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("HostProcessor"),
              std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("SafetyController"),
              std::nullopt);
}

TEST(NvidiaIgxSerialConsoleTest, SourceToDbus)
{
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("HostProcessor"), "Tegra");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("SafetyController"),
              "SMCU");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("Tegra"), std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("SMCU"), std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus(""), std::nullopt);
}

TEST(NvidiaIgxSerialConsoleTest, GetReportsRedfishName)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    nvidia_igx::afterSerialConsoleGet(asyncResp, {}, "Tegra");
    EXPECT_EQ(asyncResp->res.result(), status::ok);
    EXPECT_EQ(asyncResp->res.jsonValue["SerialConsoleSource"], "HostProcessor");
}

TEST(NvidiaIgxSerialConsoleTest, GetIndeterminateIsNull)
{
    for (std::string_view source : {"", "Unexpected"})
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        nvidia_igx::afterSerialConsoleGet(asyncResp, {}, std::string(source));
        EXPECT_EQ(asyncResp->res.result(), status::ok);
        EXPECT_TRUE(asyncResp->res.jsonValue["SerialConsoleSource"].is_null());
    }
}

TEST(NvidiaIgxSerialConsoleTest, GetDaemonAbsentIsNull)
{
    for (auto code : {boost::system::errc::host_unreachable,
                      boost::system::errc::no_such_device_or_address})
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        nvidia_igx::afterSerialConsoleGet(asyncResp, errc(code), "");
        EXPECT_EQ(asyncResp->res.result(), status::ok);
        EXPECT_TRUE(asyncResp->res.jsonValue["SerialConsoleSource"].is_null());
    }
}

TEST(NvidiaIgxSerialConsoleTest, GetOtherErrorIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    nvidia_igx::afterSerialConsoleGet(asyncResp,
                                      errc(boost::system::errc::io_error), "");
    EXPECT_EQ(asyncResp->res.result(), status::internal_server_error);
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("SerialConsoleSource"));
}

TEST(NvidiaIgxSerialConsoleTest, SwitchErrors)
{
    struct Case
    {
        std::string_view dbusError;
        status expected;
        std::string_view retryAfter;
    };
    for (const Case& c : {
             Case{"com.nvidia.ConsoleMux.Error.Busy",
                  status::service_unavailable, "5"},
             Case{"org.freedesktop.DBus.Error.NoReply",
                  status::service_unavailable, "10"},
             Case{"org.freedesktop.DBus.Error.Timeout",
                  status::service_unavailable, "10"},
             Case{"org.freedesktop.DBus.Error.ServiceUnknown",
                  status::bad_request, ""},
             Case{"org.freedesktop.DBus.Error.NameHasNoOwner",
                  status::bad_request, ""},
             Case{"com.nvidia.ConsoleMux.Error.SwitchFailed",
                  status::internal_server_error, ""},
         })
    {
        crow::Response res;
        nvidia_igx::serialConsoleSwitchError(res, c.dbusError);
        EXPECT_EQ(res.result(), c.expected) << c.dbusError;
        EXPECT_EQ(res.getHeaderValue(boost::beast::http::field::retry_after),
                  c.retryAfter)
            << c.dbusError;
    }
}

TEST(NvidiaIgxSerialConsoleTest, AdvertiseLinkAndAction)
{
    nlohmann::json oem;
    nvidia_igx::advertiseSerialConsole(oem, "bmc");
    EXPECT_EQ(oem["@odata.type"], "#NvidiaManager.v1_12_0.NvidiaManager");
    EXPECT_EQ(oem["SerialConsole"]["@odata.id"],
              "/redfish/v1/Managers/bmc/Oem/Nvidia/SerialConsole");

    nlohmann::json actions;
    nvidia_igx::advertiseSwitchSerialConsole(actions, "bmc");
    const nlohmann::json& action =
        actions["#NvidiaManager.SwitchSerialConsole"];
    EXPECT_EQ(
        action["target"],
        "/redfish/v1/Managers/bmc/Actions/Oem/NvidiaManager.SwitchSerialConsole");
    EXPECT_EQ(action["Source@Redfish.AllowableValues"],
              nlohmann::json::array({"HostProcessor", "SafetyController"}));
}

constexpr const char* validOutcome = R"({
    "Outcome": "Confirmed",
    "Message": "Host is in recovery mode.",
    "RecoveryState": "Confirmed",
    "RecoveryActive": true,
    "LastCheckStatus": "Detected",
    "OperationState": "Idle"
})";

TEST(NvidiaIgxHostRecoveryTest, OutcomeAtTopLevel)
{
    crow::Response res;
    nvidia_igx::hostRecoveryRelayOutcome(res, validOutcome,
                                         "EnterHostRecovery");
    EXPECT_EQ(res.result(), status::ok);
    EXPECT_EQ(res.jsonValue["Outcome"], "Confirmed");
    EXPECT_EQ(res.jsonValue["Message"], "Host is in recovery mode.");
    EXPECT_EQ(res.jsonValue["RecoveryState"], "Confirmed");
    EXPECT_EQ(res.jsonValue["RecoveryActive"], true);
    EXPECT_EQ(res.jsonValue["LastCheckStatus"], "Detected");
    EXPECT_EQ(res.jsonValue["OperationState"], "Idle");
    EXPECT_FALSE(res.jsonValue.contains("Oem"));
}

TEST(NvidiaIgxHostRecoveryTest, EnterFailedFailsOnlyEnter)
{
    nlohmann::json reply = nlohmann::json::parse(validOutcome);
    reply["Outcome"] = "EnterFailed";
    reply["RecoveryState"] = "EnterFailed";
    reply["RecoveryActive"] = false;

    crow::Response enter;
    nvidia_igx::hostRecoveryRelayOutcome(enter, reply.dump(),
                                         "EnterHostRecovery");
    EXPECT_EQ(enter.result(), status::bad_gateway);
    EXPECT_EQ(enter.jsonValue["Outcome"], "EnterFailed");

    crow::Response check;
    nvidia_igx::hostRecoveryRelayOutcome(check, reply.dump(),
                                         "CheckHostRecoveryStatus");
    EXPECT_EQ(check.result(), status::ok);
    EXPECT_EQ(check.jsonValue["Outcome"], "EnterFailed");
}

TEST(NvidiaIgxHostRecoveryTest, MissingOrWrongTypedFieldIsInternalError)
{
    for (const char* key :
         {"Outcome", "Message", "RecoveryState", "RecoveryActive",
          "LastCheckStatus", "OperationState"})
    {
        nlohmann::json missing = nlohmann::json::parse(validOutcome);
        missing.erase(key);
        crow::Response res;
        nvidia_igx::hostRecoveryRelayOutcome(res, missing.dump(),
                                             "EnterHostRecovery");
        EXPECT_EQ(res.result(), status::internal_server_error) << key;
        EXPECT_FALSE(res.jsonValue.contains("Outcome")) << key;

        nlohmann::json wrongType = nlohmann::json::parse(validOutcome);
        wrongType[key] = 1;
        crow::Response res2;
        nvidia_igx::hostRecoveryRelayOutcome(res2, wrongType.dump(),
                                             "EnterHostRecovery");
        EXPECT_EQ(res2.result(), status::internal_server_error) << key;
        EXPECT_FALSE(res2.jsonValue.contains("Outcome")) << key;
    }
}

TEST(NvidiaIgxHostRecoveryTest, MalformedReplyIsInternalError)
{
    for (const char* payload : {"", "not json", "[]", "\"Confirmed\""})
    {
        crow::Response res;
        nvidia_igx::hostRecoveryRelayOutcome(res, payload, "ExitHostRecovery");
        EXPECT_EQ(res.result(), status::internal_server_error) << payload;
        EXPECT_FALSE(res.jsonValue.contains("Outcome")) << payload;
    }
}

TEST(NvidiaIgxHostRecoveryTest, GetReportsSchemaPropertiesOnly)
{
    dbus::utility::DBusPropertiesMap props = {
        {"RecoveryState", std::string("Confirmed")},
        {"LastCheckStatus", std::string("Detected")},
        {"OperationState", std::string("Idle")},
        {"RecoveryActive", true},
        {"RecoveryAttemptCount", uint32_t{2}},
        {"EvidenceSource", std::string("usb")},
        {"ObservedTegraState", std::string("APX")},
        {"BmcBootId", std::string("abc")},
        {"EvidenceProviderAvailable", true},
        {"RecoveryGeneration", uint64_t{7}},
        {"StateRevision", uint64_t{9}},
        {"LastRecoveryRequestedAt", uint64_t{100}},
        {"NextRecoveryAttemptAllowedAt", uint64_t{200}},
    };
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    nvidia_igx::afterHostRecoveryGet(asyncResp, {}, props);
    EXPECT_EQ(asyncResp->res.result(), status::ok);
    EXPECT_EQ(asyncResp->res.jsonValue,
              nlohmann::json({{"HostRecoveryAvailable", true},
                              {"RecoveryState", "Confirmed"},
                              {"LastCheckStatus", "Detected"},
                              {"OperationState", "Idle"},
                              {"RecoveryActive", true},
                              {"RecoveryAttemptCount", 2}}));
}

TEST(NvidiaIgxHostRecoveryTest, GetDaemonAbsentIsUnavailable)
{
    for (auto code : {boost::system::errc::host_unreachable,
                      boost::system::errc::no_such_device_or_address})
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        nvidia_igx::afterHostRecoveryGet(asyncResp, errc(code), {});
        EXPECT_EQ(asyncResp->res.result(), status::ok);
        EXPECT_EQ(asyncResp->res.jsonValue["HostRecoveryAvailable"], false);
        EXPECT_EQ(asyncResp->res.jsonValue["RecoveryState"], "Unknown");
        EXPECT_EQ(asyncResp->res.jsonValue["LastCheckStatus"], "Unknown");
        EXPECT_EQ(asyncResp->res.jsonValue["OperationState"], "Unknown");
    }
}

TEST(NvidiaIgxHostRecoveryTest, GetOtherErrorIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    nvidia_igx::afterHostRecoveryGet(asyncResp,
                                     errc(boost::system::errc::io_error), {});
    EXPECT_EQ(asyncResp->res.result(), status::internal_server_error);
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("HostRecoveryAvailable"));
}

TEST(NvidiaIgxHostRecoveryTest, ActionErrors)
{
    struct Case
    {
        std::string_view dbusError;
        status expected;
        std::string_view retryAfter;
    };
    for (const Case& c : {
             Case{"com.nvidia.HostRecovery.Error.Busy",
                  status::service_unavailable, "5"},
             Case{"com.nvidia.HostRecovery.Error.AttemptLimit",
                  status::service_unavailable, "900"},
             Case{"org.freedesktop.DBus.Error.NoReply",
                  status::service_unavailable, "10"},
             Case{"com.nvidia.HostRecovery.Error.NotAcknowledged",
                  status::bad_request, ""},
             Case{"com.nvidia.HostRecovery.Error.PoweredOff",
                  status::bad_request, ""},
             Case{"com.nvidia.HostRecovery.Error.NotSupported",
                  status::bad_request, ""},
             Case{"org.freedesktop.DBus.Error.ServiceUnknown",
                  status::bad_request, ""},
             Case{"com.nvidia.HostRecovery.Error.Failed",
                  status::internal_server_error, ""},
         })
    {
        crow::Response res;
        nvidia_igx::hostRecoveryError(res, c.dbusError, "EnterHostRecovery");
        EXPECT_EQ(res.result(), c.expected) << c.dbusError;
        EXPECT_EQ(res.getHeaderValue(boost::beast::http::field::retry_after),
                  c.retryAfter)
            << c.dbusError;
    }
}

TEST(NvidiaIgxHostRecoveryTest, AdvertiseLinkAndActions)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    nvidia_igx::advertiseHostRecovery(asyncResp, "system");
    const nlohmann::json& j = asyncResp->res.jsonValue;
    EXPECT_EQ(j["Oem"]["Nvidia"]["@odata.type"],
              "#NvidiaComputerSystem.v1_11_0.NvidiaComputerSystem");
    EXPECT_EQ(j["Oem"]["Nvidia"]["HostRecovery"]["@odata.id"],
              "/redfish/v1/Systems/system/Oem/Nvidia/HostRecovery");
    for (std::string_view action :
         {"EnterHostRecovery", "CheckHostRecoveryStatus", "ExitHostRecovery",
          "AbandonUnverifiedRecoverySession"})
    {
        EXPECT_EQ(
            j["Actions"]["Oem"][std::format("#NvidiaComputerSystem.{}", action)]
             ["target"],
            std::format(
                "/redfish/v1/Systems/system/Actions/Oem/NvidiaComputerSystem.{}",
                action))
            << action;
    }
}

} // namespace
} // namespace redfish
