/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "query.hpp"
#include "registries.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utility.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/mctp_utils.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/url/format.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace redfish
{
namespace nvidia
{

constexpr size_t p384ValueHexLength = 192;

constexpr std::string_view factoryResetService =
    "xyz.openbmc_project.FactoryResetManager";
constexpr std::string_view factoryResetObject =
    "/xyz/openbmc_project/control/factory_reset";
constexpr std::string_view attestedWipeInterface = "com.nvidia.BF.AttestedWipe";
constexpr std::string_view attestedWipeKeyInterface =
    "com.nvidia.BF.AttestedWipeKey";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> loggingMatch = nullptr;

struct AttestedWipeSupport
{
    // com.nvidia.BF.AttestedWipeKey: GetWipeNonce and EnrollWipeKey.
    bool key = false;
    // com.nvidia.BF.AttestedWipe: AttestedWipe and RetrieveFlashLog.
    bool wipe = false;
};

/**
 * @brief Reads which attested-wipe interfaces FactoryResetManager provides.
 *
 * @param object The mapper GetObject reply for the factory-reset object.
 *
 * @return The interfaces implemented by FactoryResetManager. Interfaces from
 * any other service are ignored, because the actions call that service.
 */
inline AttestedWipeSupport parseAttestedWipeSupport(
    const dbus::utility::MapperGetObject& object)
{
    AttestedWipeSupport support;
    for (const auto& [service, interfaces] : object)
    {
        if (service != factoryResetService)
        {
            continue;
        }
        support.key = support.key ||
                      std::ranges::find(interfaces, attestedWipeKeyInterface) !=
                          interfaces.end();
        support.wipe = support.wipe ||
                       std::ranges::find(interfaces, attestedWipeInterface) !=
                           interfaces.end();
    }
    return support;
}

/**
 * @brief Looks up the attested-wipe interfaces on the factory-reset object.
 *
 * A mapper error, including ResourceNotFound when neither interface is
 * registered, reports no support.
 *
 * @param callback Called with the interfaces that are present.
 *
 * @return none.
 */
inline void getAttestedWipeSupport(
    std::function<void(const AttestedWipeSupport&)>&& callback)
{
    constexpr std::array<std::string_view, 2> interfaces = {
        attestedWipeKeyInterface, attestedWipeInterface};
    dbus::utility::getDbusObject(
        std::string(factoryResetObject), interfaces,
        [callback = std::move(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("No attested-wipe interface on {}: {}",
                                 factoryResetObject, ec.message());
                callback(AttestedWipeSupport{});
                return;
            }
            callback(parseAttestedWipeSupport(object));
        });
}

/**
 * @brief Maps a FactoryResetManager D-Bus error onto a Redfish response.
 *
 * @param response The Redfish response to populate.
 * @param methodError The D-Bus method error from the call.
 * @param actionName The OEM action name used in the Redfish error payload.
 *
 * @return none.
 */
inline void mapAttestedWipe(crow::Response& response,
                            sdbusplus::message_t& methodError,
                            std::string_view actionName)
{
    const sd_bus_error* dbusError = methodError.get_error();
    if (dbusError == nullptr || dbusError->name == nullptr)
    {
        messages::internalError(response);
        return;
    }

    const std::string_view errorName(dbusError->name);
    if (errorName == "xyz.openbmc_project.Common.Error.Unavailable")
    {
        messages::serviceTemporarilyUnavailable(response, "60");
        return;
    }
    if (errorName == "xyz.openbmc_project.Common.Error.NotAllowed")
    {
        messages::operationNotAllowed(response);
        return;
    }
    if (errorName == "xyz.openbmc_project.Common.Error.UnsupportedRequest")
    {
        messages::actionNotSupported(response, actionName);
        return;
    }
    if (errorName == "xyz.openbmc_project.Common.Error.InvalidArgument")
    {
        messages::actionParameterValueError(response, "AttestedWipe",
                                            actionName);
        return;
    }
    if (errorName == "xyz.openbmc_project.Common.Error.ResourceNotFound" &&
        actionName == "RetrieveFlashLog")
    {
        messages::resourceNotFound(response, "FlashLog", "RetrieveFlashLog");
        return;
    }
    if (errorName == "xyz.openbmc_project.Common.Error.InternalFailure")
    {
        messages::internalError(response);
        return;
    }
    messages::operationFailed(response);
}

/**
 * @brief Checks whether a value is a hexadecimal string of a fixed length.
 *
 * @param value The candidate hexadecimal string.
 * @param expectedLength The number of hexadecimal characters required.
 *
 * @return true if the value has the expected length and contains only
 * hexadecimal digits; otherwise false.
 */
inline bool isValidHexValue(std::string_view value, size_t expectedLength)
{
    return value.size() == expectedLength &&
           std::ranges::all_of(value, [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

/**
 * @brief Checks whether an action request body is empty or an empty JSON
 * object.
 *
 * @param body The raw request body. Whitespace is ignored.
 *
 * @return true if the body is empty or "{}"; otherwise false.
 */
inline bool isEmptyActionBodyText(std::string body)
{
    std::erase_if(body, [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    return body.empty() || body == "{}";
}

/**
 * @brief Checks whether a Redfish action request has no parameters.
 *
 * @param request The incoming Redfish request.
 *
 * @return true if the body is empty or "{}"; otherwise false.
 */
inline bool isEmptyActionBody(const crow::Request& request)
{
    return isEmptyActionBodyText(request.body());
}

/**
 * @brief Checks whether a ConfigData value is a non-empty Base64 string.
 *
 * @param configData The ConfigData value from the AttestedWipe request.
 *
 * @return true if the value is non-empty and decodes as Base64; otherwise
 * false.
 */
inline bool isValidConfigData(std::string_view configData)
{
    std::string decoded;
    return !configData.empty() &&
           crow::utility::base64Decode(configData, decoded) && !decoded.empty();
}

/**
 * @brief Populates Links.ComponentIntegrity from SPDM Responder objects.
 *
 * Uses the same mapper GetSubTree and Object.Enable filter as
 * /redfish/v1/ComponentIntegrity.
 *
 * @param asyncResp Pointer to the object holding response data.
 *
 * @return none.
 */
inline void populateAttestedWipeComponentIntegrityLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr const char* spdmRoot = "/xyz/openbmc_project/SPDM";
    const std::array<const char*, 1> responderInterface = {
        mctp_utils::spdmResponderIntf};

    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::GetSubTreeType& subtree) {
            nlohmann::json& links =
                asyncResp->res.jsonValue["Links"]["ComponentIntegrity"];
            links = nlohmann::json::array();

            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to enumerate SPDM objects for ComponentIntegrity links: {}",
                    ec.message());
                asyncResp->res
                    .jsonValue["Links"]["ComponentIntegrity@odata.count"] = 0;
                return;
            }
            if (subtree.empty())
            {
                asyncResp->res
                    .jsonValue["Links"]["ComponentIntegrity@odata.count"] = 0;
                return;
            }

            for (const auto& object : subtree)
            {
                const std::string objPathString = object.first;
                if (object.second.empty())
                {
                    continue;
                }

                dbus::utility::getProperty<bool>(
                    object.second[0].first, objPathString,
                    "xyz.openbmc_project.Object.Enable", "Enabled",
                    [asyncResp,
                     objPathString](const boost::system::error_code& ec2,
                                    const bool& enabled) {
                        if (ec2)
                        {
                            BMCWEB_LOG_ERROR(
                                "Failed to read Enabled for SPDM object {}: {}",
                                objPathString, ec2.message());
                            return;
                        }
                        if (!enabled)
                        {
                            return;
                        }
                        const std::string componentId =
                            std::filesystem::path(objPathString)
                                .filename()
                                .string();
                        nlohmann::json& componentIntegrityLinks =
                            asyncResp->res
                                .jsonValue["Links"]["ComponentIntegrity"];
                        componentIntegrityLinks.push_back(
                            {{"@odata.id", "/redfish/v1/ComponentIntegrity/" +
                                               componentId}});
                        asyncResp->res
                            .jsonValue["Links"]
                                      ["ComponentIntegrity@odata.count"] =
                            componentIntegrityLinks.size();
                    });
            }
        },
        dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
        dbus_utils::mapperIntf, "GetSubTree", spdmRoot, 0, responderInterface);
}

/**
 * @brief Fills the Nvidia AttestedWipe OEM resource for the interfaces that
 * are present.
 *
 * Does not add Links.ComponentIntegrity entries; the caller reads them from
 * D-Bus when support.wipe is set.
 *
 * @param response The Redfish response to populate.
 * @param managerId The Manager URI segment from the request path.
 * @param support The attested-wipe interfaces on the factory-reset object.
 *
 * @return none.
 */
inline void populateAttestedWipe(crow::Response& response,
                                 const std::string& managerId,
                                 const AttestedWipeSupport& support)
{
    if (!support.key && !support.wipe)
    {
        messages::resourceNotFound(response, "NvidiaAttestedWipe",
                                   "AttestedWipe");
        return;
    }

    response.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/AttestedWipe", managerId);
    response.jsonValue["@odata.type"] =
        "#NvidiaAttestedWipe.v1_0_0.NvidiaAttestedWipe";
    response.jsonValue["Id"] = "AttestedWipe";
    response.jsonValue["Name"] = "Attested Wipe";

    nlohmann::json& actions = response.jsonValue["Actions"];
    actions = nlohmann::json::object();
    if (support.key)
    {
        actions["#NvidiaAttestedWipe.EnrollWipeKey"]["target"] =
            boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/"
                                "AttestedWipe/Actions/"
                                "NvidiaAttestedWipe.EnrollWipeKey",
                                managerId);
        actions["#NvidiaAttestedWipe.GetWipeNonce"]["target"] =
            boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/"
                                "AttestedWipe/Actions/"
                                "NvidiaAttestedWipe.GetWipeNonce",
                                managerId);
    }
    if (support.wipe)
    {
        response.jsonValue["Links"] = {
            {"ComponentIntegrity", nlohmann::json::array()},
            {"ComponentIntegrity@odata.count", 0}};

        actions["#NvidiaAttestedWipe.AttestedWipe"]["target"] =
            boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/"
                                "AttestedWipe/Actions/"
                                "NvidiaAttestedWipe.AttestedWipe",
                                managerId);
        actions["#NvidiaAttestedWipe.AttestedWipe"]["@Redfish.ActionInfo"] =
            boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/"
                                "AttestedWipe/AttestedWipeActionInfo",
                                managerId);
        actions["#NvidiaAttestedWipe.RetrieveFlashLog"]["target"] =
            boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/"
                                "AttestedWipe/Actions/"
                                "NvidiaAttestedWipe.RetrieveFlashLog",
                                managerId);
    }
}

