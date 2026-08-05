/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
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

#pragma once

#include "dbus_singleton.hpp"
#include "error_message_utils.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "nvidia_error_messages.hpp"
#include "nvidia_messages.hpp"
#include "resource_messages.hpp"
#include "update_messages.hpp"
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_utils.hpp"

#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace redfish
{

struct ChassisObjectSoftwarePath
{
    std::string chassisName;
    std::string chassisDbusPath;
    std::vector<std::string> objectPaths;
};

using UUID = std::string;
using URI = std::string;

static constexpr std::string_view imageCopyInterface = "com.nvidia.ImageCopy";
static constexpr std::string_view bmcInventoryInterface =
    "xyz.openbmc_project.Inventory.Item.BMC";
static constexpr std::string_view chassisInventoryInterface =
    "xyz.openbmc_project.Inventory.Item.Chassis";

/**
 * @brief Structure to hold the result of a single commit operation
 */
struct CommitResult
{
    std::vector<std::string> objectPaths;
    bool success{};
    std::string message;
    std::string severity;
    std::string messageId;
    std::string resolution;
};

/**
 * @brief Aggregation context for tracking multiple commit image operations
 *
 * This class tracks the progress and results of commit image operations
 * across multiple chassis. It uses the shared_ptr lifecycle pattern to ensure
 * the HTTP response is finalized only after all asynchronous operations
 * complete.
 *
 * The destructor finalizes the response when the last shared_ptr reference
 * is released, which occurs when all async callbacks have completed.
 */
class CommitImageAggregationContext :
    public std::enable_shared_from_this<CommitImageAggregationContext>
{
  public:
    CommitImageAggregationContext(const CommitImageAggregationContext&) =
        delete;
    CommitImageAggregationContext& operator=(
        const CommitImageAggregationContext&) = delete;
    CommitImageAggregationContext(CommitImageAggregationContext&&) = delete;
    CommitImageAggregationContext& operator=(CommitImageAggregationContext&&) =
        delete;

    /**
     * @brief Constructor
     *
     * @param asyncRespIn Shared pointer to the async response object
     * @param totalOpsIn Total number of operations to track
     */
    CommitImageAggregationContext(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
        size_t totalOpsIn) : asyncResp(asyncRespIn), totalOperations(totalOpsIn)
    {
        results.reserve(totalOperations);
    }

    /**
     * @brief Destructor - finalizes the HTTP response
     *
     * Called when the last shared_ptr reference is released (after all
     * async operations complete). Aggregates all results and sets the
     * appropriate HTTP response status and ExtendedInfo messages.
     */
    ~CommitImageAggregationContext()
    {
        finalizeResponse();
    }

    /**
     * @brief Report the result of a single operation
     *
     * This method is called by each async callback when an operation completes.
     *
     * @param objectPaths The software object paths associated with this
     * operation
     * @param success Whether the operation succeeded
     * @param errorMessage Error message if operation failed
     */
    void reportResult(const std::vector<std::string>& objectPaths, bool success,
                      const std::string& messageId = "",
                      const std::string& severity = "",
                      const std::string& message = "",
                      const std::string& resolution = "")
    {
        CommitResult result;
        result.objectPaths = objectPaths;
        result.success = success;
        result.messageId =
            messageId.empty() ? "Base.1.16.0.Success" : messageId;
        result.message = message;
        result.severity = severity.empty() ? "OK" : severity;
        result.resolution = resolution.empty() ? "None." : resolution;
        results.push_back(std::move(result));

        completedOperations++;

        BMCWEB_LOG_DEBUG(
            "CommitImage: Operation completed ({}/{}), success={}, paths={}",
            completedOperations, totalOperations, success, objectPaths.size());
    }

  private:
    /**
     * @brief Finalize the HTTP response after all operations complete
     *
     * Sets the appropriate HTTP status code and creates detailed
     * ExtendedInfo messages for each operation.
     */
    void finalizeResponse()
    {
        BMCWEB_LOG_DEBUG("CommitImage: Finalizing response for {} operations",
                         totalOperations);

        size_t successCount = 0;
        size_t failureCount = 0;

        for (const auto& result : results)
        {
            if (result.success)
            {
                successCount++;
            }
            else
            {
                failureCount++;
            }
        }

        nlohmann::json& extendedInfo =
            asyncResp->res.jsonValue["@Message.ExtendedInfo"];

        if (extendedInfo.is_null())
        {
            extendedInfo = nlohmann::json::array();
        }

        for (const auto& result : results)
        {
            // Successful results without a specific message
            if (result.success && result.message.empty())
            {
                continue;
            }

            // Build firmware inventory paths from the D-Bus object paths
            std::vector<std::string> firmwareInventoryPaths;
            firmwareInventoryPaths.reserve(result.objectPaths.size());
            for (const auto& pathStr : result.objectPaths)
            {
                firmwareInventoryPaths.push_back(
                    "/redfish/v1/UpdateService/FirmwareInventory/" +
                    fs::path(pathStr).filename().string());
            }

            nlohmann::json msg;
            msg["@odata.type"] = "#Message.v1_1_1.Message";
            msg["MessageId"] = result.messageId;
            msg["Severity"] = result.severity;
            msg["Message"] = result.message;
            msg["MessageArgs"] = firmwareInventoryPaths;
            msg["Resolution"] = result.resolution;

            extendedInfo.push_back(std::move(msg));
        }

        // Per DSP0266, only add Success when nothing else was reported.
        if (successCount > 0 && extendedInfo.empty())
        {
            messages::addMessageToJsonRoot(asyncResp->res.jsonValue,
                                           messages::success());
        }

        if (failureCount == 0)
        {
            asyncResp->res.result(boost::beast::http::status::ok);
            BMCWEB_LOG_INFO("CommitImage: All {} operations succeeded",
                            successCount);
        }
        else if (successCount == 0)
        {
            asyncResp->res.result(
                boost::beast::http::status::internal_server_error);
            BMCWEB_LOG_ERROR("CommitImage: All {} operations failed",
                             failureCount);
        }
        else
        {
            asyncResp->res.result(boost::beast::http::status::ok);
            BMCWEB_LOG_WARNING(
                "CommitImage: Partial success - {} succeeded, {} failed",
                successCount, failureCount);
        }
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    size_t totalOperations;
    size_t completedOperations{0};
    std::vector<CommitResult> results;
};

struct CommitImageValueEntry
{
  public:
    URI inventoryUri;
    UUID uuid;

    friend bool operator==(const CommitImageValueEntry& c1,
                           const std::string& c2)
    {
        return c1.inventoryUri == c2;
    }
};

/**
 * @brief Formats ImageCopy error messages into JSON based on error code
 * Message body varies based on the error code from NSM ImageCopy interface
 *
 * @param[in] objectPaths The vector of object paths (last element is used)
 * @param[in] errorCode The error code string from ImageCopy interface
 *
 * @returns Message formatted to JSON with appropriate MessageId, Message,
 * Severity, and Resolution
 */
inline nlohmann::json imageCopyError(
    const std::vector<std::string>& objectPaths, const std::string& errorCode)
{
    std::string messageId;
    std::string message;
    std::string severity;
    std::string resolution;

    std::string firmwareInventoryPaths;
    if (!objectPaths.empty())
    {
        firmwareInventoryPaths = "[";
        bool first = true;
        for (const auto& path : objectPaths)
        {
            if (!first)
            {
                firmwareInventoryPaths += ", ";
            }
            first = false;
            size_t pos = path.rfind('/');
            std::string objectName;
            if (pos != std::string::npos)
            {
                objectName = path.substr(pos + 1);
            }
            else
            {
                objectName = path;
            }
            firmwareInventoryPaths +=
                "'/redfish/v1/UpdateService/FirmwareInventory/";
            firmwareInventoryPaths += objectName;
            firmwareInventoryPaths += "'";
        }
        firmwareInventoryPaths += "]";
    }

    if (errorCode.find("NoBootComplete") != std::string::npos)
    {
        auto msg = redfish::messages::resourceErrorsDetected(
            firmwareInventoryPaths,
            "Device has not received boot complete indication");
        msg["MessageSeverity"] = "Critical";
        msg["Resolution"] =
            "Wait for the Device to receive boot complete indication "
            "before initiating image copy operation.";
        return msg;
    }
    if (errorCode.find("UpdateInProgress") != std::string::npos)
    {
        auto msg = redfish::messages::updateInProgress();
        msg["MessageSeverity"] = "Critical";
        msg["Resolution"] = "Wait for the current firmware update to complete "
                            "before initiating a new image copy operation.";
        return msg;
    }
    if (errorCode.find("ImageCopyInProgress") != std::string::npos)
    {
        auto msg = redfish::messages::resourceErrorsDetected(
            firmwareInventoryPaths, "Image copy is already in progress");
        msg["MessageSeverity"] = "Critical";
        msg["Resolution"] =
            "Wait for the current image copy operation to complete "
            "before initiating a new one.";
        return msg;
    }
    if (errorCode.find("FlashWearMitigation") != std::string::npos)
    {
        auto msg = redfish::messages::resourceErrorsDetected(
            firmwareInventoryPaths,
            "A flash wear mitigation policy is in effect");
        msg["MessageSeverity"] = "Critical";
        msg["Resolution"] =
            "The flash wear mitigation policy is preventing this "
            "operation. Wait for the mitigation period to expire.";
        return msg;
    }
    if (errorCode.find("IncompleteComponentSet") != std::string::npos)
    {
        auto msg = redfish::messages::resourceErrorsDetected(
            firmwareInventoryPaths,
            "Additional components required in the request");
        msg["MessageSeverity"] = "Critical";
        msg["Resolution"] =
            "Include all required components in the image copy request.";
        return msg;
    }
    if (errorCode.find("ImageCopyCompleted") != std::string::npos)
    {
        return redfish::messages::imageCopyCompleted(firmwareInventoryPaths);
    }
    if (errorCode.find("TimedOut") != std::string::npos)
    {
        messageId = "OpenBMC.0.4.1.AsyncError";
        message = "Async command failed timed out for software objects: " +
                  firmwareInventoryPaths;
        severity = "Critical";
        resolution =
            "Resubmit the request. If the problem persists, consider resetting the service or provider.";
    }
    else
    {
        auto msg = redfish::messages::resourceErrorsDetected(
            firmwareInventoryPaths,
            "Operation failed with error: " + errorCode);
        msg["MessageSeverity"] = "Critical";
        msg["Resolution"] =
            "Check the error code and system logs for more details.";
        return msg;
    }

    return nlohmann::json{{"@odata.type", "#Message.v1_1_1.Message"},
                          {"MessageId", messageId},
                          {"Message", message},
                          {"MessageArgs", {errorCode}},
                          {"MessageSeverity", severity},
                          {"Resolution", resolution}};
}

inline std::vector<CommitImageValueEntry> getAllowableValues()
{
    static std::vector<CommitImageValueEntry> allowableValues;

    if (!allowableValues.empty())
    {
        return allowableValues;
    }

    std::string configPath(BMCWEB_FW_UUID_MAPPING_JSON);

    if (!fs::exists(configPath))
    {
        BMCWEB_LOG_ERROR("The file doesn't exist: {}", configPath);
        return allowableValues;
    }

    std::ifstream jsonFile(configPath);
    std::string jsonContents((std::istreambuf_iterator<char>(jsonFile)),
                             std::istreambuf_iterator<char>());
    auto data = Json::parse(jsonContents, nullptr, false);
    if (data.is_discarded())
    {
        BMCWEB_LOG_ERROR("Unable to parse json data {}", configPath);
        return allowableValues;
    }

    const Json emptyJson{};

    auto entries = data.value("FwUuidMap", emptyJson);
    for (const auto& element : entries.items())
    {
        try
        {
            CommitImageValueEntry allowableVal;

            allowableVal.inventoryUri = element.key();
            allowableVal.uuid = static_cast<std::string>(element.value());

            allowableValues.push_back(allowableVal);
        }
        catch ([[maybe_unused]] const std::exception& e)
        {
            BMCWEB_LOG_ERROR("FW UUID map format error.");
        }
    }
    return allowableValues;
}

/**
 * @brief Chassis info combining software paths and D-Bus path
 */
struct ChassisInfo
{
    std::vector<std::string> softwarePaths;
    std::string dbusPath;
};

/**
 * @brief State tracker for asynchronous collection of chassis-to-software path
 * mappings
 *
 * This struct maintains the state for the async operation chain initiated by
 * collectImageCopySoftwarePaths. It tracks pending D-Bus operations and
 * accumulates the chassis-to-software path mappings.
 */
struct CollectionState
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;

    std::function<void(const std::map<std::string, ChassisInfo>&)> callback;

    std::map<std::string, ChassisInfo> chassisMap;

    size_t pendingOps = 0;

    /**
     * @brief Checks if all operations are complete and invokes the callback
     *
     * @return void
     */
    void checkComplete() const
    {
        if (pendingOps == 0)
        {
            callback(chassisMap);
        }
    }
};

/**
 * @brief Handles the result of checking if a chassis has ImageCopy interface
 *
 * This function is called as a callback after querying D-Bus to determine if
 * a chassis object implements the com.nvidia.ImageCopy interface. If the
 * interface is found, the software path is added to the chassis mapping.
 *
 * @param state Shared state containing the collection context and results
 * @param chassisName Name of the chassis being checked
 * @param softwarePath D-Bus path to the software object
 * @param assocEndpoint D-Bus path to the associated chassis endpoint
 * @param errorCode Error code from the D-Bus GetObject call
 * @param objInfo Map of services and their provided interfaces
 *
 * @return void
 */
inline void handleImageCopyInterfaceCheck(
    const std::shared_ptr<CollectionState>& state,
    const std::string& chassisName, const std::string& softwarePath,
    const std::string& assocEndpoint,
    const boost::system::error_code& errorCode,
    const dbus::utility::MapperGetObject& objInfo)
{
    state->pendingOps--;

    if (errorCode)
    {
        BMCWEB_LOG_ERROR("D-Bus call failed to get dbus object for {}: {}",
                         assocEndpoint, errorCode.message());
        state->checkComplete();
        return;
    }

    bool hasImageCopyInterface = false;
    for (const auto& [service, interfaces] : objInfo)
    {
        for (const auto& interface : interfaces)
        {
            if (interface == imageCopyInterface)
            {
                hasImageCopyInterface = true;
                break;
            }
        }
        if (hasImageCopyInterface)
        {
            break;
        }
    }

    if (hasImageCopyInterface)
    {
        state->chassisMap[chassisName].softwarePaths.push_back(softwarePath);
        state->chassisMap[chassisName].dbusPath = assocEndpoint;
        BMCWEB_LOG_DEBUG(
            "Added mapping - Chassis: {} (path: {}) -> Software: {} (has com.nvidia.ImageCopy)",
            chassisName, assocEndpoint, softwarePath);
    }

    state->checkComplete();
}

/**
 * @brief Handles the result of querying associated_chassis endpoints
 *
 * This function processes the endpoints from the associated_chassis D-Bus
 * association. It extracts chassis names from the endpoints and initiates
 * a check to determine if each chassis supports the ImageCopy interface.
 *
 * @param state Shared state containing the collection context and results
 * @param softwarePath D-Bus path to the software object
 * @param errorCode Error code from the D-Bus getAssociationEndPoints call
 * @param associatedEndpoints Vector of associated chassis endpoint paths
 *
 * @return void
 */
inline void handleAssociatedChassisEndpoints(
    const std::shared_ptr<CollectionState>& state,
    const std::string& softwarePath, const boost::system::error_code& errorCode,
    const std::vector<std::string>& associatedEndpoints)
{
    state->pendingOps--;

    if (errorCode)
    {
        BMCWEB_LOG_DEBUG(
            "D-Bus call failed to get associated_chassis for {}: {}, "
            "falling back to check inventory endpoint directly",
            softwarePath, errorCode.message());

        state->checkComplete();
        return;
    }

    // Process each associated chassis endpoint
    for (const std::string& assocEndpoint : associatedEndpoints)
    {
        // Extract chassis name from path (last segment)
        sdbusplus::object_path objPath(assocEndpoint);
        std::string chassisName = objPath.filename();

        if (!chassisName.empty())
        {
            // Check if chassis implements com.nvidia.ImageCopy interface
            state->pendingOps++;

            dbus::utility::getDbusObject(
                assocEndpoint, std::array<std::string_view, 0>{},
                [state, chassisName, softwarePath,
                 assocEndpoint](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& objInfo) {
                    handleImageCopyInterfaceCheck(state, chassisName,
                                                  softwarePath, assocEndpoint,
                                                  ec, objInfo);
                });

            break;
        }
    }

    state->checkComplete();
}

/**
 * @brief Handles the result of checking interfaces for an inventory endpoint
 *
 * This function is called after querying D-Bus to determine what interfaces
 * an inventory endpoint implements. Based on the interfaces, it determines
 * the appropriate association path to query.
 *
 * @param state Shared state containing the collection context and results
 * @param softwarePath D-Bus path to the software object
 * @param endpoint D-Bus path of the inventory endpoint
 * @param errorCode Error code from the D-Bus GetObject call
 * @param objInfo Map of services and their provided interfaces
 *
 * @return void
 */
inline void handleInventoryEndpointInterfaces(
    const std::shared_ptr<CollectionState>& state,
    const std::string& softwarePath, const std::string& endpoint,
    const boost::system::error_code& errorCode,
    const dbus::utility::MapperGetObject& objInfo)
{
    state->pendingOps--;

    if (errorCode || objInfo.empty())
    {
        BMCWEB_LOG_DEBUG(
            "Failed to get interfaces for inventory endpoint {}: {}", endpoint,
            errorCode ? errorCode.message() : "empty response");
        state->checkComplete();
        return;
    }

    // Check what interfaces this inventory endpoint implements
    bool isBmcPath = false;
    bool isChassisPath = false;

    for (const auto& [service, interfaces] : objInfo)
    {
        for (const auto& intf : interfaces)
        {
            if (intf == bmcInventoryInterface)
            {
                isBmcPath = true;
                break;
            }
            if (intf == chassisInventoryInterface)
            {
                isChassisPath = true;
            }
        }
        if (isBmcPath)
        {
            break;
        }
    }

    if (!isBmcPath && !isChassisPath)
    {
        BMCWEB_LOG_DEBUG(
            "Inventory endpoint {} has neither BMC nor Chassis interface",
            endpoint);
        state->checkComplete();
        return;
    }

    BMCWEB_LOG_DEBUG(
        "Found {} path {} in inventory endpoints for software path {}",
        isBmcPath ? "BMC" : "chassis", endpoint, softwarePath);

    // Query associated_chassis/associated_ROT for this inventory path
    std::string associatedChassisPath =
        isBmcPath ? endpoint + "/associated_ROT"
                  : endpoint + "/associated_chassis";

    state->pendingOps++;

    dbus::utility::getAssociationEndPoints(
        associatedChassisPath,
        [state, softwarePath](
            const boost::system::error_code& ec,
            const dbus::utility::MapperEndPoints& associatedEndpoints) {
            handleAssociatedChassisEndpoints(state, softwarePath, ec,
                                             associatedEndpoints);
        });

    state->checkComplete();
}

/**
 * @brief Handles the result of querying inventory endpoints
 *
 * This function processes inventory endpoints from a software object's
 * /inventory association. It queries D-Bus to determine what interfaces
 * each endpoint implements to find chassis or BMC paths.
 *
 * @param state Shared state containing the collection context and results
 * @param softwarePath D-Bus path to the software object
 * @param errorCode Error code from the D-Bus getProperty call
 * @param invEndpoints Vector of inventory endpoint paths
 *
 * @return void
 */
inline void handleInventoryEndpoints(
    const std::shared_ptr<CollectionState>& state,
    const std::string& softwarePath, const boost::system::error_code& errorCode,
    const std::vector<std::string>& invEndpoints)
{
    state->pendingOps--;

    if (errorCode)
    {
        BMCWEB_LOG_DEBUG(
            "D-Bus call failed to get inventory endpoints for {}: {}",
            softwarePath, errorCode.message());
        state->checkComplete();
        return;
    }

    if (invEndpoints.empty())
    {
        BMCWEB_LOG_DEBUG("No inventory endpoints found for {}", softwarePath);
        state->checkComplete();
        return;
    }

    // Make GetObject D-Bus call for each inventory endpoint to check its
    // interfaces (BMC or Chassis)
    for (const std::string& endpoint : invEndpoints)
    {
        state->pendingOps++;

        dbus::utility::getDbusObject(
            endpoint,
            std::array<std::string_view, 2>{bmcInventoryInterface,
                                            chassisInventoryInterface},
            [state, softwarePath,
             endpoint](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetObject& objInfo) {
                handleInventoryEndpointInterfaces(state, softwarePath, endpoint,
                                                  ec, objInfo);
            });
    }

    state->checkComplete();
}

/**
 * @brief Handles the response from inventory path D-Bus lookup
 *
 * This function processes the result of checking if an inventory path exists
 * on D-Bus. If the path exists, it queries the inventory association endpoints
 * to continue the discovery chain. If not, it decrements the pending operations
 * counter and checks for completion.
 *
 * @param state Shared collection state tracking async operations
 * @param softwarePath D-Bus path of the software object being processed
 * @param inventoryObjectPath D-Bus path of the inventory association
 * @param errorCode Error code from the D-Bus getDbusObject call
 * @param mapperResponse Response from the object mapper lookup
 *
 * @return void
 */
inline void handleInventoryPathLookup(
    const std::shared_ptr<CollectionState>& state,
    const std::string& softwarePath, const std::string& inventoryObjectPath,
    const boost::system::error_code& errorCode,
    const dbus::utility::MapperGetObject& mapperResponse)
{
    if (errorCode || mapperResponse.empty())
    {
        // Inventory path doesn't exist, skip this software object
        state->pendingOps--;
        state->checkComplete();
        return;
    }

    // Query inventory association endpoints
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", inventoryObjectPath,
        "xyz.openbmc_project.Association", "endpoints",
        [state, softwarePath](const boost::system::error_code& ec,
                              const std::vector<std::string>& invEndpoints) {
            handleInventoryEndpoints(state, softwarePath, ec, invEndpoints);
        });
}

