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

#include "bmcweb_config.h"

#include "app.hpp"
#include "background_copy.hpp"
#include "commit_image.hpp"
#include "component_integrity.hpp"
#include "dbus_utility.hpp"
#include "debug_token/erase_policy.hpp"
#include "multipart_parser.hpp"
#include "ossl_random.hpp"
#include "persistentstorage_util.hpp"
#include "query.hpp"
#include "redfish_aggregator.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/sw_utils.hpp"

#include <sys/mman.h>

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <http_client.hpp>
#include <http_connection.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <update_messages.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/fw_utils.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace redfish
{

/* holds compute digest operation state to allow one operation at a time */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool computeDigestInProgress = false;
const std::string hashComputeInterface = "com.Nvidia.ComputeHash";
constexpr auto retimerHashMaxTimeSec =
    180; // 2 mins for 2 attempts and 1 addional min as buffer
// Only allow one update at a time
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool fwUpdateInProgress = false;

// allowed firmware image size
constexpr const size_t firmwareImageLimitBytes =
    // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
    BMCWEB_FIRMWARE_IMAGE_LIMIT * 1024 * 1024;

class BMCStatusAsyncResp
{
  public:
    explicit BMCStatusAsyncResp(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn) :
        asyncResp(asyncRespIn)
    {}

    ~BMCStatusAsyncResp()
    {
        if (bmcStateString == "xyz.openbmc_project.State.BMC.BMCState.Ready" &&
            hostStateString !=
                "xyz.openbmc_project.State.Host.HostState.TransitioningToRunning" &&
            hostStateString !=
                "xyz.openbmc_project.State.Host.HostState.TransitioningToOff" &&
            pldm_serviceStatus && mctp_serviceStatus)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
        }
        else
        {
            asyncResp->res.jsonValue["Status"]["State"] = "UnavailableOffline";
        }
        if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
        {
            asyncResp->res.jsonValue["Status"]["Conditions"] =
                nlohmann::json::array();
        }
    }

    BMCStatusAsyncResp(const BMCStatusAsyncResp&) = delete;
    BMCStatusAsyncResp(BMCStatusAsyncResp&&) = delete;
    BMCStatusAsyncResp& operator=(const BMCStatusAsyncResp&) = delete;
    BMCStatusAsyncResp& operator=(BMCStatusAsyncResp&&) = delete;

    const std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    bool pldm_serviceStatus = false;
    bool mctp_serviceStatus = false;
    std::string bmcStateString;
    std::string hostStateString;
};

inline static bool validSubpath([[maybe_unused]] const std::string& objPath,
                                [[maybe_unused]] const std::string& objectPath)
{
    return false;
}

inline static bool relatedItemAlreadyPresent(const nlohmann::json& relatedItem,
                                             const std::string& itemPath)
{
    for (const auto& obj : relatedItem)
    {
        if (obj.contains("@odata.id") && obj["@odata.id"] == itemPath)
        {
            return true;
        }
    }
    return false;
}

inline static void getRelatedItemsDrive(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const sdbusplus::message::object_path& objPath)
{
    // Drive is expected to be under a Chassis
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         const std::vector<std::string>& objects) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                return;
            }

            nlohmann::json& relatedItem = aResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                aResp->res.jsonValue["RelatedItem@odata.count"];

            for (const auto& object : objects)
            {
                if (!validSubpath(objPath.str, object))
                {
                    continue;
                }

                sdbusplus::message::object_path path(object);
                relatedItem.push_back(
                    {{"@odata.id",
                      "/redfish/v1/"
                      "Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/"
                          "Storage/" +
                          path.filename() + "/Drives/" + objPath.filename()}});
                break;
            }
            relatedItemCount = relatedItem.size();
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string, 1>{
            "xyz.openbmc_project.Inventory.Item.Storage"});
}

inline static void getRelatedItemsStorageController(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         const std::vector<std::string>& objects) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                return;
            }

            for (const auto& object : objects)
            {
                if (!validSubpath(objPath.str, object))
                {
                    continue;
                }

                sdbusplus::message::object_path path(object);

                crow::connections::systemBus->async_method_call(
                    [aResp, objPath,
                     path](const boost::system::error_code& errCodeController,
                           const dbus::utility::MapperGetSubTreeResponse&
                               subtree) {
                        if (errCodeController || subtree.empty())
                        {
                            return;
                        }
                        nlohmann::json& relatedItem =
                            aResp->res.jsonValue["RelatedItem"];
                        nlohmann::json& relatedItemCount =
                            aResp->res.jsonValue["RelatedItem@odata.count"];

                        for (size_t i = 0; i < subtree.size(); ++i)
                        {
                            if (subtree[i].first != objPath.str)
                            {
                                continue;
                            }

                            relatedItem.push_back(
                                {{"@odata.id",
                                  "/redfish/v1/Systems/" +
                                      std::string(
                                          BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                      "/Storage/" + path.filename() +
                                      "#/StorageControllers/" +
                                      std::to_string(i)}});
                            break;
                        }

                        relatedItemCount = relatedItem.size();
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree", object,
                    int32_t(0),
                    std::array<const char*, 1>{"xyz.openbmc_project.Inventory."
                                               "Item.StorageController"});
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Storage"});
}

inline static void getRelatedItemsPowerSupply(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
                BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
                return;
            }
            std::string chassisName = "chassis";
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("Invalid Object.");
                return;
            }
            for (const std::string& path : *data)
            {
                sdbusplus::message::object_path myLocalPath(path);
                chassisName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];
            relatedItem.push_back(
                {{"@odata.id",
                  "/redfish/v1/Chassis/" + chassisName +
                      "/PowerSubsystem/PowerSupplies/" + objPath.filename()}});

            relatedItemCount = relatedItem.size();
            asyncResp->res.jsonValue["Description"] = "Power Supply image";
        },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void getRelatedItemsPCIeDevice(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
                BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
                return;
            }
            std::string chassisName = "chassis";
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("Invalid Object.");
                return;
            }
            for (const std::string& path : *data)
            {
                sdbusplus::message::object_path myLocalPath(path);
                chassisName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];
            relatedItem.push_back(
                {{"@odata.id", "/redfish/v1/Chassis/" + chassisName +
                                   "/PCIeDevices/" + objPath.filename()}});

            relatedItemCount = relatedItem.size();
        },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void getRelatedItemsSwitch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
                BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
                return;
            }
            std::string fabricName = "fabric";
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("Invalid Object.");
                return;
            }
            for (const std::string& path : *data)
            {
                sdbusplus::message::object_path myLocalPath(path);
                fabricName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];
            relatedItem.push_back(
                {{"@odata.id", "/redfish/v1/Fabrics/" + fabricName +
                                   "/Switches/" + objPath.filename()}});

            relatedItemCount = relatedItem.size();
        },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void getRelatedItemsNetworkAdapter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
                return;
            }
            std::string networAdapterChassisName = "Networkadapter";
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("Invalid Object.");
                return;
            }
            if (!data->empty())
            {
                sdbusplus::message::object_path myLocalPath(data->front());
                networAdapterChassisName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];
            relatedItem.push_back(
                {{"@odata.id",
                  "/redfish/v1/Chassis/" + networAdapterChassisName +
                      "/NetworkAdapters/" + objPath.filename()}});

            relatedItemCount = relatedItem.size();
        },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void getRelatedItemsOther(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const sdbusplus::message::object_path& association)
{
    // Find supported device types.
    crow::connections::systemBus->async_method_call(
        [aResp, association](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objects) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                                 ec.message());
                return;
            }
            if (objects.empty())
            {
                return;
            }

            nlohmann::json& relatedItem = aResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                aResp->res.jsonValue["RelatedItem@odata.count"];

            for (const auto& object : objects)
            {
                for (const auto& interfaces : object.second)
                {
                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.Drive")
                    {
                        getRelatedItemsDrive(aResp, association);
                    }

                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.PCIeDevice")
                    {
                        getRelatedItemsPCIeDevice(aResp, association);
                    }

                    if (interfaces == "xyz.openbmc_project."
                                      "Inventory."
                                      "Item.Accelerator" ||
                        interfaces == "xyz.openbmc_project."
                                      "Inventory.Item.Cpu")
                    {
                        relatedItem.push_back(
                            {{"@odata.id",
                              "/redfish/v1/Systems/" +
                                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                  "/Processors/" + association.filename()}});
                    }

                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.Board" ||
                        interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.Chassis")
                    {
                        std::string itemPath =
                            "/redfish/v1/Chassis/" + association.filename();
                        if (!relatedItemAlreadyPresent(relatedItem, itemPath))
                        {
                            relatedItem.push_back({{"@odata.id", itemPath}});
                        }
                    }

                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.StorageController")
                    {
                        getRelatedItemsStorageController(aResp, association);
                    }
                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.PowerSupply")
                    {
                        getRelatedItemsPowerSupply(aResp, association);
                    }

                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.Switch")
                    {
                        getRelatedItemsSwitch(aResp, association);
                    }

                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.NetworkInterface")
                    {
                        getRelatedItemsNetworkAdapter(aResp, association);
                    }
                }
            }

            relatedItemCount = relatedItem.size();
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", association.str,
        std::array<const char*, 10>{
            "xyz.openbmc_project.Inventory.Item.PowerSupply",
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.PCIeDevice",
            "xyz.openbmc_project.Inventory.Item.Switch",
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Drive",
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis",
            "xyz.openbmc_project.Inventory.Item.StorageController",
            "xyz.openbmc_project.Inventory.Item.NetworkInterface"});
}

