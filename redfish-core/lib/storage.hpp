/*
// Copyright (c) 2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include "bmcweb_config.h"
// #include "task.hpp"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "generated/enums/drive.hpp"
#include "generated/enums/protocol.hpp"
#include "human_sort.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <ranges>
#include <string_view>

namespace redfish
{
// task uri for long-run drive operation
static std::map<std::string, std::string> taskUri;
// drive resouce has two interfaces from Dbus.
// EM will also populate drive resource with the only one interface
const std::array<const char*, 2> driveInterface = {
    "xyz.openbmc_project.Inventory.Item.Drive",
    "xyz.openbmc_project.Nvme.Operation"};

inline void handleSystemsStorageCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#StorageCollection.StorageCollection";
    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Systems/{}/Storage", BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["Name"] = "Storage Collection";

    constexpr std::array<std::string_view, 1> interface{
        "xyz.openbmc_project.Inventory.Item.Storage"};

    collection_util::getCollectionMembers(
        asyncResp,
        boost::urls::format("/redfish/v1/Systems/{}/Storage",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME),
        interface, "/xyz/openbmc_project/inventory");
}

inline void handleStorageCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#StorageCollection.StorageCollection";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Storage";
    asyncResp->res.jsonValue["Name"] = "Storage Collection";
    constexpr std::array<std::string_view, 1> interface{
        "xyz.openbmc_project.Inventory.Item.Storage"};
    collection_util::getCollectionMembers(
        asyncResp, boost::urls::format("/redfish/v1/Storage"), interface,
        "/xyz/openbmc_project/inventory");
}

inline void requestRoutesStorageCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Storage/")
        .privileges(redfish::privileges::getStorageCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSystemsStorageCollectionGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Storage/")
        .privileges(redfish::privileges::getStorageCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleStorageCollectionGet, std::ref(app)));
}

inline void afterChassisDriveCollectionSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<HealthPopulate>& health,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& ret)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Drive mapper call error");
        messages::internalError(asyncResp->res);
        return;
    }

    nlohmann::json& driveArray = asyncResp->res.jsonValue["Drives"];
    driveArray = nlohmann::json::array();
    auto& count = asyncResp->res.jsonValue["Drives@odata.count"];
    count = 0;

    for (const auto& [path, objDict] : ret)
    {
        uint32_t num = 0;
        for (const std::string& interface : objDict.begin()->second)
        {
            if (std::find_if(driveInterface.begin(), driveInterface.end(),
                             [interface](const std::string& possible) {
                return boost::starts_with(interface, possible);
            }) != driveInterface.end())
            {
                num++;
            }
        }
        if (num != driveInterface.size())
        {
            continue;
        }

        health->inventory.insert(health->inventory.end(), path);

        nlohmann::json::object_t driveJson;
        std::string file = std::filesystem::path(path).filename();

        driveJson["@odata.id"] =
            boost::urls::format("/redfish/v1/Systems/{}/Storage/1/Drives/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, file);
        driveArray.emplace_back(std::move(driveJson));
    }
    count = driveArray.size();
}
inline void getDrives(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::shared_ptr<HealthPopulate>& health)
{
    const std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Drive"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(afterChassisDriveCollectionSubtree, asyncResp, health));
}

inline void afterSystemsStorageGetSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& storageId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("requestRoutesStorage DBUS response error");
        messages::resourceNotFound(asyncResp->res, "#Storage.v1_13_0.Storage",
                                   storageId);
        return;
    }
    auto storage = std::ranges::find_if(
        subtree,
        [&storageId](const std::pair<std::string,
                                     dbus::utility::MapperServiceMap>& object) {
        return sdbusplus::message::object_path(object.first).filename() ==
               storageId;
    });
    if (storage == subtree.end())
    {
        messages::resourceNotFound(asyncResp->res, "#Storage.v1_13_0.Storage",
                                   storageId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] = "#Storage.v1_13_0.Storage";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Systems/{}/Storage/{}",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME, storageId);
    asyncResp->res.jsonValue["Name"] = "Storage";
    asyncResp->res.jsonValue["Id"] = storageId;
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

    auto health = std::make_shared<HealthPopulate>(asyncResp);
    health->populate();

    getDrives(asyncResp, health);
    asyncResp->res.jsonValue["Controllers"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Systems/{}/Storage/{}/Controllers",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME, storageId);
}

inline void
    handleSystemsStorageGet(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& systemName,
                            const std::string& storageId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Storage"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(afterSystemsStorageGetSubtree, asyncResp, storageId));
}

inline void afterSubtree(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& storageId,
                         const boost::system::error_code& ec,
                         const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("requestRoutesStorage DBUS response error");
        messages::resourceNotFound(asyncResp->res, "#Storage.v1_13_0.Storage",
                                   storageId);
        return;
    }
    auto storage = std::ranges::find_if(
        subtree,
        [&storageId](const std::pair<std::string,
                                     dbus::utility::MapperServiceMap>& object) {
        return sdbusplus::message::object_path(object.first).filename() ==
               storageId;
    });
    if (storage == subtree.end())
    {
        messages::resourceNotFound(asyncResp->res, "#Storage.v1_13_0.Storage",
                                   storageId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] = "#Storage.v1_13_0.Storage";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Storage/{}", storageId);
    asyncResp->res.jsonValue["Name"] = "Storage";
    asyncResp->res.jsonValue["Id"] = storageId;
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

    // Storage subsystem to Storage link.
    nlohmann::json::array_t storageServices;
    nlohmann::json::object_t storageService;
    storageService["@odata.id"] =
        boost::urls::format("/redfish/v1/Systems/{}/Storage/{}",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME, storageId);
    storageServices.emplace_back(storageService);
    asyncResp->res.jsonValue["Links"]["StorageServices"] =
        std::move(storageServices);
    asyncResp->res.jsonValue["Links"]["StorageServices@odata.count"] = 1;
}

inline void
    handleStorageGet(App& app, const crow::Request& req,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& storageId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_DEBUG("requestRoutesStorage setUpRedfishRoute failed");
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Storage"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(afterSubtree, asyncResp, storageId));
}

inline void requestRoutesStorage(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Storage/<str>/")
        .privileges(redfish::privileges::getStorage)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSystemsStorageGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Storage/<str>/")
        .privileges(redfish::privileges::getStorage)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleStorageGet, std::ref(app)));
}

inline void getDriveAsset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& connectionName,
                          const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [asyncResp](const boost::system::error_code& ec,
                    const std::vector<
                        std::pair<std::string, dbus::utility::DbusVariantType>>&
                        propertiesList) {
        if (ec)
        {
            // this interface isn't necessary
            return;
        }

        const std::string* partNumber = nullptr;
        const std::string* serialNumber = nullptr;
        const std::string* manufacturer = nullptr;
        const std::string* model = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
            partNumber, "SerialNumber", serialNumber, "Manufacturer",
            manufacturer, "Model", model);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (partNumber != nullptr)
        {
            asyncResp->res.jsonValue["PartNumber"] = *partNumber;
        }

        if (serialNumber != nullptr)
        {
            asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
        }

        if (manufacturer != nullptr)
        {
            asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
        }

        if (model != nullptr)
        {
            asyncResp->res.jsonValue["Model"] = *model;
        }
    });
}

inline void getDrivePresent(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& connectionName,
                            const std::string& path)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [asyncResp, path](const boost::system::error_code& ec,
                          const bool isPresent) {
        // this interface isn't necessary, only check it if
        // we get a good return
        if (ec)
        {
            return;
        }

        if (!isPresent)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Absent";
        }
    });
}

inline void getDriveState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& connectionName,
                          const std::string& path)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.State.Drive", "Rebuilding",
        [asyncResp](const boost::system::error_code& ec, const bool updating) {
        // this interface isn't necessary, only check it
        // if we get a good return
        if (ec)
        {
            return;
        }

        // updating and disabled in the backend shouldn't be
        // able to be set at the same time, so we don't need
        // to check for the race condition of these two
        // calls
        if (updating)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Updating";
        }
    });
}

inline std::optional<drive::MediaType> convertDriveType(std::string_view type)
{
    if (type == "xyz.openbmc_project.Inventory.Item.Drive.DriveType.HDD")
    {
        return drive::MediaType::HDD;
    }
    if (type == "xyz.openbmc_project.Inventory.Item.Drive.DriveType.SSD")
    {
        return drive::MediaType::SSD;
    }
    if (type == "xyz.openbmc_project.Inventory.Item.Drive.DriveType.Unknown")
    {
        return std::nullopt;
    }

    return drive::MediaType::Invalid;
}

inline std::optional<protocol::Protocol>
    convertDriveProtocol(std::string_view proto)
{
    if (proto == "xyz.openbmc_project.Inventory.Item.Drive.DriveProtocol.SAS")
    {
        return protocol::Protocol::SAS;
    }
    if (proto == "xyz.openbmc_project.Inventory.Item.Drive.DriveProtocol.SATA")
    {
        return protocol::Protocol::SATA;
    }
    if (proto == "xyz.openbmc_project.Inventory.Item.Drive.DriveProtocol.NVMe")
    {
        return protocol::Protocol::NVMe;
    }
    if (proto == "xyz.openbmc_project.Inventory.Item.Drive.DriveProtocol.FC")
    {
        return protocol::Protocol::FC;
    }
    if (proto ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveProtocol.Unknown")
    {
        return std::nullopt;
    }

    return protocol::Protocol::Invalid;
}

inline std::optional<std::string>
    convertDriveFormFactor(const std::string& formFactor)
{
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.Drive3_5")
    {
        return "Drive3_5";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.Drive2_5")
    {
        return "Drive2_5";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_1U_Long")
    {
        return "EDSFF_1U_Long";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_1U_Short")
    {
        return "EDSFF_1U_Short";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_E3_Short")
    {
        return "EDSFF_E3_Short";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_E3_Long")
    {
        return "EDSFF_E3_Long";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2230")
    {
        return "M2_2230";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2242")
    {
        return "M2_2242";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2260")
    {
        return "M2_2260";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2280")
    {
        return "M2_2280";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_22110")
    {
        return "M2_22110";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.U2")
    {
        return "U2";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.PCIeSlotFullLength")
    {
        return "PCIeSlotFullLength";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.PCIeSlotLowProfile")
    {
        return "PCIeSlotLowProfile";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.PCIeHalfLength")
    {
        return "PCIeHalfLength";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.OEM")
    {
        return "OEM";
    }

    return std::nullopt;
}

inline std::optional<std::string> convertDriveOperation(const std::string& op)
{
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Sanitize")
    {
        return "Sanitize";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Deduplicate")
    {
        return "Deduplicate";
    }
    if (op ==
        "xyz.openbmc_project.Nvme.Operation.OperationType.CheckConsistency")
    {
        return "CheckConsistency";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Initialize")
    {
        return "Initialize";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Replicate")
    {
        return "Replicate";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Delete")
    {
        return "Delete";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.ChangeRAIDType")
    {
        return "ChangeRAIDType";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Rebuild")
    {
        return "Rebuild";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Encrypt")
    {
        return "Encrypt";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Encrypt")
    {
        return "Decrypt";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Resize")
    {
        return "Resize";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Compress")
    {
        return "Compress";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Format")
    {
        return "Format";
    }
    if (op ==
        "xyz.openbmc_project.Nvme.Operation.OperationType.ChangeStripSize")
    {
        return "ChangeStripSize";
    }
    return "";
}

inline void
    getDriveItemProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item.Drive",
        [asyncResp](const boost::system::error_code& ec,
                    const std::vector<
                        std::pair<std::string, dbus::utility::DbusVariantType>>&
                        propertiesList) {
        if (ec)
        {
            // this interface isn't required
            return;
        }
        const std::string* encryptionStatus = nullptr;
        const bool* isLocked = nullptr;
        for (const std::pair<std::string, dbus::utility::DbusVariantType>&
                 property : propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "Type")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    // illegal property
                    BMCWEB_LOG_ERROR("Illegal property: Type");
                    messages::internalError(asyncResp->res);
                    return;
                }

                std::optional<drive::MediaType> mediaType =
                    convertDriveType(*value);
                if (!mediaType)
                {
                    BMCWEB_LOG_WARNING("UnknownDriveType Interface: {}",
                                       *value);
                    continue;
                }
                if (*mediaType == drive::MediaType::Invalid)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                asyncResp->res.jsonValue["MediaType"] = *mediaType;
            }
            else if (propertyName == "Capacity")
            {
                const uint64_t* capacity =
                    std::get_if<uint64_t>(&property.second);
                if (capacity == nullptr)
                {
                    BMCWEB_LOG_ERROR("Illegal property: Capacity");
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (*capacity == 0)
                {
                    // drive capacity not known
                    continue;
                }

                asyncResp->res.jsonValue["CapacityBytes"] = *capacity;
            }
            else if (propertyName == "Protocol")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Illegal property: Protocol");
                    messages::internalError(asyncResp->res);
                    return;
                }

                std::optional<protocol::Protocol> proto =
                    convertDriveProtocol(*value);
                if (!proto)
                {
                    BMCWEB_LOG_WARNING("Unknown DrivePrototype Interface: {}",
                                       *value);
                    continue;
                }
                if (*proto == protocol::Protocol::Invalid)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Protocol"] = *proto;
            }
            else if (propertyName == "FormFactor")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Illegal property: FormFactor");
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::optional<std::string> formFactor =
                    convertDriveFormFactor(*value);
                if (!formFactor)
                {
                    BMCWEB_LOG_ERROR(
                        "Unsupported Drive FormFactor Interface: {}", *value);
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["DriveFormFactor"] = *formFactor;
            }
            else if (propertyName == "PredictedMediaLifeLeftPercent")
            {
                const uint8_t* lifeLeft =
                    std::get_if<uint8_t>(&property.second);
                if (lifeLeft == nullptr)
                {
                    BMCWEB_LOG_ERROR(
                        "Illegal property: PredictedMediaLifeLeftPercent");
                    messages::internalError(asyncResp->res);
                    return;
                }
                // 255 means reading the value is not supported
                if (*lifeLeft != 255)
                {
                    asyncResp->res.jsonValue["PredictedMediaLifeLeftPercent"] =
                        *lifeLeft;
                }
            }
            else if (propertyName == "EncryptionStatus")
            {
                encryptionStatus = std::get_if<std::string>(&property.second);
                if (encryptionStatus == nullptr)
                {
                    BMCWEB_LOG_ERROR("Illegal property: EncryptionStatus");
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
            else if (propertyName == "Locked")
            {
                isLocked = std::get_if<bool>(&property.second);
                if (isLocked == nullptr)
                {
                    BMCWEB_LOG_ERROR("Illegal property: Locked");
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
        }

        if (encryptionStatus == nullptr || isLocked == nullptr ||
            *encryptionStatus ==
                "xyz.openbmc_project.Inventory.Item.Drive.DriveEncryptionState.Unknown")
        {
            return;
        }
        if (*encryptionStatus !=
            "xyz.openbmc_project.Inventory.Item.Drive.DriveEncryptionState.Encrypted")
        {
            //"The drive is not currently encrypted."
            asyncResp->res.jsonValue["EncryptionStatus"] =
                drive::EncryptionStatus::Unencrypted;
            return;
        }
        if (*isLocked)
        {
            //"The drive is currently encrypted and the data is not
            // accessible to the user."
            asyncResp->res.jsonValue["EncryptionStatus"] =
                drive::EncryptionStatus::Locked;
            return;
        }
        // if not locked
        // "The drive is currently encrypted but the data is accessible
        // to the user in unencrypted form."
        asyncResp->res.jsonValue["EncryptionStatus"] =
            drive::EncryptionStatus::Unlocked;
    });
}

inline void
    getDrivePortProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.PortInfo",
        [asyncResp](const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string, dbus::utility::DbusVariantType>>&
                        propertiesList) {
        if (ec)
        {
            // this interface isn't required
            return;
        }
        for (const std::pair<std::string, dbus::utility::DbusVariantType>&
                 property : propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "MaxSpeed")
            {
                const double* maxSpeed = std::get_if<double>(&property.second);
                if (maxSpeed == nullptr)
                {
                    BMCWEB_LOG_ERROR("Illegal property: MaxSpeed");
                    messages::internalError(asyncResp->res);
                    return;
                }

                asyncResp->res.jsonValue["CapableSpeedGbs"] = *maxSpeed;
            }
            else if (propertyName == "CurrentSpeed")
            {
                const double* speed = std::get_if<double>(&property.second);
                if (speed == nullptr)
                {
                    BMCWEB_LOG_ERROR("Illegal property: NegotiatedSpeedGbs");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["NegotiatedSpeedGbs"] = *speed;
            }
        }
    });
}

inline void getDriveVersion(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& connectionName,
                            const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Software.Version", "Version",
        [asyncResp, path](const boost::system::error_code ec,
                          const std::string& version) {
        if (ec)
        {
            return;
        }
        asyncResp->res.jsonValue["Revision"] = version;
    });
}

inline void
    getDriveLocation(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, path](const boost::system::error_code ec,
                          const std::string& location) {
        if (ec)
        {
            return;
        }
        asyncResp->res.jsonValue["PhysicalLocation"]["PartLocation"]
                                ["ServiceLabel"] = location;
    });
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Location", "LocationType",
        [asyncResp, path](const boost::system::error_code ec,
                          const std::string& type) {
        if (ec)
        {
            return;
        }
        asyncResp->res
            .jsonValue["PhysicalLocation"]["PartLocation"]["LocationType"] =
            redfish::dbus_utils::toLocationType(type);
    });
}

inline void getDriveStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path, const std::string& sw)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [asyncResp, path, sw](const boost::system::error_code ec,
                              const bool functional) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("fail to get drive status");
            return;
        }
        if (!functional)
        {
            asyncResp->res.jsonValue["StatusIndicator"] = "Fail";
        }
        else if (sw != "0" && sw != "2")
        {
            // the temperature(2) is excluded and it is not PFA.
            asyncResp->res.jsonValue["StatusIndicator"] =
                "PredictiveFailureAnalysis";
            asyncResp->res.jsonValue["FailurePredicted"] = true;
        }
        else
        {
            asyncResp->res.jsonValue["StatusIndicator"] = "OK";
            asyncResp->res.jsonValue["FailurePredicted"] = false;
        }
    });
}

inline void
    getDriveSmartWarning(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& connectionName,
                         const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Nvme.Status", "SmartWarnings",
        [asyncResp, connectionName, path](const boost::system::error_code ec,
                                          const std::string& sw) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("fail to get drive smart");
            return;
        }
        getDriveStatus(asyncResp, connectionName, path, sw);
    });
}

inline void
    getDriveProgress(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& connectionName, const std::string& path,
                     const std::optional<std::string>& operationName)
{
    sdbusplus::asio::getProperty<uint8_t>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Common.Progress", "Progress",
        [asyncResp, operationName, path](const boost::system::error_code ec,
                                         const uint8_t prog) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("fail to get drive progress");
            return;
        }
        nlohmann::json::object_t obj;
        asyncResp->res.jsonValue["Operations"] = nlohmann::json::array_t();

        obj["PercentageComplete"] = prog;
        if (operationName)
        {
            obj["OperationName"] = *operationName;
        }

        sdbusplus::message::object_path objectPath(path);
        std::string driveId = objectPath.filename();
        if (!taskUri[driveId].empty())
        {
            obj["AssociatedTask"]["@odata.id"] = taskUri[driveId];
        }

        asyncResp->res.jsonValue["Operations"].emplace_back(std::move(obj));
    });
}

inline void
    getDriveOperation(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& connectionName,
                      const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Nvme.Operation", "Operation",
        [asyncResp, connectionName, path](const boost::system::error_code ec,
                                          const std::string& op) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("fail to get drive progress");
            return;
        }

        std::optional<std::string> operationName = convertDriveOperation(op);
        getDriveProgress(asyncResp, connectionName, path, operationName);
    });
}

static void addAllDriveInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& connectionName,
                            const std::string& path,
                            const std::vector<std::string>& interfaces)
{
    for (const std::string& interface : interfaces)
    {
        if (interface == "xyz.openbmc_project.Inventory.Decorator.Asset")
        {
            getDriveAsset(asyncResp, connectionName, path);
        }
        else if (interface == "xyz.openbmc_project.Inventory.Item")
        {
            getDrivePresent(asyncResp, connectionName, path);
        }
        else if (interface == "xyz.openbmc_project.State.Drive")
        {
            getDriveState(asyncResp, connectionName, path);
        }
        else if (interface == "xyz.openbmc_project.Inventory.Item.Drive")
        {
            getDriveItemProperties(asyncResp, connectionName, path);
        }
        else if (interface ==
                 "xyz.openbmc_project.Inventory.Decorator.PortInfo")
        {
            getDrivePortProperties(asyncResp, connectionName, path);
        }
        else if (interface == "xyz.openbmc_project.Software.Version")
        {
            getDriveVersion(asyncResp, connectionName, path);
        }
        else if (interface == "xyz.openbmc_project.Nvme.Status")
        {
            getDriveSmartWarning(asyncResp, connectionName, path);
        }
        else if (interface ==
                 "xyz.openbmc_project.Inventory.Decorator.LocationCode")
        {
            getDriveLocation(asyncResp, connectionName, path);
        }
        else if (interface == "xyz.openbmc_project.Nvme.Operation")
        {
            getDriveOperation(asyncResp, connectionName, path);
        }
    }
}

// The getMainChassisId() would get the Main Chassis ID but it's not suitable
// for the case of Drives under sub-chassis Need to ensure this Chassis includes
// the drive endpoints
inline void getChassisID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& driveId)
{
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp,
         driveId](const boost::system::error_code ec,
                  const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        // Iterate over all retrieved ObjectPaths.
        for (const auto& [path, connectionNames] : subtree)
        {
            if (connectionNames.empty())
            {
                BMCWEB_LOG_ERROR("Got 0 Connection names");
                continue;
            }

            sdbusplus::message::object_path objPath(path);
            sdbusplus::asio::getProperty<std::vector<std::string>>(
                *crow::connections::systemBus,
                "xyz.openbmc_project.ObjectMapper", path + "/drive",
                "xyz.openbmc_project.Association", "endpoints",
                [asyncResp, objPath](const boost::system::error_code ec3,
                                     const std::vector<std::string>& resp) {
                if (ec3 || resp.empty())
                {
                    return; // no drives = no
                            // failures
                }
                asyncResp->res.jsonValue["Links"]["Chassis"]["@odata.id"] =
                    "/redfish/v1/Chassis/" + objPath.filename();

                return;
            });
        }
    });
}

inline void createSanitizeProgressTask(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& driveId)
{
    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        [service, path,
         driveId](boost::system::error_code ec, sdbusplus::message_t& msg,
                  const std::shared_ptr<task::TaskData>& taskData) {
        if (ec)
        {
            taskData->finishTask();
            taskData->state = "Aborted";
            taskData->messages.emplace_back(
                messages::resourceErrorsDetectedFormatError("Drive SecureErase",
                                                            ec.message()));
            return task::completed;
        }

        std::string iface;
        boost::container::flat_map<std::string, dbus::utility::DbusVariantType>
            values;

        std::string index = std::to_string(taskData->index);
        msg.read(iface, values);

        if (iface != "xyz.openbmc_project.Common.Progress")
        {
            return !task::completed;
        }
        auto findStatus = values.find("Status");
        if (findStatus != values.end())
        {
            std::string* state =
                std::get_if<std::string>(&(findStatus->second));
            if (state == nullptr)
            {
                taskData->messages.emplace_back(messages::internalError());
                return !task::completed;
            }

            if (boost::ends_with(*state, "Aborted") ||
                boost::ends_with(*state, "Failed"))
            {
                taskData->state = "Exception";
                taskData->messages.emplace_back(messages::taskAborted(index));
                return task::completed;
            }

            if (boost::ends_with(*state, "Completed"))
            {
                taskData->state = "Completed";
                taskData->percentComplete = 100;
                taskData->messages.emplace_back(
                    messages::taskCompletedOK(index));
                taskData->finishTask();
                return task::completed;
            }
        }

        auto findProgress = values.find("Progress");
        if (findProgress == values.end())
        {
            return !task::completed;
        }
        uint8_t* progress = std::get_if<uint8_t>(&(findProgress->second));
        if (progress == nullptr)
        {
            taskData->messages.emplace_back(messages::internalError());
            return task::completed;
        }
        taskData->percentComplete = static_cast<int>(*progress);

        BMCWEB_LOG_ERROR("{}", taskData->percentComplete);
        taskData->messages.emplace_back(messages::taskProgressChanged(
            index, static_cast<size_t>(*progress)));

        return !task::completed;
    },
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',path='" +
            path + "'");

    task->startTimer(std::chrono::seconds(60));
    task->payload.emplace(req);
    task->populateResp(asyncResp->res);

    taskUri[driveId] = "/redfish/v1/TaskService/Tasks/" +
                       std::to_string(task->index);
}

inline void
    handleDriveSanitizePost(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& /*chassisId*/,
                            const std::string& driveId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::string sanitizeType;
    uint16_t owPass = 0;
    if (!json_util::readJsonAction(req, asyncResp->res, "SanitizationType",
                                   sanitizeType))
    {
        messages::actionParameterValueError(asyncResp->res, "Drive.SecureErase",
                                            "SanitizationType");
        return;
    }
    if (sanitizeType == "Overwrite")
    {
        if (!json_util::readJsonAction(req, asyncResp->res, "OverwritePasses",
                                       owPass))
        {
            messages::actionParameterMissing(
                asyncResp->res, "Drive.SecureErase", "OverwritePasses");
            return;
        }
    }
    else if (sanitizeType == "CryptographicErase")
    {
        sanitizeType = "CryptoErase";
    }

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Drive"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [req, asyncResp, driveId, sanitizeType,
         owPass](const boost::system::error_code ec,
                 const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Drive mapper call error");
            messages::internalError(asyncResp->res);
            return;
        }

        auto drive = std::find_if(
            subtree.begin(), subtree.end(),
            [&driveId](
                const std::pair<std::string, dbus::utility::MapperServiceMap>&
                    object) {
            return sdbusplus::message::object_path(object.first).filename() ==
                   driveId;
        });
        if (drive == subtree.end())
        {
            messages::resourceNotFound(asyncResp->res, "Drive", driveId);
            return;
        }
        const std::string& path = drive->first;
        const dbus::utility::MapperServiceMap& connNames = drive->second;
        if (connNames.size() != 1)
        {
            BMCWEB_LOG_ERROR("Connection size {}, not equal to 1",
                             connNames.size());
            messages::internalError(asyncResp->res);
            return;
        }

        auto service = connNames[0].first;
        auto interfaces = connNames[0].second;
        for (const std::string& interface : interfaces)
        {
            if (interface != "xyz.openbmc_project.Nvme.SecureErase")
            {
                continue;
            }

            auto methodName =
                "xyz.openbmc_project.Nvme.SecureErase.EraseMethod." +
                sanitizeType;
            // execute drive sanitize operation
            crow::connections::systemBus->async_method_call(
                [req, asyncResp, service, path,
                 driveId](const boost::system::error_code ec,
                          sdbusplus::message::message& msg) {
                const sd_bus_error* dbusError = msg.get_error();
                if (dbusError != nullptr &&
                    strcmp(dbusError->name,
                           "xyz.openbmc_project.Common.Error.NotAllowed") == 0)
                {
                    std::string resolution =
                        "Drive sanitize in progress. Retry "
                        "the sanitize operation once it is complete.";
                    redfish::messages::updateInProgressMsg(asyncResp->res,
                                                           resolution);
                    BMCWEB_LOG_ERROR("Sanitize on drive{} already in progress.",
                                     driveId);
                }
                if (ec)
                {
                    // other errors return here.
                    return;
                }
                createSanitizeProgressTask(req, asyncResp, service, path,
                                           driveId);
            },
                service, path, interface, "Erase", owPass, methodName);
            return;
        }
    });
}

inline void handleDriveSanitizetActionInfoGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string&,
    const std::string& driveId)
{
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_1_2.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "Sanitize Action Info";
    asyncResp->res.jsonValue["Id"] = "SanitizeActionInfo";

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Drive"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp,
         driveId](const boost::system::error_code ec,
                  const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Drive mapper call error");
            messages::internalError(asyncResp->res);
            return;
        }

        auto drive = std::find_if(
            subtree.begin(), subtree.end(),
            [&driveId](
                const std::pair<std::string, dbus::utility::MapperServiceMap>&
                    object) {
            return sdbusplus::message::object_path(object.first).filename() ==
                   driveId;
        });
        if (drive == subtree.end())
        {
            messages::resourceNotFound(asyncResp->res, "Drive", driveId);
            return;
        }
        const std::string& path = drive->first;
        const dbus::utility::MapperServiceMap& connNames = drive->second;
        if (connNames.size() != 1)
        {
            BMCWEB_LOG_ERROR("Connection size {}, not equal to 1",
                             connNames.size());
            messages::internalError(asyncResp->res);
            return;
        }

        auto service = connNames[0].first;
        auto interfaces = connNames[0].second;
        for (const std::string& interface : interfaces)
        {
            if (interface != "xyz.openbmc_project.Nvme.SecureErase")
            {
                continue;
            }
            sdbusplus::asio::getProperty<std::vector<std::string>>(
                *crow::connections::systemBus, service, path, interface,
                "SanitizeCapability",
                [asyncResp](const boost::system::error_code ec,
                            const std::vector<std::string>& cap) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("fail to get drive Progress");
                    return;
                }
                nlohmann::json::array_t parameters;
                nlohmann::json::object_t parameter;
                nlohmann::json::array_t allowed;

                if (std::find(
                        cap.begin(), cap.end(),
                        "xyz.openbmc_project.Nvme.SecureErase.EraseMethod.Overwrite") !=
                    cap.end())
                {
                    parameter["Name"] = "OverwritePasses";
                    parameter["DataType"] = "integer";
                    parameters.push_back(parameter);

                    allowed.push_back("Overwrite");
                }
                if (std::find(
                        cap.begin(), cap.end(),
                        "xyz.openbmc_project.Nvme.SecureErase.EraseMethod.BlockErase") !=
                    cap.end())
                {
                    allowed.push_back("BlockErase");
                }
                if (std::find(
                        cap.begin(), cap.end(),
                        "xyz.openbmc_project.Nvme.SecureErase.EraseMethod.CryptoErase") !=
                    cap.end())
                {
                    allowed.push_back("CryptographicErase");
                }
                parameter["Name"] = "SanitizationType";
                parameter["DataType"] = "String";

                parameter["AllowableValues"] = allowed;
                parameters.push_back(parameter);

                asyncResp->res.jsonValue["Parameters"] = parameters;
            });
            break;
        }
    });
}

inline void handleSystemDriveSanitizetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId, const std::string& driveId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Systems/" + systemId +
                                            "/Drives/" + driveId +
                                            "/SanitizeActionInfo";

    handleDriveSanitizetActionInfoGet(asyncResp, systemId, driveId);
}

inline void handleChassisDriveSanitizetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& driveId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Chassis/" + chassisId +
                                            "/Drives/" + driveId +
                                            "/SanitizeActionInfo";

    handleDriveSanitizetActionInfoGet(asyncResp, chassisId, driveId);
}

inline void afterGetSubtreeSystemsStorageDrive(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& driveId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Drive mapper call error");
        messages::internalError(asyncResp->res);
        return;
    }

    auto drive = std::ranges::find_if(
        subtree,
        [&driveId](const std::pair<std::string,
                                   dbus::utility::MapperServiceMap>& object) {
        return sdbusplus::message::object_path(object.first).filename() ==
               driveId;
    });

    if (drive == subtree.end())
    {
        messages::resourceNotFound(asyncResp->res, "Drive", driveId);
        return;
    }

    const std::string& path = drive->first;
    const dbus::utility::MapperServiceMap& connectionNames = drive->second;

    asyncResp->res.jsonValue["@odata.type"] = "#Drive.v1_18_0.Drive";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Systems/{}/Storage/1/Drives/{}",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME, driveId);
    asyncResp->res.jsonValue["Name"] = driveId;
    asyncResp->res.jsonValue["Id"] = driveId;

    if (connectionNames.size() != 1)
    {
        BMCWEB_LOG_ERROR("Connection size {}, not equal to 1",
                         connectionNames.size());
        messages::internalError(asyncResp->res);
        return;
    }

    getChassisID(asyncResp, driveId);

    // default it to Enabled
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

    addAllDriveInfo(asyncResp, connectionNames[0].first, path,
                    connectionNames[0].second);
}

inline void handleSystemsStorageDriveGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& driveId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Drive"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(afterGetSubtreeSystemsStorageDrive, asyncResp,
                        driveId));
}

/**
 * System drives, this URL will show all the DriveCollection
 * information
 */
inline void
    driveCollectionGet(crow::App& app, const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#DriveCollection.DriveCollection";
    asyncResp->res.jsonValue["Name"] = "Drive Collection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Storage/1/Drives/";

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Drive"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Drive mapper call error");
            messages::internalError(asyncResp->res);
            return;
        }
        nlohmann::json& members = asyncResp->res.jsonValue["Members"];
        // important if array is empty
        members = nlohmann::json::array();
        nlohmann::json::object_t member;
        for (const auto& [path, connNames] : subtree)
        {
            // EM also populate NVMe drives to Dbus
            // We expect to have NVMe resource from nvme-manager so we
            // filter out drive instances from EM by the number of Dbus
            // interface.
            sdbusplus::message::object_path objPath(path);
            auto id = objPath.filename();
            uint32_t num = 0;
            for (const std::string& interface : connNames.begin()->second)
            {
                if (std::find_if(driveInterface.begin(), driveInterface.end(),
                                 [interface](const std::string& possible) {
                    return boost::starts_with(interface, possible);
                }) != driveInterface.end())
                {
                    num++;
                }
            }
            if (num != driveInterface.size())
            {
                continue;
            }
            member["@odata.id"] = "/redfish/v1/Systems/" +
                                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                  "/Storage/1/Drives/" + id;
            members.emplace_back(member);
        }
        asyncResp->res.jsonValue["Members@odata.count"] = members.size();
    });
}