/**
 * @brief Handles the result of getSubTree for software objects
 *
 * This function processes the D-Bus subtree response for software objects.
 * It filters for objects with /inventory sub-paths and initiates inventory
 * endpoint queries for each valid software object found.
 *
 * @param asyncResp Shared async response object for HTTP responses
 * @param callback Function to call with the final chassis-to-software mapping
 * @param errorCode Error code from the D-Bus getSubTree call
 * @param subtree Map of D-Bus object paths and their services
 *
 * @return void
 */
inline void handleSoftwareSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::function<void(const std::map<std::string, ChassisInfo>&)>&
        callback,
    const boost::system::error_code& errorCode,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (errorCode)
    {
        BMCWEB_LOG_ERROR("D-Bus call failed to get software objects: {}",
                         errorCode);
        redfish::messages::internalError(asyncResp->res);
        return;
    }

    auto state = std::make_shared<CollectionState>();
    state->asyncResp = asyncResp;
    state->callback = callback;

    // Identify software objects with /inventory sub-path
    for (const auto& [objectPath, serviceMap] : subtree)
    {
        // Construct the inventory path by appending "/inventory"
        std::string inventoryObjectPath = objectPath + "/inventory";

        // Use objectPath as the software path
        std::string softwarePath = objectPath;
        state->pendingOps++;

        // Check if inventory path exists on D-Bus
        dbus::utility::getDbusObject(
            inventoryObjectPath,
            std::array<std::string_view, 1>{"xyz.openbmc_project.Association"},
            [state, softwarePath, inventoryObjectPath](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetObject& mapperResponse) {
                handleInventoryPathLookup(state, softwarePath,
                                          inventoryObjectPath, ec,
                                          mapperResponse);
            });
    }

    // If no pending operations, invoke callback immediately
    state->checkComplete();
}