/*
    Fill related item links for Software with other purposes.
    Use other purpose for device level softwares.
*/
inline static void getRelatedItemsOthers(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& swId,
    std::string inventoryPathIn = "")
{
    BMCWEB_LOG_DEBUG("getRelatedItemsOthers enter");

    if (inventoryPathIn.empty())
    {
        inventoryPathIn = "/xyz/openbmc_project/software/";
    }

    aResp->res.jsonValue["RelatedItem"] = nlohmann::json::array();
    aResp->res.jsonValue["RelatedItem@odata.count"] = 0;

    crow::connections::systemBus->async_method_call(
        [aResp, swId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }

            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     obj : subtree)
            {
                sdbusplus::message::object_path path(obj.first);
                if (path.filename() != swId)
                {
                    continue;
                }

                if (obj.second.empty())
                {
                    continue;
                }
                crow::connections::systemBus->async_method_call(
                    [aResp](const boost::system::error_code& errCodeAssoc,
                            std::variant<std::vector<std::string>>& resp) {
                        if (errCodeAssoc)
                        {
                            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}",
                                             errCodeAssoc,
                                             errCodeAssoc.message());
                            return;
                        }

                        std::vector<std::string>* associations =
                            std::get_if<std::vector<std::string>>(&resp);
                        if ((associations == nullptr) ||
                            (associations->empty()))
                        {
                            BMCWEB_LOG_ERROR(
                                "Zero association for the software");
                            return;
                        }

                        for (const std::string& association : *associations)
                        {
                            if (association.empty())
                            {
                                continue;
                            }
                            sdbusplus::message::object_path associationPath(
                                association);

                            getRelatedItemsOther(aResp, associationPath);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper", path.str + "/inventory",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", inventoryPathIn, 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
}

inline void extendUpdateServiceGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.jsonValue["SoftwareInventory"] = {
        {"@odata.id", "/redfish/v1/UpdateService/SoftwareInventory"}};
    asyncResp->res.jsonValue["Actions"]["Oem"]["Nvidia"]
                            ["#NvidiaUpdateService.CommitImage"] = {
        {"target",
         "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage"},
        {"@Redfish.ActionInfo",
         "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo"}};
    asyncResp->res.jsonValue["Actions"]["Oem"]["Nvidia"]
                            ["#NvidiaUpdateService.PublicKeyExchange"] = {
        {"target",
         "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange"}};
    asyncResp->res
        .jsonValue["Actions"]["Oem"]["Nvidia"]
                  ["#NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"] = {
        {"target",
         "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"}};

    if constexpr (BMCWEB_REDFISH_POST_TO_OLD_UPDATESERVICE)
    {
        // See note about later on in this file about why this is neccesary
        // This is "Wrong" per the standard, but is done temporarily to
        // avoid noise in failing tests as people transition to having this
        // option disabled
        if (!asyncResp->res.getHeaderValue("Allow").empty())
        {
            asyncResp->res.clearHeader(boost::beast::http::field::allow);
        }
        asyncResp->res.addHeader(boost::beast::http::field::allow,
                                 "GET, PATCH, HEAD");
    }
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"] = {
            {"@odata.type", "#NvidiaUpdateService.v1_2_0.NvidiaUpdateService"},
            {"PersistentStorage",
             {{"@odata.id",
               "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage"}}},
            {"MultipartHttpPushUriOptions",
             {{"UpdateOptionSupport", [&]() {
                   if constexpr (BMCWEB_NVIDIA_OEM_FW_UPDATE_STAGING)
                   {
                       return std::vector<std::string>{"StageAndActivate",
                                                       "StageOnly"};
                   }
                   else
                   {
                       return std::vector<std::string>{"StageAndActivate"};
                   }
               }()}}}};
        debug_token::getErasePolicy(
            [asyncResp](const std::optional<bool>& erasePolicy) {
                if (erasePolicy)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["AutomaticDebugTokenErased"] =
                        *erasePolicy;
                }
                else
                {
                    messages::internalError(asyncResp->res);
                }
            });
    }

    auto getUpdateStatus = std::make_shared<BMCStatusAsyncResp>(asyncResp);
    crow::connections::systemBus->async_method_call(
        [asyncResp, getUpdateStatus](
            const boost::system::error_code& errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                BMCWEB_LOG_ERROR("error msg = ", errorCode.message());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                getUpdateStatus->pldm_serviceStatus = false;
                return;
            }
            getUpdateStatus->pldm_serviceStatus = true;

            // Ensure we only got one service back
            if (objInfo.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid Object Size ", objInfo.size());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code& ec,
                            GetManagedPropertyType& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("error_code = {}", ec);
                        BMCWEB_LOG_ERROR("error msg = ", ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (auto& propertyMap : resp)
                    {
                        if (propertyMap.first == "Targets")
                        {
                            auto* targets = std::get_if<
                                std::vector<sdbusplus::message::object_path>>(
                                &propertyMap.second);
                            if (targets)
                            {
                                std::vector<std::string> pushURITargets;
                                for (auto& target : *targets)
                                {
                                    std::string firmwareId = target.filename();
                                    if (firmwareId.empty())
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Unable to parse firmware ID");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    pushURITargets.push_back(
                                        "/redfish/v1/UpdateService/FirmwareInventory/" +
                                        firmwareId);
                                }
                                asyncResp->res.jsonValue["HttpPushUriTargets"] =
                                    pushURITargets;
                            }
                        }
                    }
                    return;
                },
                objInfo[0].first, "/xyz/openbmc_project/software",
                "org.freedesktop.DBus.Properties", "GetAll",
                "xyz.openbmc_project.Software.UpdatePolicy");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software",
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});

    crow::connections::systemBus->async_method_call(
        [getUpdateStatus](
            boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
            getUpdateStatus->mctp_serviceStatus = !(ec || subtree.empty());
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/mctp/0", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.MCTP.Endpoint"});

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.BMC",
        "/xyz/openbmc_project/state/bmc0", "xyz.openbmc_project.State.BMC",
        "CurrentBMCState",
        [getUpdateStatus](const boost::system::error_code& ec,
                          const std::string& bmcState) mutable {
            if (ec)
            {
                return;
            }

            getUpdateStatus->bmcStateString = bmcState;
            return;
        });

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
        "/xyz/openbmc_project/state/host0", "xyz.openbmc_project.State.Host",
        "CurrentHostState",
        [getUpdateStatus](const boost::system::error_code& ec,
                          const std::string& hostState) mutable {
            if (ec)
            {
                return;
            }

            getUpdateStatus->hostStateString = hostState;
            return;
        });
}