inline void requestRoutesDrive(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Storage/1/Drives/")
        .privileges(redfish::privileges::getDriveCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(driveCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Storage/1/Drives/<str>/")
        .privileges(redfish::privileges::getDrive)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSystemsStorageDriveGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Storage/1/Drives/<str>/Actions/Drive.SecureErase/")
        .privileges(redfish::privileges::postDrive)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDriveSanitizePost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Storage/1/Drives/<str>/SanitizeActionInfo/")
        .privileges(redfish::privileges::getDrive)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSystemDriveSanitizetActionInfoGet, std::ref(app)));
}

inline void afterChassisDriveCollectionSubtreeGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        if (ec == boost::system::errc::host_unreachable)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }
        messages::internalError(asyncResp->res);
        return;
    }

    // Iterate over all retrieved ObjectPaths.
    for (const auto& [path, connectionNames] : subtree)
    {
        sdbusplus::message::object_path objPath(path);
        if (objPath.filename() != chassisId)
        {
            continue;
        }

        if (connectionNames.empty())
        {
            BMCWEB_LOG_ERROR("Got 0 Connection names");
            continue;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#DriveCollection.DriveCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/Drives", chassisId);
        asyncResp->res.jsonValue["Name"] = "Drive Collection";

        // Association lambda
        dbus::utility::getAssociationEndPoints(
            path + "/drive",
            [asyncResp, chassisId](const boost::system::error_code& ec3,
                                   const dbus::utility::MapperEndPoints& resp) {
            if (ec3)
            {
                BMCWEB_LOG_ERROR("Error in chassis Drive association ");
            }
            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            // important if array is empty
            members = nlohmann::json::array();

            std::vector<std::string> leafNames;
            for (const auto& drive : resp)
            {
                sdbusplus::message::object_path drivePath(drive);
                leafNames.push_back(drivePath.filename());
            }

            std::ranges::sort(leafNames, AlphanumLess<std::string>());

            for (const auto& leafName : leafNames)
            {
                nlohmann::json::object_t member;
                member["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Chassis/{}/Drives/{}", chassisId, leafName);
                members.emplace_back(std::move(member));
                // navigation links will be registered in next patch set
            }
            asyncResp->res.jsonValue["Members@odata.count"] = resp.size();
        }); // end association lambda
    }
}
/**
 * Chassis drives, this URL will show all the DriveCollection
 * information
 */