/**
 * @brief Collects mappings between chassis and software paths supporting
 * ImageCopy
 *
 * This function initiates an asynchronous operation chain to discover all
 * chassis-to-software path mappings where the chassis implements the
 * com.nvidia.ImageCopy interface. The operation traverses D-Bus associations
 * from software objects through inventory to chassis objects.
 *
 * The function performs the following steps asynchronously:
 * 1. Query all software objects under /xyz/openbmc_project/software
 * 2. For each software object with an /inventory association:
 *    a. Query inventory endpoints
 *    b. Find chassis paths in the inventory
 *    c. Query associated_chassis endpoints
 *    d. Check if chassis implements com.nvidia.ImageCopy interface
 *    e. Add mapping if interface is present
 *
 * @param asyncResp Shared async response object for HTTP error reporting
 * @param callback Function called with the final map of chassis names to
 *                 software paths when all operations complete
 *
 * @return void
 */
inline void collectImageCopySoftwarePaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::function<void(const std::map<std::string, ChassisInfo>&)>&
        callback)
{
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Software.Version"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/software", 0, interfaces,
        [asyncResp,
         callback](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
            handleSoftwareSubtree(asyncResp, callback, ec, subtree);
        });
}

/**
 * @brief Helper class to monitor ImageCopyRequestStatus property changes
 *
 * This class sets up a D-Bus property change match to monitor the
 * ImageCopyRequestStatus property and react when the operation completes
 * or fails.
 */