/**
 * @brief Handles GET for the Nvidia AttestedWipe OEM resource.
 *
 * @param app The crow application used to set up the Redfish route.
 * @param request The incoming Redfish request.
 * @param asyncResp Pointer to the object holding response data.
 * @param managerId The Manager URI segment from the request path.
 *
 * @return none.
 */
inline void handleAttestedWipeGet(
    App& app, const crow::Request& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!setUpRedfishRoute(app, request, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    getAttestedWipeSupport(
        [asyncResp, managerId](const AttestedWipeSupport& support) {
            populateAttestedWipe(asyncResp->res, managerId, support);
            if (support.wipe)
            {
                populateAttestedWipeComponentIntegrityLinks(asyncResp);
            }
        });
}

/**
 * @brief Fills AttestedWipe ActionInfo when com.nvidia.BF.AttestedWipe is
 * present.
 *
 * @param response The Redfish response to populate.
 * @param managerId The Manager URI segment from the request path.
 * @param support The attested-wipe interfaces on the factory-reset object.
 *
 * @return none.
 */
inline void populateAttestedWipeActionInfo(crow::Response& response,
                                           const std::string& managerId,
                                           const AttestedWipeSupport& support)
{
    if (!support.wipe)
    {
        messages::resourceNotFound(response, "ActionInfo",
                                   "AttestedWipeActionInfo");
        return;
    }

    response.jsonValue = {
        {"@odata.id",
         boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/AttestedWipe/"
                             "AttestedWipeActionInfo",
                             managerId)},
        {"@odata.type",
         "#NvidiaAttestedWipeActionInfo.v1_0_0.NvidiaAttestedWipeActionInfo"},
        {"Id", "AttestedWipeActionInfo"},
        {"Name", "AttestedWipe Action Info"},
        {"Parameters",
         {{{"Name", "RotSignature"},
           {"Required", true},
           {"DataType", "String"}},
          {{"Name", "ConfigDataSignature"},
           {"Required", true},
           {"DataType", "String"}},
          {{"Name", "ConfigData"},
           {"Required", true},
           {"DataType", "String"}}}}};
}