/**
 * @brief update oem action with ComputeDigest for devices which supports hash
 * compute
 *
 * @param[in] asyncResp
 * @param[in] swId
 */
inline void updateOemActionComputeDigest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, swId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                // hash compute interface is not applicable, ignore for the
                // device
                return;
            }
            for (const auto& obj : subtree)
            {
                sdbusplus::message::object_path hashPath(obj.first);
                std::string hashId = hashPath.filename();
                if (hashId == swId)
                {
                    std::string computeDigestTarget =
                        "/redfish/v1/UpdateService/FirmwareInventory/" + swId +
                        "/Actions/Oem/NvidiaSoftwareInventory.ComputeDigest";
                    asyncResp->res
                        .jsonValue["Actions"]["Oem"]
                                  ["#NvidiaSoftwareInventory.ComputeDigest"] = {
                        {"target", computeDigestTarget}};
                    break;
                }
            }
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief compute digest method handler invoke retimer hash computation
 *
 * @param[in] req - http request
 * @param[in] asyncResp - http response
 * @param[in] hashComputeObjPath - hash object path
 * @param[in] swId - software id
 */
inline void computeDigest(const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& hashComputeObjPath,
                          const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, req, hashComputeObjPath, swId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to GetObject for ComputeDigest: {}",
                                 ec);
                messages::internalError(asyncResp->res);
                return;
            }
            // Ensure we only got one service back
            if (objInfo.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid Object Size {}", objInfo.size());
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string hashComputeService = objInfo[0].first;
            unsigned retimerId = 0;
            try
            {
                // TODO this needs moved to from_chars
                retimerId = static_cast<unsigned>(
                    std::stoul(swId.substr(swId.rfind('_') + 1)));
            }
            catch (const std::exception& e)
            {
                BMCWEB_LOG_ERROR("Error while parsing retimer Id: {}",
                                 e.what());
                messages::internalError(asyncResp->res);
                return;
            }
            // callback to reset hash compute state for timeout scenario
            auto timeoutCallback =
                [](const std::string_view state, size_t index) {
                    nlohmann::json message{};
                    if (state == "Started")
                    {
                        message = messages::taskStarted(std::to_string(index));
                    }
                    else if (state == "Aborted")
                    {
                        computeDigestInProgress = false;
                        message = messages::taskAborted(std::to_string(index));
                    }
                    return message;
                };
            // create a task to wait for the hash digest property changed signal
            std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
                [hashComputeObjPath, hashComputeService](
                    const boost::system::error_code& ec1,
                    sdbusplus::message::message& msg,
                    const std::shared_ptr<task::TaskData>& taskData) {
                    if (ec1)
                    {
                        if (ec1 != boost::asio::error::operation_aborted)
                        {
                            taskData->state = "Aborted";
                            taskData->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    "NvidiaSoftwareInventory.ComputeDigest",
                                    ec1.message()));
                            taskData->finishTask();
                        }
                        computeDigestInProgress = false;
                        return task::completed;
                    }

                    std::string interface;
                    boost::container::flat_map<std::string,
                                               dbus::utility::DbusVariantType>
                        propertiesList;

                    msg.read(interface, propertiesList);
                    if (interface == hashComputeInterface)
                    {
                        auto it = propertiesList.find("Digest");
                        if (it == propertiesList.end())
                        {
                            BMCWEB_LOG_ERROR(
                                "Signal doesn't have Digest value");
                            return !task::completed;
                        }
                        auto* value = std::get_if<std::string>(&(it->second));
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Digest value is not a string");
                            return !task::completed;
                        }

                        if (!(value->empty()))
                        {
                            std::string hashDigestValue = *value;
                            crow::connections::systemBus->async_method_call(
                                [taskData, hashDigestValue](
                                    const boost::system::error_code& ec2,
                                    const std::variant<std::string>& property) {
                                    if (ec2)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "DBUS response error for Algorithm");
                                        taskData->state = "Exception";
                                        taskData->messages.emplace_back(
                                            messages::taskAborted(
                                                std::to_string(
                                                    taskData->index)));
                                        return;
                                    }
                                    const std::string* hashAlgoValue =
                                        std::get_if<std::string>(&property);
                                    if (hashAlgoValue == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Null value returned for Algorithm");
                                        taskData->state = "Exception";
                                        taskData->messages.emplace_back(
                                            messages::taskAborted(
                                                std::to_string(
                                                    taskData->index)));
                                        return;
                                    }

                                    nlohmann::json jsonResponse;
                                    jsonResponse["FirmwareDigest"] =
                                        hashDigestValue;
                                    jsonResponse
                                        ["FirmwareDigestHashingAlgorithm"] =
                                            *hashAlgoValue;
                                    taskData->taskResponse
                                        .emplace<nlohmann::json>(jsonResponse);
                                    std::string location =
                                        "Location: /redfish/v1/TaskService/Tasks/" +
                                        std::to_string(taskData->index) +
                                        "/Monitor";
                                    taskData->payload->httpHeaders.emplace_back(
                                        std::move(location));
                                    taskData->state = "Completed";
                                    taskData->percentComplete = 100;
                                    taskData->messages.emplace_back(
                                        messages::taskCompletedOK(
                                            std::to_string(taskData->index)));
                                    taskData->finishTask();
                                },
                                hashComputeService, hashComputeObjPath,
                                "org.freedesktop.DBus.Properties", "Get",
                                hashComputeInterface, "Algorithm");
                            computeDigestInProgress = false;
                            return task::completed;
                        }

                        BMCWEB_LOG_ERROR("GetHash failed. Digest is empty.");
                        taskData->state = "Exception";
                        taskData->messages.emplace_back(
                            messages::resourceErrorsDetectedFormatError(
                                "NvidiaSoftwareInventory.ComputeDigest",
                                "Hash Computation Failed"));
                        taskData->finishTask();
                        computeDigestInProgress = false;
                        return task::completed;
                    }
                    return !task::completed;
                },
                "type='signal',member='PropertiesChanged',"
                "interface='org.freedesktop.DBus.Properties',"
                "path='" +
                    hashComputeObjPath + "',",
                timeoutCallback);
            task->startTimer(std::chrono::seconds(retimerHashMaxTimeSec));
            task->populateResp(asyncResp->res);
            task->payload.emplace(req);
            computeDigestInProgress = true;
            crow::connections::systemBus->async_method_call(
                [task](const boost::system::error_code& ec3) {
                    if (ec3)
                    {
                        BMCWEB_LOG_ERROR("Failed to ComputeDigest: {}", ec3);
                        task->state = "Aborted";
                        task->messages.emplace_back(
                            messages::resourceErrorsDetectedFormatError(
                                "NvidiaSoftwareInventory.ComputeDigest",
                                ec3.message()));
                        task->finishTask();
                        computeDigestInProgress = false;
                        return;
                    }
                },
                hashComputeService, hashComputeObjPath, hashComputeInterface,
                "GetHash", retimerId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", hashComputeObjPath,
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief post handler for compute digest method
 *
 * @param req
 * @param asyncResp
 * @param swId
 */
inline void handlePostComputeDigest(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, swId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                messages::resourceNotFound(
                    asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest",
                    swId);
                BMCWEB_LOG_ERROR("Invalid object path: {}", ec);
                return;
            }
            for (const auto& obj : subtree)
            {
                sdbusplus::message::object_path hashPath(obj.first);
                std::string hashId = hashPath.filename();
                if (hashId == swId)
                {
                    computeDigest(req, asyncResp, hashPath, swId);
                    return;
                }
            }
            messages::resourceNotFound(
                asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest", swId);
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief Get allowable value for particular firmware inventory
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and returns the allowable value if exists in the collection
 *
 * @param[in] inventoryPathIn - firmware inventory path.
 * @returns Pair of boolean value if the allowable value exists
 * and the object of AllowableValue who contains inventory path
 * and assigned to its MCTP EID.
 */
inline std::pair<bool, CommitImageValueEntry> getAllowableValue(
    const std::string_view inventoryPathIn)
{
    std::pair<bool, CommitImageValueEntry> result;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    std::vector<CommitImageValueEntry>::iterator it =
        find(allowableValues.begin(), allowableValues.end(),
             static_cast<std::string>(inventoryPathIn));

    if (it != allowableValues.end())
    {
        result.second = *it;
        result.first = true;
    }
    else
    {
        result.first = false;
    }

    return result;
}

/**
 * @brief Check whether firmware inventory is allowable
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and check if the firmware inventory is in this collection
 *
 * @param[in] inventoryPathIn - firmware inventory path.
 * @returns boolean value indicates whether firmware inventory
 * is allowable.
 */
inline bool isInventoryAllowableValue(const std::string_view inventoryPathIn)
{
    bool isAllowable = false;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    std::vector<CommitImageValueEntry>::iterator it =
        find(allowableValues.begin(), allowableValues.end(),
             static_cast<std::string>(inventoryPathIn));

    isAllowable = it != allowableValues.end();

    return isAllowable;
}

/**
 * @brief Update parameters for GET Method CommitImageInfo
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void updateParametersForCommitImageInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>>&
        subtree)
{
    asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array();
    nlohmann::json& parameters = asyncResp->res.jsonValue["Parameters"];

    nlohmann::json parameterTargets;
    parameterTargets["Name"] = "Targets";
    parameterTargets["Required"] = false;
    parameterTargets["DataType"] = "StringArray";
    parameterTargets["AllowableValues"] = nlohmann::json::array();

    nlohmann::json& allowableValues = parameterTargets["AllowableValues"];

    for (const auto& obj : subtree)
    {
        sdbusplus::message::object_path path(obj.first);
        std::string fwId = path.filename();
        if (fwId.empty())
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_DEBUG("Cannot parse firmware ID");
            return;
        }

        if (isInventoryAllowableValue(obj.first))
        {
            allowableValues.push_back(
                "/redfish/v1/UpdateService/FirmwareInventory/" + fwId);
        }
    }

    parameters.push_back(parameterTargets);
}

/**
 * @brief Handles request POST
 * The function triggers Commit Image action
 * for the list of delivered in the body of request
 * firmware inventories
 *
 * @param resp Async HTTP response.
 * @param asyncResp Pointer to object holding response data
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void handleCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>>&
        subtree)
{
    std::optional<std::vector<std::string>> targets;

    if (!json_util::readJsonAction(req, asyncResp->res, "Targets", targets))
    {
        return;
    }

    bool hasTargets = false;

    if (targets && !targets.value().empty())
    {
        hasTargets = true;
    }

    UuidToUriMap targetUuidInventoryUriMap = {};

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        for (auto& target : targetsCollection)
        {
            sdbusplus::message::object_path objectPath(target);
            std::string inventoryPathIn =
                "/xyz/openbmc_project/software/" + objectPath.filename();
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(inventoryPathIn);
            if (result.first)
            {
                targetUuidInventoryUriMap[result.second.uuid] =
                    result.second.inventoryUri;
            }
            else
            {
                BMCWEB_LOG_DEBUG(
                    "Cannot find firmware inventory in allowable values");
                boost::urls::url_view targetURL(target);
                messages::resourceMissingAtURI(asyncResp->res, targetURL);
            }
        }
    }
    else
    {
        for (const auto& obj : subtree)
        {
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(obj.first);

            if (result.first)
            {
                targetUuidInventoryUriMap[result.second.uuid] =
                    result.second.inventoryUri;
            }
        }
    }

    auto initBackgroundCopyCallback =
        [req, asyncResp]([[maybe_unused]] const UUID& uuid, const EID eid,
                         const URI& inventoryUri) mutable {
            BMCWEB_LOG_DEBUG("Run CommitImage operation for EID {}, UUID {}",
                             eid, uuid);
            initBackgroundCopy(req, asyncResp, eid, inventoryUri);
        };

    auto errorCallback =
        [req, asyncResp]([[maybe_unused]] const std::string& desc,
                         [[maybe_unused]] const std::string& errMsg) mutable {
            BMCWEB_LOG_ERROR("The CommitImage operation failed: {}, {}", desc,
                             errMsg);
            messages::internalError(asyncResp->res);
        };

    retrieveEidFromMctpServices(targetUuidInventoryUriMap,
                                initBackgroundCopyCallback, errorCallback);
}

inline void extendSoftwareInventoryGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& objectPath,
    const std::shared_ptr<std::string>& swId)
{
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if constexpr (BMCWEB_NVIDIA_OEM_FW_UPDATE_STAGING)
        {
            fw_util::getFWSlotInformation(asyncResp, objectPath);
        }

        updateOemActionComputeDigest(asyncResp, *swId);
    }
}

/**
 * @brief POST handler for SSH public key exchange - user and remote server
 * authentication.
 *
 * @param app
 *
 * @return None
 */
inline nlohmann::json extendedInfoSuccessMsg(const std::string& msg,
                                             const std::string& arg)
{
    return nlohmann::json{{"@odata.type", "#Message.v1_1_1.Message"},
                          {"Message", msg},
                          {"MessageArgs", {arg}}};
}

inline void requestRoutesUpdateServicePublicKeyExchange(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string remoteServerIP;
            std::string remoteServerKeyString; // "<type> <key>"

            BMCWEB_LOG_DEBUG("Enter UpdateService.PublicKeyExchange doPost");

            if (!json_util::readJsonAction(
                    req, asyncResp->res, "RemoteServerIP", remoteServerIP,
                    "RemoteServerKeyString", remoteServerKeyString) &&
                (remoteServerIP.empty() || remoteServerKeyString.empty()))
            {
                std::string emptyprops;
                if (remoteServerIP.empty())
                {
                    emptyprops += "RemoteServerIP ";
                }
                if (remoteServerKeyString.empty())
                {
                    emptyprops += "RemoteServerKeyString ";
                }
                messages::createFailedMissingReqProperties(asyncResp->res,
                                                           emptyprops);
                BMCWEB_LOG_DEBUG("Missing {}", emptyprops);
                return;
            }

            BMCWEB_LOG_DEBUG("RemoteServerIP: {} RemoteServerKeyString: {}",
                             remoteServerIP, remoteServerKeyString);

            // Verify remoteServerKeyString matches the pattern "<type> <key>"
            std::string remoteServerKeyStringPattern = R"(\S+\s+\S+)";
            std::regex pattern(remoteServerKeyStringPattern);
            if (!std::regex_match(remoteServerKeyString, pattern))
            {
                // Invalid format, return an error message
                messages::actionParameterValueTypeError(
                    asyncResp->res, remoteServerKeyString,
                    "RemoteServerKeyString", "UpdateService.PublicKeyExchange");
                BMCWEB_LOG_DEBUG("Invalid RemoteServerKeyString format");
                return;
            }

            // Call SCP service
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code& ec) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                         ec.message());
                        return;
                    }

                    crow::connections::systemBus->async_method_call(
                        [asyncResp](const boost::system::error_code& ec2,
                                    const std::string& selfPublicKeyStr) {
                            if (ec2 || selfPublicKeyStr.empty())
                            {
                                messages::internalError(asyncResp->res);
                                BMCWEB_LOG_ERROR(
                                    "error_code = {} error msg = {}", ec2,
                                    ec2.message());
                                return;
                            }

                            // Create a JSON object with the additional
                            // information
                            std::string keyMsg =
                                "Please add the following public key info to "
                                "~/.ssh/authorized_keys on the remote server";
                            std::string keyInfo =
                                selfPublicKeyStr + " root@dpu-bmc";

                            asyncResp->res
                                .jsonValue[messages::messageAnnotation] =
                                nlohmann::json::array();
                            asyncResp->res
                                .jsonValue[messages::messageAnnotation]
                                .push_back(
                                    extendedInfoSuccessMsg(keyMsg, keyInfo));
                            messages::success(asyncResp->res);
                            BMCWEB_LOG_DEBUG(
                                "Call to PublicKeyExchange succeeded {}",
                                selfPublicKeyStr);
                        },
                        "xyz.openbmc_project.Software.Download",
                        "/xyz/openbmc_project/software",
                        "xyz.openbmc_project.Common.SCP",
                        "GenerateSelfKeyPair");
                },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.SCP", "AddRemoteServerPublicKey",
                remoteServerIP, remoteServerKeyString);
        });
}