class ImageCopyStatusMonitor :
    public std::enable_shared_from_this<ImageCopyStatusMonitor>
{
  public:
    // Timeout for image copy operation (matches NSM async infra timeout)
    static constexpr std::chrono::seconds monitorTimeout{60};

    ImageCopyStatusMonitor(
        const std::shared_ptr<CommitImageAggregationContext>& ctx,
        const std::string& chassis, const std::string& dbusPath,
        const std::vector<std::string>& paths) :
        aggregationCtx(ctx), chassisName(chassis), chassisDBusPath(dbusPath),
        objectPaths(paths),
        timeoutTimer(crow::connections::systemBus->get_io_context())
    {}

    ImageCopyStatusMonitor(const ImageCopyStatusMonitor&) = delete;
    ImageCopyStatusMonitor& operator=(const ImageCopyStatusMonitor&) = delete;
    ImageCopyStatusMonitor(ImageCopyStatusMonitor&&) = delete;
    ImageCopyStatusMonitor& operator=(ImageCopyStatusMonitor&&) = delete;

    ~ImageCopyStatusMonitor()
    {
        timeoutTimer.cancel();
        destroyMatch();
    }

    /**
     * @brief Start monitoring the ImageCopyRequestStatus property
     */
    void startMonitoring()
    {
        createMatch();
        startTimeout();
        // Query initial status
        queryStatus();
    }

  private:
    /**
     * @brief Create D-Bus match for ImageCopyRequestStatus property changes
     */
    void createMatch()
    {
        std::string matchRule = sdbusplus::bus::match::rules::propertiesChanged(
            chassisDBusPath, std::string(imageCopyInterface));

        match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, matchRule,
            [self = shared_from_this()](sdbusplus::message::message& msg) {
                self->handlePropertyChange(msg);
            });
    }

    /**
     * @brief Start timeout timer for the image copy operation
     */
    void startTimeout()
    {
        timeoutTimer.expires_after(monitorTimeout);
        timeoutTimer.async_wait([self = shared_from_this()](
                                    const boost::system::error_code& ec) {
            if (ec == boost::asio::error::operation_aborted)
            {
                // Timer was canceled, operation completed normally
                return;
            }
            if (self->completed)
            {
                return;
            }
            self->completed = true;
            self->destroyMatch();
            BMCWEB_LOG_ERROR(
                "Operation timed out for chassis {} object paths: {}",
                self->chassisName, join(self->objectPaths, ", "));
            self->aggregationCtx->reportResult(
                self->objectPaths, false, "Base.1.16.0.OperationTimeout",
                "Critical", "Image copy operation timed out after 60 seconds",
                "Retry the operation. If the problem persists, check NSM service logs.");
        });
    }

    /**
     * @brief Destroy the D-Bus match
     */
    void destroyMatch()
    {
        if (match)
        {
            match.reset();
        }
    }

    /**
     * @brief Query the current ImageCopyRequestStatus
     */
    void queryStatus()
    {
        dbus::utility::getProperty<std::string>(
            "xyz.openbmc_project.NSM", chassisDBusPath,
            std::string(imageCopyInterface), "ImageCopyRequestStatus",
            [self = shared_from_this(),
             ctx = aggregationCtx](const boost::system::error_code& ec,
                                   const std::string& status) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "D-Bus call failed to query ImageCopyRequestStatus for chassis {} object paths {}: {}",
                        self->chassisName, join(self->objectPaths, ", "),
                        ec.message());
                    ctx->reportResult(
                        self->objectPaths, false, "Base.1.16.0.GeneralError",
                        "Critical", "Failed to query status: " + ec.message());
                    return;
                }
                self->handleStatus(status);
            });
    }

    /**
     * @brief Handle property change notifications
     */
    void handlePropertyChange(sdbusplus::message::message& msg)
    {
        if (completed)
        {
            return;
        }

        std::string interface;
        dbus::utility::DBusPropertiesMap propertiesMap;
        msg.read(interface, propertiesMap);

        // Look for ImageCopyRequestStatus property
        for (const auto& [key, value] : propertiesMap)
        {
            if (key == "ImageCopyRequestStatus")
            {
                const std::string* status = std::get_if<std::string>(&value);
                if (status != nullptr)
                {
                    handleStatus(*status);
                }
                break;
            }
        }
    }

    /**
     * @brief Handle status update
     */
    void handleStatus(const std::string& status)
    {
        // Keep both the monitor and aggregationCtx alive for the duration of
        // this function This is critical because we may call destroyMatch()
        // which destroys the match callback that's currently executing,
        // potentially releasing the last references before we're done using
        // them
        auto self = shared_from_this();
        auto ctx = aggregationCtx;

        if (completed)
        {
            return;
        }

        // Continue monitoring if status is InProgress
        if (status == "com.nvidia.ImageCopy.ImageCopyRequestStatus.Processing")
        {
            return;
        }

        // Operation completed or failed
        completed = true;
        timeoutTimer.cancel();
        destroyMatch();

        if (status == "com.nvidia.ImageCopy.ImageCopyRequestStatus.Accepted")
        {
            ctx->reportResult(objectPaths, true);
        }
        else if (status ==
                 "com.nvidia.ImageCopy.ImageCopyRequestStatus.Rejected")
        {
            // readErrorCode() will further classify the outcome,
            // may be benign "already completed" case.
            BMCWEB_LOG_WARNING(
                "Image copy rejected for chassis {} object paths: {}; reading ErrorCode",
                chassisName, join(objectPaths, ", "));

            readErrorCode();
        }
        else
        {
            BMCWEB_LOG_ERROR(
                "Unknown ImageCopy Request Status '{}' for chassis {} object paths {}",
                status, chassisName, join(objectPaths, ", "));
            ctx->reportResult(
                objectPaths, false, "Base.1.16.0.GeneralError", "Critical",
                "Image copy returned unexpected status: " + status,
                "Check NSM service logs for details.");
        }
    }

    /**
     * @brief Read ErrorCode property when operation fails
     */
    void readErrorCode()
    {
        dbus::utility::getProperty<std::string>(
            "xyz.openbmc_project.NSM", chassisDBusPath,
            std::string(imageCopyInterface), "ErrorCode",
            [self = shared_from_this(),
             ctx = aggregationCtx](const boost::system::error_code& ec,
                                   const std::string& errorCode) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "D-Bus call failed to read ImageCopy ErrorCode property for chassis {} object paths {}: {}",
                        self->chassisName, join(self->objectPaths, ", "),
                        ec.message());
                    ctx->reportResult(
                        self->objectPaths, false, "Base.1.16.0.GeneralError",
                        "Critical", "Image copy failed. Error: " + ec.message(),
                        "Verify NSM service is running and try again.");
                    return;
                }

                bool alreadyCompleted =
                    errorCode.find("ImageCopyCompleted") != std::string::npos;

                if (alreadyCompleted)
                {
                    BMCWEB_LOG_INFO(
                        "CommitImage: image copy already completed for chassis {} object paths {}: {}",
                        self->chassisName, join(self->objectPaths, ", "),
                        errorCode);
                }
                else
                {
                    BMCWEB_LOG_ERROR(
                        "CommitImage: ErrorCode for chassis {} object paths {}: {}",
                        self->chassisName, join(self->objectPaths, ", "),
                        errorCode);
                }

                // Map error code to user-friendly message using centralized
                // error message registry
                nlohmann::json errorMsg =
                    redfish::imageCopyError(self->objectPaths, errorCode);

                // Extract message details from the error JSON
                std::string messageId =
                    errorMsg.value("MessageId", "Base.1.16.0.GeneralError");
                std::string message =
                    errorMsg.value("Message", "Operation failed");
                std::string severity =
                    errorMsg.value("MessageSeverity", "Critical");
                std::string resolution = errorMsg.value("Resolution", "None.");

                ctx->reportResult(self->objectPaths, alreadyCompleted,
                                  messageId, severity, message, resolution);
            });
    }

    std::shared_ptr<CommitImageAggregationContext> aggregationCtx;
    std::string chassisName;
    std::string chassisDBusPath;
    std::vector<std::string> objectPaths;
    std::unique_ptr<sdbusplus::bus::match_t> match;
    boost::asio::steady_timer timeoutTimer;
    bool completed = false;
};