/**
 * @brief Handles GET for AttestedWipe ActionInfo.
 *
 * @param app The crow application used to set up the Redfish route.
 * @param request The incoming Redfish request.
 * @param asyncResp Pointer to the object holding response data.
 * @param managerId The Manager URI segment from the request path.
 *
 * @return none.
 */
inline void handleAttestedWipeActionInfo(
    App& app, const crow::Request& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!setUpRedfishRoute(app, request, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    getAttestedWipeSupport(
        [asyncResp, managerId](const AttestedWipeSupport& support) {
            populateAttestedWipeActionInfo(asyncResp->res, managerId, support);
        });
}

/**
 * @brief Handles POST for Oem NvidiaAttestedWipe.GetWipeNonce.
 *
 * @param app The crow application used to set up the Redfish route.
 * @param request The incoming Redfish request.
 * @param asyncResp Pointer to the object holding response data.
 * @param managerId The Manager URI segment from the request path.
 *
 * @return none.
 */
inline void handleGetWipeNonce(
    App& app, const crow::Request& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!setUpRedfishRoute(app, request, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }
    if (!isEmptyActionBody(request))
    {
        BMCWEB_LOG_ERROR("Unrecognized request body");
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }

    getAttestedWipeSupport([asyncResp](const AttestedWipeSupport& support) {
        if (!support.key)
        {
            messages::actionNotSupported(asyncResp->res, "GetWipeNonce");
            return;
        }
        dbus::utility::async_method_call(
            [asyncResp](const boost::system::error_code& ec,
                        sdbusplus::message_t& methodError,
                        const std::vector<uint8_t>& nonce) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("GetWipeNonce D-Bus call failed: {}",
                                     ec.message());
                    mapAttestedWipe(asyncResp->res, methodError,
                                    "GetWipeNonce");
                    return;
                }
                asyncResp->res.result(boost::beast::http::status::ok);
                asyncResp->res.jsonValue["Nonce"] = bytesToHexString(nonce);
            },
            std::string(factoryResetService), std::string(factoryResetObject),
            std::string(attestedWipeKeyInterface), "GetWipeNonce");
    });
}

