#pragma once
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
// These generated headers are a superset of what is needed.
// clang sees them as an error, so ignore
// NOLINTBEGIN(misc-include-cleaner)
#include "http_response.hpp"

#include <boost/url/url_view_base.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
// NOLINTEND(misc-include-cleaner)

namespace redfish
{

namespace messages
{

/**
 * @brief Formats ResourceErrorsDetected message into JSON
 * Message body: "The resource property <arg1> has detected errors of type
 * '<arg2>'."
 *
 * @param[in] arg1 Parameter of message that will replace <arg1> in its body.
 * @param[in] arg2 Parameter of message that will replace <arg2> in its body.
 *
 * @returns Message ResourceErrorsDetected formatted to JSON */
nlohmann::json resourceErrorsDetectedFormatError(const std::string& arg1,
                                                 const std::string& arg2);

void resourceErrorsDetectedFormatError(
    crow::Response& res, const std::string& arg1, const std::string& arg2,
    const std::string& resolution = {});

/**
 * @brief Formats AsyncCommands Error message into JSON
 * Message body: "Async Comand failed with error rc <errCode> "
 *
 * @param[in] arg1 Parameter of message that will replace %1 in its body.
 *
 * @returns Message actionParameterValueError formatted to JSON */

nlohmann::json asyncCommandError(const std::string& errorCode,
                                 const std::string& resolution);
void asyncError(crow::Response& res, const std::string& errorCode,
                const std::string& resolution);

/**
 * @brief message registry entry for update in progress
 *
 * @param res[in] - response
 * @param resolution[in] - if empty default resolution will be used
 */
void updateInProgressMsg(crow::Response& res, const std::string& resolution);

/**
 * @brief NvidiaUpdate.MalformedMultipartRequest, sets 400 Bad Request.
 *
 * @param res[in] - response
 * @param arg1[in] - why the multipart body could not be parsed
 * @param resolution[in] - if empty the registry resolution is kept
 */
void malformedMultipartRequest(crow::Response& res, std::string_view arg1,
                               const std::string& resolution = {});

/**
 * @brief NvidiaUpdate.FirmwarePackageSizeExceeded, sets 413 Payload Too Large.
 *
 * @param res[in] - response
 * @param arg1[in] - uploaded firmware package name
 * @param arg2[in] - uploaded size, human readable
 * @param arg3[in] - maximum supported size, human readable
 */
void firmwarePackageSizeExceeded(crow::Response& res, std::string_view arg1,
                                 std::string_view arg2, std::string_view arg3);

/**
 * @brief NvidiaUpdate.FirmwarePackageStagingError, sets 507 Insufficient
 * Storage.
 *
 * @param res[in] - response
 * @param arg1[in] - uploaded firmware package name
 * @param arg2[in] - size staged so far, human readable
 */
void firmwarePackageStagingError(crow::Response& res, std::string_view arg1,
                                 std::string_view arg2);

/**
 * @brief NvidiaUpdate.FirmwareUpdateInProgress, sets 409 Conflict.
 *
 * @param res[in] - response
 * @param arg1[in] - URI of the running firmware update task
 */
void firmwareUpdateInProgress(crow::Response& res, std::string_view arg1);

/**
 * @brief NvidiaUpdate.FirmwareUpdateTargetInvalid, sets 400 Bad Request.
 *
 * @param res[in] - response
 * @param arg1[in] - the rejected Targets entry
 */
void firmwareUpdateTargetInvalid(crow::Response& res, std::string_view arg1);

/**
 * @brief Base.ServiceTemporarilyUnavailable with a caller-supplied resolution.
 *
 * The registry resolution ("Wait for the indicated retry duration and retry the
 * operation.") says nothing about why the service refused the request, which
 * leaves the operator guessing on properties that are writable only inside a
 * device-driven window. Sets the same 503 and Retry-After header as
 * messages::serviceTemporarilyUnavailable.
 *
 * @param res[in] - response
 * @param arg1[in] - retry duration in seconds, also sent as Retry-After
 * @param resolution[in] - if empty the registry resolution is kept
 */
void serviceTemporarilyUnavailableMsg(
    crow::Response& res, std::string_view arg1, const std::string& resolution);

void success(crow::Response& res, const std::string& resolution);

/**
 * @brief Formats InvalidUpload message into JSON
 * Message body: Invalid file uploaded to %1: %2.*
 * @param[in] arg1 Parameter of message that will replace %1 in its body.
 * @param[in] arg2 Parameter of message that will replace %2 in its body.
 *
 * @returns Message InvalidUpload formatted to JSON */
nlohmann::json invalidUpload(std::string_view arg1, std::string_view arg2);

void invalidUpload(crow::Response& res, std::string_view arg1,
                   std::string_view arg2);

/**
 * @brief Formats ResourceCannotBeDeleted message into JSON
 * Message body: "The requested resource of type <arg1> named <arg2> cannot be
 * deleted."
 *
 * @param[in] arg1 Parameter of message that will replace %1 in its body.
 * @param[in] arg2 Parameter of message that will replace %2 in its body.
 *
 * @returns Message ResourceCannotBeDeleted formatted to JSON */
nlohmann::json resourceCannotBeDeleted(const std::string& arg1,
                                       const std::string& arg2);

void resourceCannotBeDeleted(crow::Response& res, const std::string& arg1,
                             const std::string& arg2);

/**
 * @brief Formats OperationNotAllowed message into JSON
 * Message body: "The HTTP method is not allowed on this resource."
 * @param[in] arg - argument
 * @returns Message OperationNotAllowed formatted to JSON */
nlohmann::json operationNotAllowed(std::string_view arg);

void operationNotAllowed(crow::Response& res, std::string_view arg);

/**
 * @brief Formats UnsupportedMediaType message into JSON
 * Message body: "The request specifies a Content-Type for the body that is not
 * supported"
 * @returns Message UnsupportedMediaType formatted to JSON */
nlohmann::json unsupportedMediaType();

void unsupportedMediaType(crow::Response& res);

} // namespace messages
} // namespace redfish