/**
 * @brief Execute InitiateImageCopy command with aggregation support
 *
 * Initiates an image copy operation and monitors the ImageCopyRequestStatus
 * property for completion. When the status changes from InProgress, the
 * operation result is reported to the aggregation context.
 *
 * @param aggregationCtx Shared pointer to aggregation context for tracking
 * multiple operations
 * @param chassisObjectSoftwarePath object containing chassis name and software
 * object paths
 *
 * @return None.
 */
inline void initiateImageCopy(
    const std::shared_ptr<CommitImageAggregationContext>& aggregationCtx,
    const ChassisObjectSoftwarePath& chassisObjectSoftwarePath)
{
    std::string chassisDBusPath = chassisObjectSoftwarePath.chassisDbusPath;
    std::string chassisName = chassisObjectSoftwarePath.chassisName;
    std::vector<std::string> objectPaths =
        chassisObjectSoftwarePath.objectPaths;

    std::vector<sdbusplus::object_path> objectPathsVector;
    objectPathsVector.reserve(objectPaths.size());
    for (const auto& pathStr : objectPaths)
    {
        objectPathsVector.emplace_back(pathStr);
    }

    // Create status monitor
    auto monitor = std::make_shared<ImageCopyStatusMonitor>(
        aggregationCtx, chassisName, chassisDBusPath, objectPaths);

    dbus::utility::async_method_call(
        [self = aggregationCtx, chassisName, objectPaths,
         monitor](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "D-Bus call RequestImageCopy for chassis {} object paths {} failed: {}",
                    chassisName, join(objectPaths, ", "), ec.message());
                self->reportResult(
                    objectPaths, false, "Base.1.16.0.GeneralError", "Critical",
                    "Failed to initiate image copy operation. Error: " +
                        ec.message(),
                    "Verify NSM service is running and try again.");
                return;
            }

            BMCWEB_LOG_DEBUG(
                "D-Bus call RequestImageCopy for object paths {} succeeded",
                join(objectPaths, ", "));

            // Start monitoring the status property
            monitor->startMonitoring();
        },
        "xyz.openbmc_project.NSM", chassisDBusPath,
        std::string(imageCopyInterface), "RequestImageCopy", objectPathsVector);
}
} // namespace redfish