/**
 * @brief Calls EnrollWipeKey on the factory-reset object.
 *
 * @param asyncResp Pointer to the object holding response data.
 * @param publicKey The 192-character hexadecimal P-384 wipe public key.
 * @param signature The 192-character hexadecimal signature, empty for the
 *                  first enrollment.
 *
 * @return none.
 */
inline void callEnrollWipeKey(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& publicKey, const std::string& signature)
{
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    sdbusplus::message_t& methodError) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("EnrollWipeKey D-Bus call failed: {}",
                                 ec.message());
                mapAttestedWipe(asyncResp->res, methodError, "EnrollWipeKey");
                return;
            }
            asyncResp->res.result(boost::beast::http::status::no_content);
        },
        std::string(factoryResetService), std::string(factoryResetObject),
        std::string(attestedWipeKeyInterface), "EnrollWipeKey", publicKey,
        signature);
}

/**
 * @brief Handles POST for Oem NvidiaAttestedWipe.EnrollWipeKey.
 *
 * @param app The crow application used to set up the Redfish route.
 * @param request The incoming Redfish request.
 * @param asyncResp Pointer to the object holding response data.
 * @param managerId The Manager URI segment from the request path.
 *
 * @return none.
 */
inline void handleEnrollWipeKey(
    App& app, const crow::Request& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!setUpRedfishRoute(app, request, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    std::string publicKey;
    std::optional<std::string> signature;
    if (!json_util::readJsonAction(request, asyncResp->res, "PublicKey",
                                   publicKey, "Signature", signature))
    {
        return;
    }
    if (!isValidHexValue(publicKey, p384ValueHexLength))
    {
        BMCWEB_LOG_ERROR("Invalid public key");
        messages::actionParameterValueError(asyncResp->res, "PublicKey",
                                            "EnrollWipeKey");
        return;
    }
    if (signature && !isValidHexValue(*signature, p384ValueHexLength))
    {
        BMCWEB_LOG_ERROR("Invalid signature");
        messages::actionParameterValueError(asyncResp->res, "Signature",
                                            "EnrollWipeKey");
        return;
    }

    getAttestedWipeSupport(
        [asyncResp, publicKey, signature = signature.value_or("")](
            const AttestedWipeSupport& support) {
            if (!support.key)
            {
                messages::actionNotSupported(asyncResp->res, "EnrollWipeKey");
                return;
            }
            callEnrollWipeKey(asyncResp, publicKey, signature);
        });
}

constexpr std::string_view progressInterface =
    "xyz.openbmc_project.Common.Progress";
constexpr std::string_view progressStatusCompleted =
    "xyz.openbmc_project.Common.Progress.OperationStatus.Completed";
constexpr std::string_view progressStatusFailed =
    "xyz.openbmc_project.Common.Progress.OperationStatus.Failed";
constexpr std::string_view progressStatusAborted =
    "xyz.openbmc_project.Common.Progress.OperationStatus.Aborted";
constexpr std::string_view attestedWipeLogPath =
    "/var/lib/factory-reset-manager/attested-wipe-log.json";
// Bounds the synchronous read and parse on the event loop.
constexpr std::uintmax_t maxFlashLogSize =
    static_cast<std::uintmax_t>(1024U) * 1024U;
constexpr auto attestedWipeWatchdog = std::chrono::minutes(5);

/**
 * @brief Updates an AttestedWipe task from the FactoryResetManager
 * xyz.openbmc_project.Common.Progress interface.
 *
 * Progress 10 is the NIC command. Later progress values are 20
 * (selector blob written) and 30 (ready to trigger the ERoT). Failed or
 * Aborted at any other step ends the task with an exception. The success path
 * reboots the BMC, so Completed is normally not observed.
 *
 * @param ec Set when the task watchdog fires.
 * @param msg The PropertiesChanged signal.
 * @param taskData The Redfish task watching the wipe.
 *
 * @return task::completed when the task should stop; otherwise false.
 */
inline bool attestedWipeProgress(
    boost::system::error_code ec, sdbusplus::message_t& msg,
    const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Attested wipe progress error: {}", ec.message());
        return task::completed;
    }

    std::string iface;
    dbus::utility::DBusPropertiesMap values;
    msg.read(iface, values);
    if (iface != progressInterface)
    {
        return !task::completed;
    }

    const std::string index = std::to_string(taskData->index);
    const uint8_t* progress = nullptr;
    const std::string* status = nullptr;
    for (const auto& [key, value] : values)
    {
        if (key == "Progress")
        {
            progress = std::get_if<uint8_t>(&value);
            if (progress == nullptr)
            {
                BMCWEB_LOG_ERROR("Attested wipe Progress has wrong type");
                taskData->messages.emplace_back(messages::internalError());
                taskData->state = "Exception";
                return task::completed;
            }
        }
        else if (key == "Status")
        {
            status = std::get_if<std::string>(&value);
            if (status == nullptr)
            {
                BMCWEB_LOG_ERROR("Attested wipe Status has wrong type");
                taskData->messages.emplace_back(messages::internalError());
                taskData->state = "Exception";
                return task::completed;
            }
        }
    }

    if (progress != nullptr)
    {
        taskData->percentComplete = static_cast<int>(*progress);
        taskData->messages.emplace_back(messages::taskProgressChanged(
            index, static_cast<size_t>(*progress)));
    }

    if (status == nullptr)
    {
        return !task::completed;
    }

    if (*status == progressStatusFailed || *status == progressStatusAborted)
    {
        BMCWEB_LOG_ERROR("Attested wipe ended at progress {}: {}",
                         taskData->percentComplete, *status);
        taskData->state = "Exception";
        taskData->messages.emplace_back(messages::taskAborted(index));
        return task::completed;
    }
    if (*status == progressStatusCompleted)
    {
        BMCWEB_LOG_INFO("Attested wipe completed");
        taskData->state = "Completed";
        taskData->messages.emplace_back(messages::taskCompletedOK(index));
        return task::completed;
    }

    return !task::completed;
}

