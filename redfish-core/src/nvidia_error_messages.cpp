/****************************************************************
 *                 READ THIS WARNING FIRST
 * This is an auto-generated header which contains definitions
 * for Redfish DMTF defined messages.
 * DO NOT modify this registry outside of running the
 * parse_registries.py script.  The definitions contained within
 * this file are owned by DMTF.  Any modifications to these files
 * should be first pushed to the relevant registry in the DMTF
 * github organization.
 ***************************************************************/
#include "nvidia_error_messages.hpp"

#include "error_message_utils.hpp"
#include "http_response.hpp"
#include "update_messages.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace redfish::messages
{

nlohmann::json resourceErrorsDetectedFormatError(const std::string& arg1,
                                                 const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_1_1.Message"},
        {"MessageId", "ResourceEvent.1.0.3.ResourceErrorsDetected"},
        {"Message", "The resource property " + arg1 +
                        " has detected errors of type '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"MessageSeverity", "Warning"},
        {"Resolution", "Resolution dependent upon error type."}};
}

void resourceErrorsDetectedFormatError(
    crow::Response& res, const std::string& arg1, const std::string& arg2,
    const std::string& resolution)
{
    res.result(boost::beast::http::status::internal_server_error);
    nlohmann::json responseMessage =
        resourceErrorsDetectedFormatError(arg1, arg2);
    if (!resolution.empty())
    {
        responseMessage["Resolution"] = resolution;
    }
    addMessageToErrorJson(res.jsonValue, responseMessage);
}

nlohmann::json asyncCommandError(const std::string& errorCode,
                                 const std::string& resolution)
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_1_1.Message"},
        {"MessageId", "OpenBMC.0.4.1.AsyncError"},
        {"Message", "Async command failed with rc:" + errorCode},
        {"MessageArgs", {errorCode}},
        {"MessageSeverity", "Warning"},
        {"Resolution", resolution}};
}

void asyncError(crow::Response& res, const std::string& errorCode,
                const std::string& resolution)
{
    res.result(boost::beast::http::status::internal_server_error);
    addMessageToErrorJson(res.jsonValue,
                          asyncCommandError(errorCode, resolution));
}

void updateInProgressMsg(crow::Response& res, const std::string& resolution)
{
    res.result(boost::beast::http::status::bad_request);
    auto message = redfish::messages::updateInProgress();
    if (!resolution.empty())
    {
        message["Resolution"] = resolution;
    }
    addMessageToErrorJson(res.jsonValue, message);
}

void success(crow::Response& res, const std::string& resolution)
{
    nlohmann::json responseMessage = nlohmann::json{
        {"@odata.type", "#Message.v1_1_1.Message"},
        {"MessageId", "Base.1.12.0.Success"},
        {"Message", "Successfully Completed Request"},
        {"MessageSeverity", "OK"},
        {"Resolution", resolution.empty() ? "None" : resolution}};
    addMessageToJsonRoot(res.jsonValue, responseMessage);
}

nlohmann::json invalidUpload(std::string_view arg1, std::string_view arg2)
{
    std::string msg = "Invalid file uploaded to ";
    msg += arg1;
    msg += ": ";
    msg += arg2;
    msg += ".";
    return nlohmann::json{
        {"@odata.type", "/redfish/v1/$metadata#Message.v1_1_1.Message"},
        {"MessageId", "OpenBMC.0.2.InvalidUpload"},
        {"Message", std::move(msg)},
        {"MessageArgs", {arg1, arg2}},
        {"MessageSeverity", "Warning"},
        {"Resolution", "None."}};
}

void invalidUpload(crow::Response& res, std::string_view arg1,
                   std::string_view arg2)
{
    res.result(boost::beast::http::status::bad_request);
    addMessageToErrorJson(res.jsonValue, invalidUpload(arg1, arg2));
}

nlohmann::json resourceCannotBeDeleted(const std::string& arg1,
                                       const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_1_1.Message"},
        {"MessageId", "Base.1.8.1.ResourceCannotBeDeleted"},
        {"Message", "The requested resource of type " + arg1 + " named " +
                        arg2 + " cannot be deleted."},
        {"MessageArgs", {arg1, arg2}},
        {"MessageSeverity", "Critical"},
        {"Resolution", "Do not attempt to delete a non-deletable resource."}};
}

void resourceCannotBeDeleted(crow::Response& res, const std::string& arg1,
                             const std::string& arg2)
{
    res.result(boost::beast::http::status::forbidden);
    addMessageToErrorJson(res.jsonValue, resourceCannotBeDeleted(arg1, arg2));
}

nlohmann::json operationNotAllowed(std::string_view arg)
{
    std::string msg = "Operation is not allowed on this resource, ";
    msg += arg;
    return nlohmann::json{
        {"@odata.type", "/redfish/v1/$metadata#Message.v1_1_1.Message"},
        {"MessageId", "OpenBMC.0.2.InvalidUpload"},
        {"Message", "Base.1.15.0.OperationNotAllowed"},
        {"MessageArgs", {arg}},
        {"MessageSeverity", "Critical"},
        {"Resolution", "None."}};
}

void operationNotAllowed(crow::Response& res, std::string_view arg)
{
    res.result(boost::beast::http::status::method_not_allowed);
    addMessageToErrorJson(res.jsonValue, operationNotAllowed(arg));
}

/**
 * @internal
 * @brief Formats UnsupportedMediaType message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json unsupportedMediaType()
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_1_1.Message"},
        {"MessageId", "Base.1.15.0.UnsupportedMediaType"},
        {"Message",
         "The request specifies a Content-Type for the body that is not supported"},
        {"MessageArgs", {}},
        {"MessageSeverity", "Critical"},
        {"Resolution",
         "Please ensure that the Content-Type header in your request specifies a valid media type for the body content."}};
}

void unsupportedMediaType(crow::Response& res)
{
    res.result(boost::beast::http::status::unsupported_media_type);
    addMessageToErrorJson(res.jsonValue, unsupportedMediaType());
}

} // namespace redfish::messages