inline void chassisDriveCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    // mapper call lambda
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(afterChassisDriveCollectionSubtreeGet, asyncResp,
                        chassisId));
}

inline void requestRoutesChassisDrive(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Drives/")
        .privileges(redfish::privileges::getDriveCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(chassisDriveCollectionGet, std::ref(app)));
}

inline void buildDrive(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId,
                       const std::string& driveName,
                       const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    // Iterate over all retrieved ObjectPaths.
    for (const auto& [path, connectionNames] : subtree)
    {
        sdbusplus::message::object_path objPath(path);
        if (objPath.filename() != driveName)
        {
            continue;
        }

        if (connectionNames.empty())
        {
            BMCWEB_LOG_ERROR("Got 0 Connection names");
            continue;
        }

        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/Drives/{}", chassisId, driveName);

        asyncResp->res.jsonValue["@odata.type"] = "#Drive.v1_18_0.Drive";
        asyncResp->res.jsonValue["Name"] = driveName;
        asyncResp->res.jsonValue["Id"] = driveName;
        // default it to Enabled
        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

        // FIXME: Health Populate
        // auto health = std::make_shared<HealthPopulate>(asyncResp);
        // health->inventory.emplace_back(path);
        // health->selfPath = path;
        // health->populate();

        nlohmann::json::object_t linkChassisNav;
        linkChassisNav["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}", chassisId);
        asyncResp->res.jsonValue["Links"]["Chassis"] = linkChassisNav;
        asyncResp->res.jsonValue["Actions"]["#Drive.SecureErase"]["target"] =
            boost::urls::format(
                "/redfish/v1/Chassis/{}/Drives/{}/Actions/Drive.SecureErase",
                chassisId, driveName);
        asyncResp->res.jsonValue["Actions"]["#Drive.SecureErase"]
                                ["@Redfish.ActionInfo"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/Drives/{}/SanitizeActionInfo", chassisId,
            driveName);

        addAllDriveInfo(asyncResp, connectionNames[0].first, path,
                        connectionNames[0].second);
    }
}

