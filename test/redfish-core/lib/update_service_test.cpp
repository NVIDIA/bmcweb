// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "update_service.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(UpdateService, ParseHTTSPPostitive)
{
    crow::Response res;
    {
        // No protocol, schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", std::nullopt, res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Protocol, no schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
}

TEST(UpdateService, ParseHTTPSPostitive)
{
    crow::Response res;
    {
        // No protocol, schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", std::nullopt, res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Protocol, no schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url with path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url without path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url without path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://[2001:db8::1]", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "[2001:db8::1]");
        EXPECT_EQ(ret->encoded_path(), "/");
        EXPECT_EQ(ret->scheme(), "https");
    }
}

TEST(UpdateService, ParseHTTPSNegative)
{
    crow::Response res;
    // No protocol, no schema
    ASSERT_EQ(parseSimpleUpdateUrl("1.1.1.1/path", std::nullopt, res),
              std::nullopt);
    // No host
    ASSERT_EQ(parseSimpleUpdateUrl("/path", "HTTPS", res), std::nullopt);
}

// A Software.Version object that omits the deprecated Purpose property must
// still succeed: the handler must not fail the resource and must skip the
// Purpose-derived Description.
TEST(UpdateService, MissingPurposeIsToleratedAndSkipsDescription)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap props = {
        {"Version", std::string("anyVersion")}};
    getSoftwareVersionCallback(asyncResp, "swId", {}, props);

    EXPECT_NE(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("Description"));
}

// When Purpose is present, Description is the trailing token of the
// VersionPurpose enum string plus " image"; exercise that transform.
TEST(UpdateService, PurposeIsTranslatedToDescription)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap props = {
        {"Version", std::string("anyVersion")},
        {"Purpose", std::string("xyz.openbmc_project.Software.Version."
                                "VersionPurpose.BMC")}};
    getSoftwareVersionCallback(asyncResp, "swId", {}, props);

    EXPECT_NE(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_EQ(asyncResp->res.jsonValue["Description"], "BMC image");
}

// Version is mandatory; its absence is an internal error.
TEST(UpdateService, MissingVersionIsError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    getSoftwareVersionCallback(asyncResp, "swId", {},
                               dbus::utility::DBusPropertiesMap{});

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

// Nvidia code starts here

std::optional<MultiPartUpdate::UpdateParameters> parseParams(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view content)
{
    return processUpdateParameters(asyncResp, content);
}

// Property type errors are annotated on the offending property
// ("<property>@Message.ExtendedInfo"); unknown-property errors land in the
// top-level error object. Return the first MessageId from either place.
nlohmann::json firstMessageId(crow::Response& res)
{
    for (const auto& [key, value] : res.jsonValue.items())
    {
        if (key.ends_with("@Message.ExtendedInfo"))
        {
            return value[0]["MessageId"];
        }
    }
    return res.jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"];
}

TEST(ProcessUpdateParameters, PreUpdateValidationTrueParses)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp,
        R"({"Targets":[],"ForceUpdate":false,"Oem":{"Nvidia":{"PreUpdateValidation":true}}})");
    ASSERT_TRUE(ret);
    if (!ret)
    {
        return;
    }
    EXPECT_EQ(ret->preUpdateValidation, true);
}

TEST(ProcessUpdateParameters, PreUpdateValidationFalseParses)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp, R"({"Oem":{"Nvidia":{"PreUpdateValidation":false}}})");
    ASSERT_TRUE(ret);
    if (!ret)
    {
        return;
    }
    EXPECT_EQ(ret->preUpdateValidation, false);
}

TEST(ProcessUpdateParameters, PreUpdateValidationAbsentLeavesOptionUnset)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Targets":[],"ForceUpdate":true})");
    ASSERT_TRUE(ret);
    if (!ret)
    {
        return;
    }
    EXPECT_EQ(ret->preUpdateValidation, std::nullopt);
}

TEST(ProcessUpdateParameters, NonBooleanPreUpdateValidationRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp, R"({"Oem":{"Nvidia":{"PreUpdateValidation":"yes"}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, IntegerPreUpdateValidationRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp, R"({"Oem":{"Nvidia":{"PreUpdateValidation":5}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, NonObjectOemRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":true})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, NonObjectNvidiaRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":{"Nvidia":"invalid"}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, UnrelatedOemVendorRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":{"OtherVendor":{"Flag":true}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res), "Base.1.19.PropertyUnknown");
}

TEST(ProcessUpdateParameters, UnknownNvidiaOemKeyRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":{"Nvidia":{"Flag":true}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res), "Base.1.19.PropertyUnknown");
}

TEST(MergeUpdateParameters, PreUpdateValidationCarriedIntoMultipartParams)
{
    MultiPartUpdate::UpdateParameters dest;
    MultiPartUpdate::UpdateParameters src;
    src.preUpdateValidation = true;
    mergeUpdateParameters(dest, src);
    EXPECT_EQ(dest.preUpdateValidation, true);

    // An UpdateParameters part that omits the option must not clear it.
    mergeUpdateParameters(dest, MultiPartUpdate::UpdateParameters{});
    EXPECT_EQ(dest.preUpdateValidation, true);
}
// Nvidia code ends here
} // namespace
} // namespace redfish