/**
 * @brief POST handler for adding remote server SSH public key
 *
 * @param app
 *
 * @return None
 */
inline void requestRoutesUpdateServiceRevokeAllRemoteServerPublicKeys(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string remoteServerIP;

            BMCWEB_LOG_DEBUG(
                "Enter UpdateService.RevokeAllRemoteServerPublicKeys doPost");

            if (!json_util::readJsonAction(req, asyncResp->res,
                                           "RemoteServerIP", remoteServerIP) &&
                remoteServerIP.empty())
            {
                messages::createFailedMissingReqProperties(asyncResp->res,
                                                           "RemoteServerIP");
                BMCWEB_LOG_DEBUG("Missing RemoteServerIP");
                return;
            }

            BMCWEB_LOG_DEBUG("RemoteServerIP: {}", remoteServerIP);

            // Call SCP service
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code& ec) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                         ec.message());
                    }
                    else
                    {
                        messages::success(asyncResp->res);
                        BMCWEB_LOG_DEBUG(
                            "Call to RevokeAllRemoteServerPublicKeys succeeded");
                    }
                },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.SCP",
                "RevokeAllRemoteServerPublicKeys", remoteServerIP);
        });
}

/**
 * @brief retry handler of the aggregation post request.
 *
 * @param[in] respCode HTTP response status code
 *
 * @return None
 */
