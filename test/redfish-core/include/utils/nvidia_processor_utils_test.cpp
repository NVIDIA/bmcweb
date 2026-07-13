#include "http_response.hpp"
#include "nvidia_error_messages.hpp"
#include "utils/nvidia_processor_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

namespace redfish::nvidia_processor_utils
{
namespace
{

std::string renderMask(uint64_t enableMask, uint64_t supportedMask)
{
    return formatPCIeLinkEnableMask(
        enableMask, pcieLinkEnableMaskHexDigits(enableMask, supportedMask));
}

std::string renderSupported(uint64_t enableMask, uint64_t supportedMask)
{
    return formatPCIeLinkEnableMask(
        supportedMask, pcieLinkEnableMaskHexDigits(enableMask, supportedMask));
}

TEST(PCIeLinkEnableMask, widthFollowsSupportedMask)
{
    // 48 root ports: both members render at the device-reported width so a
    // client can compare them digit for digit.
    EXPECT_EQ(renderMask(0x0, 0xFFFFFFFFFFFF), "0x000000000000");
    EXPECT_EQ(renderSupported(0x0, 0xFFFFFFFFFFFF), "0xFFFFFFFFFFFF");
    EXPECT_EQ(renderMask(0x100000001001, 0xFFFFFFFFFFFF), "0x100000001001");
}

TEST(PCIeLinkEnableMask, narrowerDeviceMask)
{
    // A device reporting only 8 root ports renders two digits, not twelve.
    EXPECT_EQ(renderMask(0x3, 0xFF), "0x03");
    EXPECT_EQ(renderSupported(0x3, 0xFF), "0xFF");
}

TEST(PCIeLinkEnableMask, zeroMaskStillRendersOneDigit)
{
    // bit_width(0) is 0; the width floor keeps intToHexString from
    // underflowing its digit index.
    EXPECT_EQ(renderMask(0x0, 0x0), "0x0");
    EXPECT_EQ(renderSupported(0x0, 0x0), "0x0");
}

TEST(PCIeLinkEnableMask, strayHighBitsAreNotTruncated)
{
    // intToHexString truncates to the digits requested, so a backend value
    // wider than SupportedMask must widen both members rather than lose bits.
    EXPECT_EQ(renderMask(0xABC, 0xF), "0xABC");
    EXPECT_EQ(renderSupported(0xABC, 0xF), "0x00F");
}

TEST(PCIeLinkEnableMask, writableOnlyDuringTheWindow)
{
    constexpr const char* prefix =
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.";
    EXPECT_TRUE(isPCIeLinkEnableMaskWritable(std::string(prefix) + "Enabled"));
    // ENABLED_UPDATEPENDING: a set is in flight but the window is still open.
    EXPECT_TRUE(
        isPCIeLinkEnableMaskWritable(std::string(prefix) + "Deferring"));

    EXPECT_FALSE(
        isPCIeLinkEnableMaskWritable(std::string(prefix) + "Starting"));
    EXPECT_FALSE(
        isPCIeLinkEnableMaskWritable(std::string(prefix) + "Disabled"));
    EXPECT_FALSE(isPCIeLinkEnableMaskWritable(
        std::string(prefix) + "UnavailableOffline"));
    EXPECT_FALSE(isPCIeLinkEnableMaskWritable(std::string(prefix) + "None"));
    EXPECT_FALSE(isPCIeLinkEnableMaskWritable(""));
}

TEST(PCIeLinkEnableMask, windowClosedResponseTellsTheOperatorWhatToDo)
{
    // A PATCH refused because the window is shut is the outcome an operator
    // hits most, and the value they sent was fine. Pin the whole payload:
    // the 503 and Retry-After are what a client retries on, and the
    // resolution is the only place the window is explained.
    crow::Response res;
    messages::serviceTemporarilyUnavailableMsg(
        res, pcieLinkEnableMaskRetryAfter,
        pcieLinkEnableMaskWindowClosedResolution);

    EXPECT_EQ(res.result(), boost::beast::http::status::service_unavailable);
    EXPECT_EQ(res.getHeaderValue(boost::beast::http::field::retry_after), "60");

    const nlohmann::json& err = res.jsonValue["error"];
    EXPECT_EQ(err["code"], "Base.1.19.ServiceTemporarilyUnavailable");
    const nlohmann::json& info = err["@Message.ExtendedInfo"][0];
    EXPECT_EQ(info["MessageId"], "Base.1.19.ServiceTemporarilyUnavailable");
    EXPECT_EQ(info["Message"],
              "The service is temporarily unavailable.  Retry in 60 seconds.");
    EXPECT_EQ(info["MessageArgs"], nlohmann::json::array({"60"}));
    EXPECT_EQ(info["MessageSeverity"], "Critical");
    EXPECT_EQ(info["Resolution"], pcieLinkEnableMaskWindowClosedResolution);
}

TEST(PCIeLinkEnableMask, unavailableResponseSeparatesTerminusLossFromTheWindow)
{
    // Same 503 and same Retry-After, different cause: the resolution must
    // not tell the operator to wait for a window when the effecter simply
    // did not answer.
    crow::Response res;
    messages::serviceTemporarilyUnavailableMsg(
        res, pcieLinkEnableMaskRetryAfter,
        pcieLinkEnableMaskUnavailableResolution);

    EXPECT_EQ(res.result(), boost::beast::http::status::service_unavailable);
    EXPECT_EQ(res.getHeaderValue(boost::beast::http::field::retry_after), "60");
    EXPECT_EQ(res.jsonValue["error"]["@Message.ExtendedInfo"][0]["Resolution"],
              pcieLinkEnableMaskUnavailableResolution);
}

} // namespace
} // namespace redfish::nvidia_processor_utils