/**
 * @brief Stop a task created before AttestedWipe when that call fails.
 *
 * The match is already installed, so a progress signal may have finished the
 * task. Leave that result in place.
 */
inline void stopAttestedWipeTask(const std::shared_ptr<task::TaskData>& task)
{
    task->timer.cancel();
    task->match.reset();
    loggingMatch.reset();
    if (task->endTime)
    {
        return;
    }
    task->state = "Exception";
    task->messages.emplace_back(
        messages::taskAborted(std::to_string(task->index)));
    task->finishTask();
    task::TaskData::sendTaskEvent(task->state, task->index);
}

/**
 * @brief Handles the FactoryResetManager.AttestedWipe D-Bus reply.
 *
 * The Task and its PropertiesChanged match already exist, so progress emitted
 * while this call is in flight is not lost. A D-Bus error is returned on this
 * response and the Task is stopped. Success is 202 with the Task, including
 * when the Task has already reached a terminal state.
 *
 * @param task The Task watching FactoryResetManager progress.
 * @param asyncResp Pointer to the object holding response data.
 * @param ec The error code of the D-Bus call.
 * @param methodError The D-Bus method error from the call.
 *
 * @return none.
 */
inline void handleAttestedWipeResponse(
    const std::shared_ptr<task::TaskData>& task,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, sdbusplus::message_t& methodError)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("AttestedWipe D-Bus call failed: {}", ec.message());
        stopAttestedWipeTask(task);
        mapAttestedWipe(asyncResp->res, methodError, "AttestedWipe");
        return;
    }

    // populateResp reports 204 once the task has an end time. This response
    // is the POST that creates the task, so keep 202 and the task resource
    // even if a progress signal already finished it.
    const std::optional<std::chrono::system_clock::time_point> savedEnd =
        task->endTime;
    if (savedEnd)
    {
        task->endTime.reset();
    }
    task->populateResp(asyncResp->res);
    if (savedEnd)
    {
        task->endTime = savedEnd;
        asyncResp->res.jsonValue["TaskState"] = task->state;
        asyncResp->res.jsonValue["EndTime"] =
            redfish::time_utils::getDateTimeStdtime(*savedEnd);
    }
}