inline boost::system::error_code aggregationPostRetryHandler(
    unsigned int respCode)
{
    // Allow all response codes because we want to surface any satellite
    // issue to the client
    BMCWEB_LOG_DEBUG(
        "Received {} response of the firmware update from satellite", respCode);
    return boost::system::errc::make_error_code(boost::system::errc::success);
}

/**
 * @brief process the response from satellite BMC.
 *
 * @param[in] prefix the prefix of the url
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] resp Pointer to object holding response data from satellite
 * BMC
 *
 * @return None
 */
inline void handleSatBMCResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, crow::Response& resp)
{
    // 429 and 502 mean we didn't actually send the request so don't
    // overwrite the response headers in that case
    if ((resp.result() == boost::beast::http::status::too_many_requests) ||
        (resp.result() == boost::beast::http::status::bad_gateway))
    {
        asyncResp->res.result(resp.result());
        return;
    }

    if (resp.resultInt() !=
        static_cast<unsigned>(boost::beast::http::status::accepted))
    {
        asyncResp->res.result(resp.result());
        asyncResp->res.copyBody(resp);
        return;
    }

    // The resp will not have a json component
    // We need to create a json from resp's stringResponse
    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (bmcweb::asciiIEquals(contentType, "application/json") ||
        bmcweb::asciiIEquals(contentType, "application/json; charset=utf-8"))
    {
        nlohmann::json jsonVal =
            nlohmann::json::parse(*resp.body(), nullptr, false);
        if (jsonVal.is_discarded())
        {
            BMCWEB_LOG_ERROR("Error parsing satellite response as JSON");

            // Notify the user if doing so won't overwrite a valid response
            if (asyncResp->res.resultInt() !=
                static_cast<unsigned>(boost::beast::http::status::ok))
            {
                messages::operationFailed(asyncResp->res);
            }
            return;
        }
        BMCWEB_LOG_DEBUG("Successfully parsed satellite response");
        auto* object = jsonVal.get_ptr<nlohmann::json::object_t*>();
        if (object == nullptr)
        {
            BMCWEB_LOG_ERROR("Parsed JSON was not an object?");
            return;
        }

        std::string rfaPrefix = std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX);
        for (std::pair<const std::string, nlohmann::json>& prop : *object)
        {
            // only prefix fix-up on Task response.
            std::string* strValue = prop.second.get_ptr<std::string*>();
            if (strValue == nullptr)
            {
                BMCWEB_LOG_CRITICAL("Item is not a string");
                continue;
            }
            if (prop.first == "@odata.id")
            {
                std::string file = std::filesystem::path(*strValue).filename();
                std::string path =
                    std::filesystem::path(*strValue).parent_path();
                std::string temp = file;

                file = rfaPrefix;
                file += "_";
                file += temp;
                path += "/";
                // add prefix on odata.id property.
                prop.second = path + file;
            }
            if (prop.first == "Id")
            {
                std::string file = std::filesystem::path(*strValue).filename();
                // add prefix on Id property.
                prop.second = rfaPrefix;
                prop.second += "_";
                prop.second += file;
            }
            else
            {
                continue;
            }
        }
        asyncResp->res.result(resp.result());
        asyncResp->res.jsonValue = std::move(jsonVal);
    }
}

