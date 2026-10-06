// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "utils/dbus_utils.hpp"

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/linux_error.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message.hpp>

#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish::details
{
namespace
{

TEST(DbusUtils, ObjectMapperNotFoundOnlyForExplicitNotFound)
{
    using boost::system::errc::make_error_code;
    const boost::system::error_code ebadr(
        boost::system::linux_error::bad_request_descriptor,
        boost::system::system_category());

    EXPECT_TRUE(isObjectMapperNotFound(ebadr));
    EXPECT_FALSE(isObjectMapperNotFound(boost::system::error_code()));
    EXPECT_FALSE(
        isObjectMapperNotFound(make_error_code(boost::system::errc::io_error)));
    EXPECT_FALSE(isObjectMapperNotFound(
        make_error_code(boost::system::errc::host_unreachable)));
}

TEST(DbusUtils, ResourceNotFoundNameBecomesNotFound)
{
    using boost::system::errc::make_error_code;
    // sd-bus delivers an unknown error name as EIO
    const auto eio = make_error_code(boost::system::errc::io_error);

    EXPECT_TRUE(isObjectMapperNotFound(dbus::utility::mapperNotFoundToEbadr(
        eio, "xyz.openbmc_project.Common.Error.ResourceNotFound")));
}

TEST(DbusUtils, OtherErrorNamesStayFailures)
{
    using boost::system::errc::make_error_code;
    const auto eio = make_error_code(boost::system::errc::io_error);
    const auto unreachable =
        make_error_code(boost::system::errc::host_unreachable);

    // InternalFailure also arrives as EIO
    EXPECT_FALSE(isObjectMapperNotFound(dbus::utility::mapperNotFoundToEbadr(
        eio, "xyz.openbmc_project.Common.Error.InternalFailure")));
    EXPECT_FALSE(
        isObjectMapperNotFound(dbus::utility::mapperNotFoundToEbadr(eio, "")));
    // ServiceUnknown arrives as EHOSTUNREACH
    EXPECT_FALSE(isObjectMapperNotFound(dbus::utility::mapperNotFoundToEbadr(
        unreachable, "org.freedesktop.DBus.Error.ServiceUnknown")));
}

TEST(DbusUtils, OtherErrorsAreReturnedUnchanged)
{
    using boost::system::errc::make_error_code;
    const auto eio = make_error_code(boost::system::errc::io_error);
    const auto unreachable =
        make_error_code(boost::system::errc::host_unreachable);

    // The caller still sees the original code, not a rewritten one.
    EXPECT_EQ(dbus::utility::mapperNotFoundToEbadr(
                  eio, "xyz.openbmc_project.Common.Error.InternalFailure"),
              eio);
    EXPECT_EQ(dbus::utility::mapperNotFoundToEbadr(
                  unreachable, "org.freedesktop.DBus.Error.ServiceUnknown"),
              unreachable);
}

TEST(DbusUtils, NoErrorIsNeverNotFound)
{
    EXPECT_FALSE(dbus::utility::mapperNotFoundToEbadr(
        boost::system::error_code(),
        "xyz.openbmc_project.Common.Error.ResourceNotFound"));
}

TEST(DbusUtils, AfterPropertySetSuccess)
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();

    boost::system::error_code ec;
    sdbusplus::message_t msg;
    afterSetProperty(asyncResp, "MyRedfishProperty",
                     nlohmann::json("MyRedfishValue"), ec, msg);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::no_content);
}

TEST(DbusUtils, AfterActionPropertySetSuccess)
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();

    boost::system::error_code ec;
    sdbusplus::message_t msg;
    afterSetPropertyAction(asyncResp, "MyRedfishProperty", "MyRedfishValue", ec,
                           msg);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(asyncResp->res.jsonValue,
              R"({
                    "@Message.ExtendedInfo": [
                        {
                            "@odata.type": "#Message.v1_1_1.Message",
                            "Message": "The request completed successfully.",
                            "MessageArgs": [],
                            "MessageId": "Base.1.19.Success",
                            "MessageSeverity": "OK",
                            "Resolution": "None."
                        }
                    ]
                })"_json);
}

TEST(DbusUtils, AfterPropertySetInternalError)
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();

    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::timed_out);
    sdbusplus::message_t msg;
    afterSetProperty(asyncResp, "MyRedfishProperty",
                     nlohmann::json("MyRedfishValue"), ec, msg);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_EQ(asyncResp->res.jsonValue.size(), 1);
    using nlohmann::literals::operator""_json;

    EXPECT_EQ(asyncResp->res.jsonValue,
              R"({
                    "error": {
                    "@Message.ExtendedInfo": [
                        {
                        "@odata.type": "#Message.v1_1_1.Message",
                        "Message": "The request failed due to an internal service error.  The service is still operational.",
                        "MessageArgs": [],
                        "MessageId": "Base.1.19.InternalError",
                        "MessageSeverity": "Critical",
                        "Resolution": "Resubmit the request.  If the problem persists, consider resetting the service."
                        }
                    ],
                    "code": "Base.1.19.InternalError",
                    "message": "The request failed due to an internal service error.  The service is still operational."
                    }
                })"_json);
}

} // namespace
} // namespace redfish::details