inline void logAttestedWipeFailed(sdbusplus::message_t& m,
                                  nlohmann::json::array_t& messages)
{
    std::vector<std::pair<std::string, dbus::utility::DBusPropertiesMap>>
        interfacesProperties;
    sdbusplus::object_path objPath;
    m.read(objPath, interfacesProperties);
    for (auto interface : interfacesProperties)
    {
        if (interface.first == "xyz.openbmc_project.Logging.Entry")
        {
            std::string rfMessage;
            std::string resolution;
            std::string messageNamespace;
            std::vector<std::string> rfArgs;
            const std::vector<std::string>* entryData = nullptr;
            for (auto& propertyMap : interface.second)
            {
                if (propertyMap.first == "AdditionalData")
                {
                    entryData = std::get_if<std::vector<std::string>>(
                        &propertyMap.second);

                    for (const auto& kv : *entryData)
                    {
                        // Split at the first '=' only, so a value may contain
                        // '='. An entry without one is malformed, skip it.
                        const size_t eq = kv.find('=');
                        if (eq == std::string::npos)
                        {
                            continue;
                        }
                        const std::string key = kv.substr(0, eq);
                        const std::string value = kv.substr(eq + 1);
                        if (key == "REDFISH_MESSAGE_ID")
                        {
                            rfMessage = value;
                        }
                        else if (key == "REDFISH_MESSAGE_ARGS")
                        {
                            boost::split(rfArgs, value, boost::is_any_of(","));
                        }
                        else if (key == "namespace")
                        {
                            messageNamespace = value;
                        }
                    }
                }
                else if (propertyMap.first == "Resolution")
                {
                    const std::string* value =
                        std::get_if<std::string>(&propertyMap.second);
                    if (value != nullptr)
                    {
                        resolution = *value;
                    }
                }
            }
            /* we need to have found the id, data, this image needs to
               correspond to the image we are working with right now and the
               message should be update related */
            if (entryData == nullptr || messageNamespace != "AttestedWipe")
            {
                // something is invalid
                BMCWEB_LOG_DEBUG("Got invalid log message");
            }
            else
            {
                const registries::Message* registryMessage =
                    registries::getMessage(rfMessage);
                if (registryMessage != nullptr)
                {
                    std::vector<std::string_view> argViews(rfArgs.begin(),
                                                           rfArgs.end());
                    nlohmann::json message = {
                        {"@odata.type", "#Message.v1_1_1.Message"},
                        {"MessageId", rfMessage},
                        {"Message", registries::fillMessageArgs(
                                        argViews, registryMessage->message)},
                        {"MessageArgs", rfArgs},
                        {"MessageSeverity", registryMessage->messageSeverity},
                        {"Resolution", registryMessage->resolution}};
                    if (!resolution.empty())
                    {
                        message["Resolution"] = resolution;
                    }
                    messages.emplace_back(message);
                }
                else
                {
                    BMCWEB_LOG_ERROR("Unknown message ID: {}", rfMessage);
                }
            }
        }
    }
}
/**
 * @brief Handles POST for Oem NvidiaAttestedWipe.AttestedWipe.
 *
 * Validates the request body, then calls FactoryResetManager.AttestedWipe.
 * That method owns the pre-reset work. bmcweb returns a Task that follows
 * the Progress property until the five-minute watchdog expires.
 *
 * @param app The crow application used to set up the Redfish route.
 * @param request The incoming Redfish request.
 * @param asyncResp Pointer to the object holding response data.
 * @param managerId The Manager URI segment from the request path.
 *
 * @return none.
 */