inline crow::ConnectionPolicy getPostAggregationPolicy()
{
    return {.maxRetryAttempts = 0,
            .requestByteLimit = firmwareImageLimitBytes,
            .maxConnections = 20,
            .retryPolicyAction = "TerminateAfterRetries",
            .retryIntervalSecs = std::chrono::seconds(0),
            .invalidResp = aggregationPostRetryHandler};
}

/**
 * @brief forward Commit Image Post Request to satBMC.
 *
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] ec Error code
 * @param[in] satelliteInfo satellite BMC information
 *
 * @return None
 */
inline void forwardCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat =
        satelliteInfo.find(std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX));
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satBMC is not found");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(handleSatBMCResponse, asyncResp);

    std::string data = req.body();
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());

    client.sendDataWithCallback(
        std::move(data), url, ensuressl::VerifyCertificate::Verify,
        req.fields(), boost::beast::http::verb::post, cb);
}

/**
 * @brief the response handler of CommitImage Post
 * the function will examine the targets of the request and send out
 * the request to the satellite BMC if the remote targets are present.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 *
 * @return return true to pass request to the local. otherwise, don't pass.
 */

inline bool handleSatBMCCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::optional<std::vector<std::string>> targets;

    if (!json_util::readJsonAction(req, asyncResp->res, "Targets", targets))
    {
        messages::createFailedMissingReqProperties(asyncResp->res, "Targets");
        BMCWEB_LOG_ERROR("Missing Targets of OemCommitImage API");
        return false;
    }

    bool hasTargets = false;

    if (targets && !targets.value().empty())
    {
        hasTargets = true;
    }

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        std::string rfaPrefix(BMCWEB_REDFISH_AGGREGATION_PREFIX);
        rfaPrefix += "_";

        bool prefix = false;
        bool noPrefix = false;
        for (auto& target : targetsCollection)
        {
            std::string file = std::filesystem::path(target).filename();
            if (file.starts_with(rfaPrefix))
            {
                prefix = true;
            }
            else
            {
                noPrefix = true;
            }
        }

        if (prefix && !noPrefix)
        {
            // targets with the prefix included only.
            RedfishAggregator::getSatelliteConfigs(
                std::bind_front(forwardCommitImagePost, req, asyncResp));

            // don't pass the request to the local
            return false;
        }
        if (prefix && noPrefix)
        {
            // drop the request with mixed targets.
            boost::urls::url_view targetURL("Target");
            messages::invalidObject(asyncResp->res, targetURL);
            return false;
        }
    }
    else
    {
        RedfishAggregator::getSatelliteConfigs(
            std::bind_front(forwardCommitImagePost, req, asyncResp));
        // forward the request with empty target.
    }
    return true;
}

/**
 * @brief  callback handler of JSON array object
 * the common function to get the JSON array object, espeically for
 * the response of CommitImageActionInfo from satBMC.
 *
 * @param[in] object JSON object
 * @param[in] name JSON name
 * @param[in] cb  The callback function
 *
 * @return None
 */
inline void getArrayObject(nlohmann::json::object_t* object,
                           const std::string_view name,
                           const std::function<void(nlohmann::json&)>& cb)
{
    for (std::pair<const std::string, nlohmann::json>& item : *object)
    {
        if (item.first != name)
        {
            continue;
        }
        auto* array = item.second.get_ptr<nlohmann::json::array_t*>();
        if (array == nullptr)
        {
            continue;
        }
        for (nlohmann::json& elm : *array)
        {
            cb(elm);
        }
    }
}

/**
 * @brief The response handler of CommitImageActionInfo from satBMC
 * aggregate the allowable values from the response of CommitImageActionInfo
 * if the response is successful.
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] resp  HTTP response of satBMC
 *
 * @return None
 */
inline void commitImageActionInfoResp(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, crow::Response& resp)
{
    // Failed to get ActionInfo because of the error response
    // just return without any further processing for the aggregation.
    if ((resp.result() == boost::beast::http::status::too_many_requests) ||
        (resp.result() == boost::beast::http::status::bad_gateway))
    {
        return;
    }

    // The resp will not have a json component
    // We need to create a json from resp's stringResponse
    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (bmcweb::asciiIEquals(contentType, "application/json") ||
        bmcweb::asciiIEquals(contentType, "application/json; charset=utf-8"))
    {
        nlohmann::json jsonVal =
            nlohmann::json::parse(*resp.body(), nullptr, false);
        if (jsonVal.is_discarded())
        {
            return;
        }
        nlohmann::json::object_t* object =
            jsonVal.get_ptr<nlohmann::json::object_t*>();
        if (object == nullptr)
        {
            BMCWEB_LOG_ERROR("Parsed JSON was not an object?");
            return;
        }

        auto cb = [asyncResp](nlohmann::json& item) mutable {
            auto allowValueCb = [asyncResp](nlohmann::json& itemInCb) mutable {
                auto* str = itemInCb.get_ptr<std::string*>();
                if (str == nullptr)
                {
                    BMCWEB_LOG_CRITICAL("Item is not a string");
                    return;
                }
                nlohmann::json& allowableValues =
                    asyncResp->res
                        .jsonValue["Parameters"][0]["AllowableValues"];

                allowableValues.push_back(*str);
            };

            auto* nestedObject = item.get_ptr<nlohmann::json::object_t*>();
            if (nestedObject == nullptr)
            {
                BMCWEB_LOG_CRITICAL("Nested object is null");
                return;
            }
            getArrayObject(nestedObject, std::string("AllowableValues"),
                           allowValueCb);
        };
        getArrayObject(object, std::string("Parameters"), cb);
    }
}

/**
 * @brief forward Commit Image Action Info request to satBMC.
 * the function will send the request to satBMC to get the CommitImageActionInfo
 * if the satellie BMC is available.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] ec Error code
 * @param[in] satelliteInfo satellite BMC information
 *
 * @return None
 */
inline void forwardCommitImageActionInfo(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    // Something went wrong while querying dbus
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat =
        satelliteInfo.find(std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX));
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(commitImageActionInfoResp, asyncResp);

    std::string data;
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());

    client.sendDataWithCallback(
        std::move(data), url, ensuressl::VerifyCertificate::Verify,
        req.fields(), boost::beast::http::verb::get, cb);
}