inline void
    matchAndFillDrive(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& chassisId,
                      const std::string& driveName,
                      const std::vector<std::string>& resp)
{
    for (const std::string& drivePath : resp)
    {
        sdbusplus::message::object_path path(drivePath);
        std::string leaf = path.filename();
        if (leaf != driveName)
        {
            continue;
        }
        //  mapper call drive
        constexpr std::array<std::string_view, 1> driveInterface = {
            "xyz.openbmc_project.Inventory.Item.Drive"};
        dbus::utility::getSubTree(
            "/xyz/openbmc_project/inventory", 0, driveInterface,
            [asyncResp, chassisId, driveName](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
            buildDrive(asyncResp, chassisId, driveName, ec, subtree);
        });
    }
}

inline void
    handleChassisDriveGet(crow::App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& driveName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    // mapper call chassis
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, chassisId,
         driveName](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        // Iterate over all retrieved ObjectPaths.
        for (const auto& [path, connectionNames] : subtree)
        {
            sdbusplus::message::object_path objPath(path);
            if (objPath.filename() != chassisId)
            {
                continue;
            }

            if (connectionNames.empty())
            {
                BMCWEB_LOG_ERROR("Got 0 Connection names");
                continue;
            }

            dbus::utility::getAssociationEndPoints(
                path + "/drive",
                [asyncResp, chassisId,
                 driveName](const boost::system::error_code& ec3,
                            const dbus::utility::MapperEndPoints& resp) {
                if (ec3)
                {
                    return; // no drives = no failures
                }
                matchAndFillDrive(asyncResp, chassisId, driveName, resp);
            });
            break;
        }
    });
}