inline void handleAttestedWipe(
    App& app, const crow::Request& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!setUpRedfishRoute(app, request, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    std::string rotSignature;
    std::string configDataSignature;
    std::string configData;
    if (!json_util::readJsonAction(
            request, asyncResp->res, "RotSignature", rotSignature,
            "ConfigDataSignature", configDataSignature, "ConfigData",
            configData))
    {
        return;
    }

    if (!isValidHexValue(rotSignature, p384ValueHexLength))
    {
        BMCWEB_LOG_ERROR("Invalid RotSignature");
        messages::actionParameterValueError(asyncResp->res, "RotSignature",
                                            "AttestedWipe");
        return;
    }
    if (!isValidHexValue(configDataSignature, p384ValueHexLength))
    {
        BMCWEB_LOG_ERROR("Invalid ConfigDataSignature");
        messages::actionParameterValueError(
            asyncResp->res, "ConfigDataSignature", "AttestedWipe");
        return;
    }
    if (!isValidConfigData(configData))
    {
        BMCWEB_LOG_ERROR("Invalid ConfigData");
        messages::actionParameterValueError(asyncResp->res, "ConfigData",
                                            "AttestedWipe");
        return;
    }

    getAttestedWipeSupport([asyncResp, rotSignature = std::move(rotSignature),
                            configDataSignature =
                                std::move(configDataSignature),
                            configData = std::move(configData)](
                               const AttestedWipeSupport& support) {
        if (!support.wipe)
        {
            messages::actionNotSupported(asyncResp->res, "AttestedWipe");
            return;
        }
        // Install the progress match before AttestedWipe. A fast failure
        // emits PropertiesChanged before the method reply, and a match
        // created in the reply handler misses it.
        std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
            attestedWipeProgress,
            std::format(
                "type='signal',interface='org.freedesktop.DBus.Properties',"
                "member='PropertiesChanged',path='{}',arg0='{}'",
                factoryResetObject, progressInterface));
        task->startTimer(attestedWipeWatchdog);
        // Remove the request body from the task payload since it include
        // signatures
        loggingMatch = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
            "member='InterfacesAdded',"
            "path='/xyz/openbmc_project/logging'",
            [task](sdbusplus::message_t& msgLog) {
                logAttestedWipeFailed(msgLog, task->messages);
            });
        // Remove the request body from the task payload since it include
        // signatures
        dbus::utility::async_method_call(
            [asyncResp, task](const boost::system::error_code& ec,
                              sdbusplus::message_t& methodError) {
                handleAttestedWipeResponse(task, asyncResp, ec, methodError);
            },
            std::string(factoryResetService), std::string(factoryResetObject),
            std::string(attestedWipeInterface), "AttestedWipe", rotSignature,
            configDataSignature, configData);
    });
}

/**
 * @brief Reads the attested wipe flash log and writes its entries to the
 * response.
 *
 * @param response The Redfish response to populate.
 * @param path The flash-log path reported by RetrieveFlashLog.
 *
 * @return none.
 */