/**
 * @brief Register Web Api endpoints for Commit Image functionality
 *
 * @return None
 */
inline void requestRoutesUpdateServiceCommitImage(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_2_0.ActionInfo";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo";
            asyncResp->res.jsonValue["Name"] = "CommitImage Action Info";
            asyncResp->res.jsonValue["Id"] = "CommitImageActionInfo";

            crow::connections::systemBus->async_method_call(
                [asyncResp{asyncResp}, req](
                    const boost::system::error_code& ec,
                    const std::vector<std::pair<
                        std::string,
                        std::vector<
                            std::pair<std::string, std::vector<std::string>>>>>&
                        subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    updateParametersForCommitImageInfo(asyncResp, subtree);
                    if constexpr (BMCWEB_REDFISH_AGGREGATION)
                    {
                        RedfishAggregator::getSatelliteConfigs(std::bind_front(
                            forwardCommitImageActionInfo, req, asyncResp));
                    }
                },
                // Note that only firmware levels associated with a device
                // are stored under /xyz/openbmc_project/software therefore
                // to ensure only real FirmwareInventory items are returned,
                // this full object path must be used here as input to
                // mapper
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/software", static_cast<int32_t>(0),
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
        });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([](const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            BMCWEB_LOG_DEBUG("doPost...");

            if constexpr (BMCWEB_REDFISH_AGGREGATION)
            {
                if (!handleSatBMCCommitImagePost(req, asyncResp))
                {
                    return;
                }
            }

            if (fwUpdateInProgress)
            {
                redfish::messages::updateInProgressMsg(
                    asyncResp->res,
                    "Retry the operation once firmware update operation is complete.");

                // don't copy the image, update already in progress.
                BMCWEB_LOG_ERROR(
                    "Cannot execute commit image. Update firmware is in progress.");

                return;
            }

            crow::connections::systemBus->async_method_call(
                [req, asyncResp{asyncResp}](
                    const boost::system::error_code& ec,
                    const std::vector<std::pair<
                        std::string,
                        std::vector<
                            std::pair<std::string, std::vector<std::string>>>>>&
                        subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    handleCommitImagePost(req, asyncResp, subtree);
                },
                // Note that only firmware levels associated with a device
                // are stored under /xyz/openbmc_project/software therefore
                // to ensure only real FirmwareInventory items are returned,
                // this full object path must be used here as input to
                // mapper
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/software", static_cast<int32_t>(0),
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
        });
}

/**
 * @brief app handler for ComputeDigest action
 *
 * @param[in] app
 */
inline void requestRoutesComputeDigestPost(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/Actions/Oem/"
             "NvidiaSoftwareInventory.ComputeDigest")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& param) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            BMCWEB_LOG_DEBUG(
                "Enter NvidiaSoftwareInventory.ComputeDigest doPost");
            std::shared_ptr<std::string> swId =
                std::make_shared<std::string>(param);
            // skip input parameter validation

            // 1. Firmware update and retimer hash cannot run in parallel
            if (fwUpdateInProgress)
            {
                redfish::messages::updateInProgressMsg(
                    asyncResp->res,
                    "Retry the operation once firmware update operation is complete.");
                BMCWEB_LOG_ERROR(
                    "Cannot execute ComputeDigest. Update firmware is in progress.");

                return;
            }
            // 2. Only one compute hash allowed at a time due to FPGA limitation
            if (computeDigestInProgress)
            {
                redfish::messages::resourceErrorsDetectedFormatError(
                    asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest",
                    "Another ComputeDigest operation is in progress");
                BMCWEB_LOG_ERROR(
                    "Cannot execute ComputeDigest. Another ComputeDigest is in progress.");
                return;
            }
            handlePostComputeDigest(req, asyncResp, *swId);
            BMCWEB_LOG_DEBUG("Exit NvidiaUpdateService.ComputeDigest doPost");
        });
}

inline void handleUpdateServiceSoftwareInventoryGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string searchPath = "/xyz/openbmc_project/inventory_software/";
    std::shared_ptr<std::string> swId = std::make_shared<std::string>(param);

    crow::connections::systemBus->async_method_call(
        [asyncResp, swId, searchPath](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            BMCWEB_LOG_DEBUG("doGet callback...");
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Ensure we find our input swId, otherwise return an
            // error
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     obj : subtree)
            {
                const std::string& path = obj.first;
                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != *swId)
                {
                    continue;
                }

                if (obj.second.empty())
                {
                    continue;
                }

                asyncResp->res.jsonValue["Id"] = *swId;
                asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
                }
                if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                {
                    asyncResp->res.jsonValue["Status"]["Conditions"] =
                        nlohmann::json::array();
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, swId, path, searchPath](
                        const boost::system::error_code& errorCode,
                        const boost::container::flat_map<
                            std::string, dbus::utility::DbusVariantType>&
                            propertiesList) {
                        if (errorCode)
                        {
                            BMCWEB_LOG_DEBUG("properties not found ");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const auto& property : propertiesList)
                        {
                            if (property.first == "Manufacturer")
                            {
                                const std::string* manufacturer =
                                    std::get_if<std::string>(&property.second);
                                if (manufacturer != nullptr)
                                {
                                    asyncResp->res.jsonValue["Manufacturer"] =
                                        *manufacturer;
                                }
                            }
                            else if (property.first == "Version")
                            {
                                const std::string* version =
                                    std::get_if<std::string>(&property.second);
                                if (version != nullptr)
                                {
                                    asyncResp->res.jsonValue["Version"] =
                                        *version;
                                }
                            }
                            else if (property.first == "Functional")
                            {
                                const bool* swInvFunctional =
                                    std::get_if<bool>(&property.second);
                                if (swInvFunctional != nullptr)
                                {
                                    BMCWEB_LOG_DEBUG(" Functinal {}",
                                                     *swInvFunctional);
                                    if (*swInvFunctional)
                                    {
                                        asyncResp->res
                                            .jsonValue["Status"]["State"] =
                                            "Enabled";
                                    }
                                    else
                                    {
                                        asyncResp->res
                                            .jsonValue["Status"]["State"] =
                                            "Disabled";
                                    }
                                }
                            }
                        }
                        // getRelatedItemsOthers(asyncResp, *swId, searchPath);
                        std::string mutablePath = searchPath;
                        fw_util::getFwUpdateableStatus(asyncResp, swId,
                                                       mutablePath);
                    },
                    obj.second[0].first, obj.first,
                    "org.freedesktop.DBus.Properties", "GetAll", "");
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/UpdateService/SoftwareInventory/" + *swId;
                asyncResp->res.jsonValue["@odata.type"] =
                    "#SoftwareInventory.v1_4_0.SoftwareInventory";
                asyncResp->res.jsonValue["Name"] = "Software Inventory";
                return;
            }
            // Couldn't find an object with that name.  return an error
            BMCWEB_LOG_DEBUG("Input swID {} not found!", *swId);
            messages::resourceNotFound(
                asyncResp->res, "SoftwareInventory.v1_4_0.SoftwareInventory",
                *swId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", searchPath,
        static_cast<int32_t>(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
}

inline void handleUpdateServiceFirmwareInventoryPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("doPatch...");
    std::shared_ptr<std::string> swId = std::make_shared<std::string>(param);

    std::optional<bool> writeProtected;
    if (!json_util::readJsonPatch(req, asyncResp->res, "WriteProtected",
                                  writeProtected))
    {
        return;
    }

    if (writeProtected)
    {
        crow::connections::systemBus->async_method_call(
            [asyncResp, swId, writeProtected](
                const boost::system::error_code& ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
                if (ec)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const std::pair<
                         std::string,
                         std::vector<
                             std::pair<std::string, std::vector<std::string>>>>&
                         obj : subtree)
                {
                    const std::string& path = obj.first;
                    sdbusplus::message::object_path objPath(path);
                    if (objPath.filename() != *swId)
                    {
                        continue;
                    }

                    if (obj.second.empty())
                    {
                        continue;
                    }
                    fw_util::patchFwWriteProtectedStatus(
                        asyncResp, swId, obj.second[0].first, *writeProtected);

                    return;
                }
                // Couldn't find an object with that name.  return
                // an error
                BMCWEB_LOG_DEBUG("Input swID {} not found!", *swId);
                messages::resourceNotFound(
                    asyncResp->res,
                    "SoftwareInventory.v1_4_0.SoftwareInventory", *swId);
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/software/", static_cast<int32_t>(0),
            std::array<const char*, 1>{
                "xyz.openbmc_project.Software.Settings"});
    }
}