/**
 * This URL will show the drive interface for the specific drive in the chassis
 */
inline void requestRoutesChassisDriveName(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Drives/<str>/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleChassisDriveGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Drives/<str>/Actions/Drive.SecureErase/")
        .privileges(redfish::privileges::postDrive)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDriveSanitizePost, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/Drives/<str>/SanitizeActionInfo/")
        .privileges(redfish::privileges::getDrive)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleChassisDriveSanitizetActionInfoGet, std::ref(app)));
}

inline void getStorageControllerAsset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>&
        propertiesList)
{
    if (ec)
    {
        // this interface isn't necessary
        BMCWEB_LOG_DEBUG("Failed to get StorageControllerAsset");
        return;
    }

    const std::string* partNumber = nullptr;
    const std::string* serialNumber = nullptr;
    const std::string* manufacturer = nullptr;
    const std::string* model = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
            partNumber, "SerialNumber", serialNumber, "Manufacturer",
            manufacturer, "Model", model))
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (partNumber != nullptr)
    {
        asyncResp->res.jsonValue["PartNumber"] = *partNumber;
    }

    if (serialNumber != nullptr)
    {
        asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
    }

    if (manufacturer != nullptr)
    {
        asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
    }

    if (model != nullptr)
    {
        asyncResp->res.jsonValue["Model"] = *model;
    }
}

inline void populateStorageController(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& controllerId, const std::string& connectionName,
    const std::string& path)
{
    asyncResp->res.jsonValue["@odata.type"] =
        "#StorageController.v1_6_0.StorageController";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Systems/{}/Storage/1/Controllers/{}",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME, controllerId);
    asyncResp->res.jsonValue["Name"] = controllerId;
    asyncResp->res.jsonValue["Id"] = controllerId;
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [asyncResp](const boost::system::error_code& ec, bool isPresent) {
        // this interface isn't necessary, only check it
        // if we get a good return
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Failed to get Present property");
            return;
        }
        if (!isPresent)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Absent";
        }
    });

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [asyncResp](const boost::system::error_code& ec,
                    const std::vector<
                        std::pair<std::string, dbus::utility::DbusVariantType>>&
                        propertiesList) {
        getStorageControllerAsset(asyncResp, ec, propertiesList);
    });
}

inline void getStorageControllerHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& controllerId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty())
    {
        // doesn't have to be there
        BMCWEB_LOG_DEBUG("Failed to handle StorageController");
        return;
    }

    for (const auto& [path, interfaceDict] : subtree)
    {
        sdbusplus::message::object_path object(path);
        std::string id = object.filename();
        if (id.empty())
        {
            BMCWEB_LOG_ERROR("Failed to find filename in {}", path);
            return;
        }
        if (id != controllerId)
        {
            continue;
        }

        if (interfaceDict.size() != 1)
        {
            BMCWEB_LOG_ERROR("Connection size {}, greater than 1",
                             interfaceDict.size());
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string& connectionName = interfaceDict.front().first;
        populateStorageController(asyncResp, controllerId, connectionName,
                                  path);
    }
}

inline void populateStorageControllerCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& controllerList)
{
    nlohmann::json::array_t members;
    if (ec || controllerList.empty())
    {
        asyncResp->res.jsonValue["Members"] = std::move(members);
        asyncResp->res.jsonValue["Members@odata.count"] = 0;
        BMCWEB_LOG_DEBUG("Failed to find any StorageController");
        return;
    }

    for (const std::string& path : controllerList)
    {
        std::string id = sdbusplus::message::object_path(path).filename();
        if (id.empty())
        {
            BMCWEB_LOG_ERROR("Failed to find filename in {}", path);
            return;
        }
        nlohmann::json::object_t member;
        member["@odata.id"] = boost::urls::format(
            "/redfish/v1/Systems/{}/Storage/1/Controllers/{}",
            BMCWEB_REDFISH_SYSTEM_URI_NAME, id);
        members.emplace_back(member);
    }
    asyncResp->res.jsonValue["Members@odata.count"] = members.size();
    asyncResp->res.jsonValue["Members"] = std::move(members);
}

inline void handleSystemsStorageControllerCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_DEBUG(
            "Failed to setup Redfish Route for StorageController Collection");
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        BMCWEB_LOG_DEBUG("Failed to find ComputerSystem of {}", systemName);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#StorageControllerCollection.StorageControllerCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        std::format("/redfish/v1/Systems/{}/Storage/1/Controllers",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["Name"] = "Storage Controller Collection";

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.StorageController"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreePathsResponse&
                        controllerList) {
        populateStorageControllerCollection(asyncResp, ec, controllerList);
    });
}

inline void handleSystemsStorageControllerGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& controllerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_DEBUG("Failed to setup Redfish Route for StorageController");
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        BMCWEB_LOG_DEBUG("Failed to find ComputerSystem of {}", systemName);
        return;
    }
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.StorageController"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp,
         controllerId](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
        getStorageControllerHandler(asyncResp, controllerId, ec, subtree);
    });
}

inline void requestRoutesStorageControllerCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Storage/1/Controllers/")
        .privileges(redfish::privileges::getStorageControllerCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSystemsStorageControllerCollectionGet, std::ref(app)));
}

inline void requestRoutesStorageController(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Storage/1/Controllers/<str>/")
        .privileges(redfish::privileges::getStorageController)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSystemsStorageControllerGet, std::ref(app)));
}

} // namespace redfish