inline void populateFlashLog(crow::Response& response, const std::string& path)
{
    if (path != attestedWipeLogPath)
    {
        BMCWEB_LOG_ERROR("RetrieveFlashLog returned unexpected path: {}", path);
        messages::internalError(response);
        return;
    }

    std::error_code sizeError;
    const std::uintmax_t logSize = std::filesystem::file_size(path, sizeError);
    if (sizeError)
    {
        BMCWEB_LOG_ERROR("Unable to get size of attested wipe log {}: {}", path,
                         sizeError.message());
        messages::internalError(response);
        return;
    }
    if (logSize > maxFlashLogSize)
    {
        BMCWEB_LOG_ERROR("Attested wipe log size {} exceeds limit {}", logSize,
                         maxFlashLogSize);
        messages::internalError(response);
        return;
    }

    std::ifstream logFile(path);
    if (!logFile.good())
    {
        BMCWEB_LOG_ERROR("Unable to open attested wipe log {}", path);
        messages::internalError(response);
        return;
    }
    // This nlohmann build has no stream input adapter, so parse the bytes.
    std::string logText(static_cast<std::size_t>(logSize), '\0');
    logFile.read(logText.data(), static_cast<std::streamsize>(logSize));
    if (!logFile)
    {
        BMCWEB_LOG_ERROR("Unable to read attested wipe log {}", path);
        messages::internalError(response);
        return;
    }
    const nlohmann::json parsed =
        nlohmann::json::parse(logText, nullptr, false);
    if (parsed.is_discarded())
    {
        BMCWEB_LOG_ERROR("Attested wipe log is not JSON");
        messages::internalError(response);
        return;
    }

    if (parsed.is_object() && parsed.contains("FlashLog"))
    {
        response.jsonValue["FlashLog"] = parsed["FlashLog"];
    }
    else if (parsed.is_array())
    {
        response.jsonValue["FlashLog"] = parsed;
    }
    else
    {
        BMCWEB_LOG_ERROR("Attested wipe log has no FlashLog entries");
        messages::internalError(response);
        return;
    }
    response.result(boost::beast::http::status::ok);
}

/**
 * @brief Handles POST for Oem NvidiaAttestedWipe.RetrieveFlashLog.
 *
 * Calls FactoryResetManager.RetrieveFlashLog and returns the flash-log
 * entries from the path that method reports.
 *
 * @param app The crow application used to set up the Redfish route.
 * @param request The incoming Redfish request.
 * @param asyncResp Pointer to the object holding response data.
 * @param managerId The Manager URI segment from the request path.
 *
 * @return none.
 */
inline void handleRetrieveFlashLog(
    App& app, const crow::Request& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!setUpRedfishRoute(app, request, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }
    if (!isEmptyActionBody(request))
    {
        BMCWEB_LOG_ERROR("RetrieveFlashLog received unrecognized request body");
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }

    getAttestedWipeSupport([asyncResp](const AttestedWipeSupport& support) {
        if (!support.wipe)
        {
            messages::actionNotSupported(asyncResp->res, "RetrieveFlashLog");
            return;
        }
        dbus::utility::async_method_call(
            [asyncResp](const boost::system::error_code& ec,
                        sdbusplus::message_t& methodError,
                        const std::string& path) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("RetrieveFlashLog D-Bus call failed: {}",
                                     ec.message());
                    mapAttestedWipe(asyncResp->res, methodError,
                                    "RetrieveFlashLog");
                    return;
                }
                populateFlashLog(asyncResp->res, path);
            },
            std::string(factoryResetService), std::string(factoryResetObject),
            std::string(attestedWipeInterface), "RetrieveFlashLog");
    });
}

/**
 * @brief Registers the Nvidia AttestedWipe OEM resource and action routes.
 *
 * @param app The crow application that owns the Redfish routes.
 *
 * @return none.
 */
inline void requestRoutesAttestedWipe(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/AttestedWipe/")
        .privileges(privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleAttestedWipeGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/AttestedWipe/Actions/NvidiaAttestedWipe.GetWipeNonce/")
        .privileges(privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleGetWipeNonce, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/AttestedWipe/Actions/NvidiaAttestedWipe.EnrollWipeKey/")
        .privileges(privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleEnrollWipeKey, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/AttestedWipe/AttestedWipeActionInfo/")
        .privileges(privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleAttestedWipeActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/AttestedWipe/Actions/NvidiaAttestedWipe.AttestedWipe/")
        .privileges(privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleAttestedWipe, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/AttestedWipe/Actions/NvidiaAttestedWipe.RetrieveFlashLog/")
        .privileges(privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleRetrieveFlashLog, std::ref(app)));
}

} // namespace nvidia

using nvidia::requestRoutesAttestedWipe;

} // namespace redfish