inline void handleUpdateServiceSoftwareInventoryCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#SoftwareInventoryCollection.SoftwareInventoryCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/UpdateService/SoftwareInventory";
    asyncResp->res.jsonValue["Name"] = "Software Inventory Collection";

    crow::connections::systemBus->async_method_call(
        [asyncResp](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec == boost::system::errc::io_error)
            {
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
            asyncResp->res.jsonValue["Members@odata.count"] = 0;

            for (const auto& obj : subtree)
            {
                sdbusplus::message::object_path path(obj.first);
                std::string swId = path.filename();
                if (swId.empty())
                {
                    messages::internalError(asyncResp->res);
                    BMCWEB_LOG_DEBUG("Can't parse software ID!!");
                    return;
                }

                nlohmann::json& members = asyncResp->res.jsonValue["Members"];
                members.push_back(
                    {{"@odata.id",
                      "/redfish/v1/UpdateService/SoftwareInventory/" + swId}});
                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();
            }
        },
        // Note that only firmware levels associated with a device
        // are stored under /xyz/openbmc_project/inventory_software
        // therefore to ensure only real SoftwareInventory items are
        // returned, this full object path must be used here as input to
        // mapper
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory_software", static_cast<int32_t>(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
}

inline void handleUpdateServicePatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("doPatch...");

    std::optional<nlohmann::json> pushUriOptions;
    std::optional<std::vector<std::string>> imgTargets;
    std::optional<bool> erasePolicy;
    if (!json_util::readJsonPatch(
            req, asyncResp->res, "HttpPushUriTargets", imgTargets,
            "Oem/Nvidia/AutomaticDebugTokenErased", erasePolicy))
    {
        BMCWEB_LOG_ERROR("UpdateService doPatch: Invalid request body");
        return;
    }

    if (erasePolicy)
    {
        debug_token::setErasePolicy(asyncResp, *erasePolicy);
    }

    if (imgTargets)
    {
        crow::connections::systemBus->async_method_call(
            [asyncResp, uriTargets{*imgTargets}](
                const boost::system::error_code& ec,
                const std::vector<std::string>& swInvPaths) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("D-Bus responses error: {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }

                std::vector<sdbusplus::message::object_path>
                    httpPushUriTargets = {};
                // validate TargetUris if entries are present
                if (!uriTargets.empty())
                {
                    std::vector<std::string> invalidTargets;
                    for (const std::string& target : uriTargets)
                    {
                        std::string compName =
                            std::filesystem::path(target).filename();
                        bool validTarget = false;
                        std::string objPath = "software/" + compName;
                        for (const std::string& path : swInvPaths)
                        {
                            std::size_t idPos = path.rfind(objPath);
                            if (idPos == std::string::npos)
                            {
                                continue;
                            }
                            std::string swId = path.substr(idPos);
                            if (swId == objPath)
                            {
                                sdbusplus::message::object_path objpath(path);
                                httpPushUriTargets.emplace_back(objpath);
                                validTarget = true;
                                break;
                            }
                        }
                        if (!validTarget)
                        {
                            invalidTargets.emplace_back(target);
                        }
                    }
                    // return HTTP400 - Bad request
                    // when none of the target filters are valid
                    if (invalidTargets.size() == uriTargets.size())
                    {
                        BMCWEB_LOG_ERROR("Targetted Device not Found!!");
                        messages::invalidObject(
                            asyncResp->res,
                            boost::urls::format("HttpPushUriTargets"));
                        return;
                    }
                    // return HTTP200 - Success with errors
                    // when there is partial valid targets
                    if (!invalidTargets.empty())
                    {
                        for (const std::string& invalidTarget : invalidTargets)
                        {
                            BMCWEB_LOG_ERROR("Invalid HttpPushUriTarget: {}",
                                             invalidTarget);
                            messages::propertyValueFormatError(
                                asyncResp->res, invalidTarget,
                                "HttpPushUriTargets");
                        }
                        asyncResp->res.result(boost::beast::http::status::ok);
                    }
                    // else all targets are valid
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, httpPushUriTargets](
                        const boost::system::error_code& errorCode,
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            objInfo) mutable {
                        if (errorCode)
                        {
                            BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                            BMCWEB_LOG_ERROR("error msg = {}",
                                             errorCode.message());
                            if (asyncResp)
                            {
                                messages::internalError(asyncResp->res);
                            }
                            return;
                        }
                        // Ensure we only got one service back
                        if (objInfo.size() != 1)
                        {
                            BMCWEB_LOG_ERROR("Invalid Object Size {}",
                                             objInfo.size());
                            if (asyncResp)
                            {
                                messages::internalError(asyncResp->res);
                            }
                            return;
                        }

                        crow::connections::systemBus->async_method_call(
                            [asyncResp](const boost::system::error_code&
                                            errCodePolicy) {
                                if (errCodePolicy)
                                {
                                    BMCWEB_LOG_ERROR("error_code = {}",
                                                     errCodePolicy);
                                    messages::internalError(asyncResp->res);
                                }
                                messages::success(asyncResp->res);
                            },
                            objInfo[0].first, "/xyz/openbmc_project/software",
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.Software.UpdatePolicy",
                            "Targets",
                            dbus::utility::DbusVariantType(httpPushUriTargets));
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject",
                    "/xyz/openbmc_project/software",
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Software.UpdatePolicy"});
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
            "/xyz/openbmc_project/software/", static_cast<int32_t>(0),
            std::array<std::string, 1>{"xyz.openbmc_project.Software.Version"});
    }
}

inline void requestRoutesNvidiaUpdateService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceSoftwareInventoryGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceSoftwareInventoryCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
        .privileges(redfish::privileges::patchUpdateService)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            handleUpdateServiceFirmwareInventoryPatch, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/")
        .privileges(redfish::privileges::patchUpdateService)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleUpdateServicePatch, std::ref(app)));
}
} // namespace redfish
