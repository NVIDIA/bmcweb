#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "nvidia_error_messages.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/processor_utils.hpp"
#include "utils/redfish_response_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <bit>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace redfish
{
namespace nvidia_processor_utils
{
using DbusProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;
using OperatingConfigProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

using ReconfigPermission = std::tuple<std::string, std::string, bool>;

/**
 * Parses the json of the InbandReconfigSettings properties.
 *
 * @param[in,out]   resp                Async HTTP response.
 * @param[in]       json                New json data to apply.
 * @param[in]       featureName         Name of permission feature
 * @param[in,out]   permissions         Collection of parsed permissions
 */
inline void parseReconfigSettingsJson(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, nlohmann::json& json,
    const std::string& featureName,
    std::vector<ReconfigPermission>& permissions)
{
    std::optional<bool> allowOneShotConfig;
    std::optional<bool> allowPersistentConfig;
    std::optional<bool> allowFLRPersistentConfig;
    if (redfish::json_util::readJson(
            json, aResp->res, "AllowOneShotConfig", allowOneShotConfig,
            "AllowPersistentConfig", allowPersistentConfig,
            "AllowFLRPersistentConfig", allowFLRPersistentConfig))
    {
        if (allowOneShotConfig)
        {
            permissions.emplace_back(featureName, "AllowOneShotConfig",
                                     *allowOneShotConfig);
        }
        if (allowPersistentConfig)
        {
            permissions.emplace_back(featureName, "AllowPersistentConfig",
                                     *allowPersistentConfig);
        }
        if (allowFLRPersistentConfig)
        {
            permissions.emplace_back(featureName, "AllowFLRPersistentConfig",
                                     *allowFLRPersistentConfig);
        }
    }
}

/**
 * Parses the json of the Inband/DOE ReconfigPermissions.
 *
 * @param[in,out]   resp                Async HTTP response.
 * @param[in]       json                New json data to apply.
 *
 * @return Collection of parsed Inband Reconfig Permissions requests
 */
inline std::vector<ReconfigPermission> parseReconfigPermissionsJson(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, nlohmann::json& json)
{
    std::vector<ReconfigPermission> permissions;
    std::map<std::string, std::optional<nlohmann::json>> features = {
        {"InSystemTest", {}},
        {"FusingMode", {}},
        {"CCMode", {}},
        {"BAR0Firewall", {}},
        {"CCDevMode", {}},
        {"TGPCurrentLimit", {}},
        {"TGPRatedLimit", {}},
        {"TGPMaxLimit", {}},
        {"TGPMinLimit", {}},
        {"ClockLimit", {}},
        {"NVLinkDisable", {}},
        {"ECCEnable", {}},
        {"PCIeVFConfiguration", {}},
        {"RowRemappingAllowed", {}},
        {"RowRemappingFeature", {}},
        {"HBMFrequencyChange", {}},
        {"HULKLicenseUpdate", {}},
        {"ForceTestCoupling", {}},
        {"BAR0TypeConfig", {}},
        {"EDPpScalingFactor", {}},
        {"PowerSmoothing", {}},
        {"PowerSmoothingPrivilegeLevel0", {}},
        {"PowerSmoothingPrivilegeLevel1", {}},
        {"PowerSmoothingPrivilegeLevel2", {}},
        {"EGMMode", {}},
        {"InfoROMFileSystemRecreate", {}},
        {"RISTDiagnostic", {}},
        {"AdaptiveTGPMode", {}},
    };

    if (redfish::json_util::readJson(
            json, aResp->res, "InSystemTest", features["InSystemTest"],
            "FusingMode", features["FusingMode"], "CCMode", features["CCMode"],
            "BAR0Firewall", features["BAR0Firewall"], "CCDevMode",
            features["CCDevMode"], "TGPCurrentLimit",
            features["TGPCurrentLimit"], "TGPRatedLimit",
            features["TGPRatedLimit"], "TGPMaxLimit", features["TGPMaxLimit"],
            "TGPMinLimit", features["TGPMinLimit"], "ClockLimit",
            features["ClockLimit"], "NVLinkDisable", features["NVLinkDisable"],
            "ECCEnable", features["ECCEnable"], "PCIeVFConfiguration",
            features["PCIeVFConfiguration"], "RowRemappingAllowed",
            features["RowRemappingAllowed"], "RowRemappingFeature",
            features["RowRemappingFeature"], "HBMFrequencyChange",
            features["HBMFrequencyChange"], "HULKLicenseUpdate",
            features["HULKLicenseUpdate"], "ForceTestCoupling",
            features["ForceTestCoupling"], "BAR0TypeConfig",
            features["BAR0TypeConfig"], "EDPpScalingFactor",
            features["EDPpScalingFactor"], "PowerSmoothing",
            features["PowerSmoothing"], "PowerSmoothingPrivilegeLevel0",
            features["PowerSmoothingPrivilegeLevel0"],
            "PowerSmoothingPrivilegeLevel1",
            features["PowerSmoothingPrivilegeLevel1"],
            "PowerSmoothingPrivilegeLevel2",
            features["PowerSmoothingPrivilegeLevel2"], "EGMMode",
            features["EGMMode"], "InfoROMFileSystemRecreate",
            features["InfoROMFileSystemRecreate"], "RISTDiagnostic",
            features["RISTDiagnostic"], "AdaptiveTGPMode",
            features["AdaptiveTGPMode"]))
    {
        for (auto& [featureName, feature] : features)
        {
            if (feature)
            {
                parseReconfigSettingsJson(aResp, *feature, featureName,
                                          permissions);
            }
        }
    }
    return permissions;
}

/**
 * Parses the json of the InbandReconfigSettings properties.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       json            InbandReconfigPermissions json data to
 * apply.
 */
inline void patchInbandReconfigPermissions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, nlohmann::json& json)
{
    auto patchRequests = parseReconfigPermissionsJson(asyncResp, json);
    redfish::processor_utils::getProcessorObject(
        asyncResp, processorId,
        [patchRequests](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        [[maybe_unused]] const std::string& procId,
                        const std::string& objectPath,
                        const dbus::utility::MapperServiceMap& serviceMap,
                        [[maybe_unused]] const std::string& deviceType) {
            for (const auto& [service, _] : serviceMap)
            {
                for (const auto& [featureName, property, value] : patchRequests)
                {
                    std::string path = objectPath;
                    path += "/InbandReconfigPermissions/";
                    path += featureName;
                    nvidia_async_operation_utils::patch(
                        aResp, service, path,
                        "com.nvidia.InbandReconfigSettings", property, value);
                }
            }
        });
}

/**
 * Parses the json of the DOEReconfigSettings properties.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       json            DOEReconfigPermissions json data to
 * apply.
 */
inline void patchDOEReconfigPermissions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, nlohmann::json& json)
{
    auto patchRequests = parseReconfigPermissionsJson(asyncResp, json);
    redfish::processor_utils::getProcessorObject(
        asyncResp, processorId,
        [patchRequests](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        [[maybe_unused]] const std::string& procId,
                        const std::string& objectPath,
                        const dbus::utility::MapperServiceMap& serviceMap,
                        [[maybe_unused]] const std::string& deviceType) {
            for (const auto& [service, _] : serviceMap)
            {
                for (const auto& [featureName, property, value] : patchRequests)
                {
                    std::string path = objectPath;
                    path += "/DOEReconfigPermissions/";
                    path += featureName;
                    nvidia_async_operation_utils::patch(
                        aResp, service, path,
                        "com.nvidia.InbandReconfigSettings", property, value);
                }
            }
        });
}

/**
 * Handle the PATCH operation of the CC Mode Property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       ccMode         New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchCCMode(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                        const std::string& processorId, const bool ccMode,
                        const std::string& cpuObjectPath,
                        const dbus::utility::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, "com.nvidia.CCMode") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(" CCMode interface not found ");
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        cpuObjectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, ccMode, processorId, cpuObjectPath, service = *inventoryService](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call");

                    nvidia_async_operation_utils::
                        doGenericSetAsyncAndGatherResult(
                            resp, std::chrono::seconds(60), service,
                            cpuObjectPath, "com.nvidia.CCMode", "CCModeEnabled",
                            std::variant<bool>(ccMode),
                            nvidia_async_operation_utils::PatchCCModeCallback{
                                resp});

                    return;
                }
            }

            // Set the property, with handler to check error responses
            dbus::utility::async_method_call(
                [resp, processorId](boost::system::error_code ec2,
                                    sdbusplus::message::message& msg) {
                    if (!ec2)
                    {
                        BMCWEB_LOG_DEBUG("Set CC Mode property succeeded");
                        return;
                    }
                    BMCWEB_LOG_DEBUG("CPU:{} set CC Mode  property failed: {}",
                                     processorId, ec2);

                    // Read and convert dbus error message to redfish error
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Error While Doing Patch on CCMode");
                        messages::internalError(resp->res);
                        return;
                    }

                    if (strcmp(dbusError->name,
                               "xyz.openbmc_project.Common."
                               "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        BMCWEB_LOG_ERROR(
                            "WriteFailure While Doing Patch on CCMode");
                        messages::operationFailed(resp->res);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "UnknownError While Doing Patch on CCMode");
                        messages::internalError(resp->res);
                    }
                },
                service, cpuObjectPath, "org.freedesktop.DBus.Properties",
                "Set", "com.nvidia.CCMode", "CCModeEnabled",
                std::variant<bool>(ccMode));
        });
}

/**
 * Handle the PATCH operation of the MIG Mode Property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       ccDevMode         New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchCCDevMode(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                           const std::string& processorId, const bool ccDevMode,
                           const std::string& cpuObjectPath,
                           const dbus::utility::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, "com.nvidia.CCMode") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(" CCMode interface not found ");
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        cpuObjectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, ccDevMode, processorId, cpuObjectPath,
         service =
             *inventoryService](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call");

                    nvidia_async_operation_utils::
                        doGenericSetAsyncAndGatherResult(
                            resp, std::chrono::seconds(60), service,
                            cpuObjectPath, "com.nvidia.CCMode",
                            "CCDevModeEnabled", std::variant<bool>(ccDevMode),
                            nvidia_async_operation_utils::PatchCCModeCallback{
                                resp});

                    return;
                }
            }

            // Set the property, with handler to check error responses
            dbus::utility::async_method_call(
                [resp, processorId](boost::system::error_code ec2,
                                    sdbusplus::message::message& msg) {
                    if (!ec2)
                    {
                        BMCWEB_LOG_DEBUG("Set CC Dev Mode property succeeded");
                        return;
                    }

                    BMCWEB_LOG_DEBUG(
                        "CPU:{} set CC Dev Mode  property failed: {}",
                        processorId, ec2);
                    // Read and convert dbus error message to redfish error
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Error While Doing Patch on CCDevMode");
                        messages::internalError(resp->res);
                        return;
                    }

                    if (strcmp(dbusError->name,
                               "xyz.openbmc_project.Common."
                               "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        BMCWEB_LOG_ERROR(
                            "WriteFailure While Doing Patch on CCDevMode");
                        messages::operationFailed(resp->res);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "UnknownError While Doing Patch on CCDevMode");
                        messages::internalError(resp->res);
                    }
                },
                service, cpuObjectPath, "org.freedesktop.DBus.Properties",
                "Set", "com.nvidia.CCMode", "CCDevModeEnabled",
                std::variant<bool>(ccDevMode));
        });
}

// Function to handle the getEgmModePendingData async method call response
static void egmAsyncRespHandler(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                                const std::string& processorId,
                                const boost::system::error_code& ec,
                                const sdbusplus::message::message& msg)
{
    if (!ec)
    {
        BMCWEB_LOG_DEBUG("Set EGM Mode property succeeded");
        return;
    }
    BMCWEB_LOG_DEBUG("CPU:{} set EGM Mode  property failed: {}", processorId,
                     ec);

    // Read and convert dbus error message to redfish error
    const sd_bus_error* dbusError = msg.get_error();
    if (dbusError == nullptr)
    {
        BMCWEB_LOG_ERROR("Error While Doing Patch on EGMMode");
        messages::internalError(resp->res);
        return;
    }

    if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                "Device.Error.WriteFailure") == 0)
    {
        // Service failed to change the config
        BMCWEB_LOG_ERROR("WriteFailure While Doing Patch on EGMMode");
        messages::operationFailed(resp->res);
    }
    else if (strcmp(dbusError->name,
                    "xyz.openbmc_project.Common.Error.Unavailable") == 0)
    {
        std::string errBusy = "0x50A";
        std::string errBusyResolution =
            "SMBPBI Command failed with error busy, \
                         please try after 60 seconds";

        // busy error
        messages::asyncError(resp->res, errBusy, errBusyResolution);
    }
    else if (strcmp(dbusError->name,
                    "xyz.openbmc_project.Common.Error.Timeout") == 0)
    {
        std::string errTimeout = "0x600";
        std::string errTimeoutResolution = "Settings may/maynot have applied, \
             please check get response before patching";

        // timeout error
        messages::asyncError(resp->res, errTimeout, errTimeoutResolution);
    }
    else
    {
        BMCWEB_LOG_ERROR("UnknownError While Doing Patch on EGMMode");
        messages::internalError(resp->res);
    }
}

static void egmGetDbusObjectHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const bool egmMode,
    const std::string& processorId, const std::string& cpuObjectPath,
    const std::string& service, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (!ec)
    {
        for (const auto& [serv, _] : object)
        {
            if (serv != service)
            {
                continue;
            }

            BMCWEB_LOG_DEBUG("Performing Patch using Set Async Method Call");

            nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                resp, std::chrono::seconds(60), service, cpuObjectPath,
                "com.nvidia.EgmMode", "EGMModeEnabled",
                std::variant<bool>(egmMode),
                nvidia_async_operation_utils::PatchEgmModeCallback{resp});

            return;
        }
    }

    BMCWEB_LOG_DEBUG("Performing Patch using set-property Call");

    // Set the property, with handler to check error responses
    dbus::utility::setProperty(
        service, cpuObjectPath, "com.nvidia.EgmMode", "EGMModeEnabled", egmMode,
        [resp, processorId](const boost::system::error_code& ec2,
                            const sdbusplus::message_t& msg) {
            egmAsyncRespHandler(resp, processorId, ec2, msg);
        });
}

/**
 * Handle the PATCH operation of the EGM Mode Property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       egmMode         New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchEgmMode(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                         const std::string& processorId, const bool egmMode,
                         const std::string& cpuObjectPath,
                         const dbus::utility::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;

    BMCWEB_LOG_DEBUG("PatchEgmMode path:{} with mode:{}", cpuObjectPath,
                     egmMode);

    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, "com.nvidia.EgmMode") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(" EgmMode interface not found ");
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        cpuObjectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, egmMode, processorId, cpuObjectPath,
         service =
             *inventoryService](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& obj) {
            egmGetDbusObjectHandler(resp, egmMode, processorId, cpuObjectPath,
                                    service, ec, obj);
        });
}

/*
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getSysGUID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get System-GUID");
    dbus::utility::getProperty<std::string>(
        service, objPath, "com.nvidia.SysGUID.SysGUID", "SysGUID",
        [objPath, asyncResp](const boost::system::error_code& ec,
                             const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaProcessor.v1_7_0.NvidiaGPU";
            mapStringOrNull(
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["MNNVLinkTopology"],
                "SystemGUID", &property);
        });
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */

inline void getCCModeData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& cpuId, const std::string& service,
                          const std::string& objPath)
{
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.CCMode",
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessor.v1_7_0.NvidiaGPU";
                if (property.first == "CCModeEnabled")
                {
                    const bool* ccModeEnabled =
                        std::get_if<bool>(&property.second);
                    if (ccModeEnabled == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["CCModeEnabled"] = *ccModeEnabled;
                }
                else if (property.first == "CCDevModeEnabled")
                {
                    const bool* ccDevModeEnabled =
                        std::get_if<bool>(&property.second);
                    if (ccDevModeEnabled == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["CCDevModeEnabled"] =
                        *ccDevModeEnabled;
                }
            }
        });
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getReconfigPermissionsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.InbandReconfigSettings",
        [aResp, cpuId, objPath](const boost::system::error_code& ec,
                                const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                return;
            }
            auto& json = aResp->res.jsonValue;
            auto reconfigPermissionsName =
                sdbusplus::object_path(objPath).filename();
            aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaProcessor.v1_7_0.NvidiaGPU";
            std::string reconfigPermissionsType;
            if (objPath.find("InbandReconfigPermissions") != std::string::npos)
            {
                reconfigPermissionsType = "InbandReconfigPermissions";
            }
            else
            {
                reconfigPermissionsType = "DOEReconfigPermissions";
            }
            auto& reconfigPermissionsJson =
                json["Oem"]["Nvidia"][reconfigPermissionsType]
                    [reconfigPermissionsName];

            for (const auto& property : properties)
            {
                if (property.first == "AllowOneShotConfig")
                {
                    const bool* allowOneShotConfig =
                        std::get_if<bool>(&property.second);
                    if (allowOneShotConfig == nullptr)
                    {
                        BMCWEB_LOG_ERROR("AllowOneShotConfig shall be boolean");
                        messages::internalError(aResp->res);
                        return;
                    }
                    reconfigPermissionsJson["AllowOneShotConfig"] =
                        *allowOneShotConfig;
                }
                else if (property.first == "AllowPersistentConfig")
                {
                    const bool* allowPersistentConfig =
                        std::get_if<bool>(&property.second);
                    if (allowPersistentConfig == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "AllowPersistentConfig shall be boolean");
                        messages::internalError(aResp->res);
                        return;
                    }
                    reconfigPermissionsJson["AllowPersistentConfig"] =
                        *allowPersistentConfig;
                }
                else if (property.first == "AllowFLRPersistentConfig")
                {
                    const bool* alowFLRPersistentConfig =
                        std::get_if<bool>(&property.second);
                    if (alowFLRPersistentConfig == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "AllowFLRPersistentConfig shall be boolean");
                        messages::internalError(aResp->res);
                        return;
                    }
                    reconfigPermissionsJson["AllowFLRPersistentConfig"] =
                        *alowFLRPersistentConfig;
                }
            }
        });
}

inline void getReconfigPermissionsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& objPath)
{
    // Ask for all objects implementing OperatingConfig so we can search
    // for one with a matching name
    dbus::utility::getSubTree(
        objPath, 0,
        std::array<std::string_view, 1>{"com.nvidia.InbandReconfigSettings"},
        [aResp, cpuId](boost::system::error_code ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec, ec.message());
                return;
            }
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                for (const auto& [serviceName, interfaceList] : serviceMap)
                {
                    getReconfigPermissionsData(aResp, cpuId, serviceName,
                                               objectPath);
                }
            }
        });
}

/**
 * @brief Fill out processor nvidia ErrorInjection info.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 */
inline void populateErrorInjectionData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId)
{
    redfish::processor_utils::getProcessorObject(
        aResp, cpuId,
        [](const std::shared_ptr<bmcweb::AsyncResp>& aResp2,
           const std::string& procId, const std::string& path,
           [[maybe_unused]] const dbus::utility::MapperServiceMap& serviceMap,
           [[maybe_unused]] const std::string& deviceType) {
            dbus::utility::getDbusObject(
                path + "/ErrorInjection", std::array<std::string_view, 0>(),
                [aResp2, procId,
                 path](const boost::system::error_code& ec,
                       const dbus::utility::MapperServiceMap& serviceMap2) {
                    if (ec)
                    {
                        return;
                    }

                    for (const auto& [_, interfaces] : serviceMap2)
                    {
                        if (std::ranges::find(
                                interfaces,
                                "com.nvidia.ErrorInjection.ErrorInjection") ==
                            interfaces.end())
                        {
                            continue;
                        }
                        aResp2->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaProcessor.v1_7_0.NvidiaGPU";
                        aResp2->res
                            .jsonValue["Oem"]["Nvidia"]["ErrorInjection"] = {
                            {"@odata.id",
                             "/redfish/v1/Systems/" +
                                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                 "/Processors/" + procId +
                                 "/Oem/Nvidia/ErrorInjection"}};
                        return;
                    }
                });
        });
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */

inline void getCCModePendingData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)

{
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.CCMode",
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            json["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaProcessor.v1_7_0.NvidiaGPU";
            for (const auto& property : properties)
            {
                if (property.first == "PendingCCModeState")
                {
                    const bool* pendingCCState =
                        std::get_if<bool>(&property.second);
                    if (pendingCCState == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get PendingCCModeState property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["CCModeEnabled"] = *pendingCCState;
                }
                else if (property.first == "PendingCCDevModeState")
                {
                    const bool* pendingCCDevState =
                        std::get_if<bool>(&property.second);
                    if (pendingCCDevState == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get PendingCCDevModeState property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["CCDevModeEnabled"] =
                        *pendingCCDevState;
                }
            }
        });
}

/**
 * @brief Populate Property SMUtilizationPercent by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getSMUtilizationData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor metrics SMUtilizationPercent data.");
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.SMUtilization",
        [aResp](const boost::system::error_code& ec,
                const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "SMUtilization")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Failed to get value of Property Utilization");
                        messages::internalError(aResp->res);
                        return;
                    }

                    aResp->res
                        .jsonValue["Oem"]["Nvidia"]["SMUtilizationPercent"] =
                        *value;
                }
            }
        });
}

inline void getNvLinkTotalCount(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.NVLink.NvLinkTotalCount",
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                if (property.first == "TotalNumberNVLinks")
                {
                    const uint64_t* totalNumberNVLinks =
                        std::get_if<uint64_t>(&property.second);
                    if (totalNumberNVLinks == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["TotalNumberNVLinks"] =
                        *totalNumberNVLinks;
                }
            }
        });
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */

inline void getPowerSmoothingInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, [[maybe_unused]] const std::string& service,
    [[maybe_unused]] const std::string& objPath)
{
    std::string powerSmoothingURI =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Processors/";
    powerSmoothingURI += processorId;
    powerSmoothingURI += "/Oem/Nvidia/PowerSmoothing";
    aResp->res.jsonValue["Oem"]["Nvidia"]["PowerSmoothing"]["@odata.id"] =
        powerSmoothingURI;
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getResetMetricsInfo(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& processorId,
                                [[maybe_unused]] const std::string& service,
                                const std::string& objPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/reset_statistics",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp, processorId](const boost::system::error_code& ec,
                             const std::vector<std::string>& /*resp*/) {
            if (ec)
            {
                if (ec == boost::system::errc::no_such_file_or_directory)
                {
                    // Log and skip if associated object path not found
                    BMCWEB_LOG_INFO(
                        "No ResetMetrics association endpoints found for processor: {}",
                        processorId);
                    return;
                }

                // For all other errors, log and return an internal error
                BMCWEB_LOG_ERROR(
                    "Failed to get ResetMetrics association endpoints: {}",
                    ec.message());
                messages::internalError(aResp->res);
                return;
            }

            // Construct the ResetMetrics URI and add it to the response
            std::string resetMetricsURI = std::format(
                "/redfish/v1/Systems/{}/Processors/{}/Oem/Nvidia/ProcessorResetMetrics",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, processorId);

            aResp->res.jsonValue["Oem"]["Nvidia"]["ProcessorResetMetrics"]
                                ["@odata.id"] = resetMetricsURI;

            BMCWEB_LOG_DEBUG("Added ResetMetrics URI: {}", resetMetricsURI);
        });
}

/**
 * @brief Build the CounterType parameter object on asyncResp from the
 *        ClearableCounters D-Bus property and assign it as the sole element
 *        of the ActionInfo Parameters array.
 *
 * Intended to be invoked from the getAllProperties callback in
 * getClearablePcieCounters.
 *
 * @param[in,out] asyncResp      Async HTTP response.
 * @param[in]     ec             D-Bus error code from getAllProperties.
 * @param[in]     propertiesList Properties returned for the ClearPCIeCounters
 *                               interface.
 */
inline void buildClearPcieCountersParameters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Get All call Failed for the interface. ec: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::vector<std::string> clearableDataSource;
    for (const auto& property : propertiesList)
    {
        const std::string& propertyName = property.first;
        if (propertyName == "ClearableCounters")
        {
            const std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&property.second);

            if (data != nullptr)
            {
                for (const auto& counter : *data)
                {
                    clearableDataSource.push_back(
                        counter.substr(counter.find_last_of('.') + 1));
                }
            }
        }
    }

    nlohmann::json parameter;
    parameter["Name"] = "CounterType";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    parameter["AllowableValues"] = clearableDataSource;
    asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array({parameter});
}

inline void getClearablePcieCounters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath,
    const std::string& interface)
{
    dbus::utility::getAllProperties(
        service, objPath, interface,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
            buildClearPcieCountersParameters(asyncResp, ec, propertiesList);
        });
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       processorId       Processor ID.
 * @param[in]       portId       Processor ID.
 */
inline void getClearPCIeCountersActionInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 2>{
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Accelerator"},
        [processorId, portId,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path + "/all_states",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, processorId,
                     portId](const boost::system::error_code& e,
                             const std::vector<std::string>& resp) {
                        if (e)
                        {
                            // no state sensors attached.
                            BMCWEB_LOG_ERROR(
                                "Object Mapper call failed while finding all_states association, with error {}",
                                e);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& sensorpath : resp)
                        {
                            // Check Interface in Object or not
                            BMCWEB_LOG_DEBUG(
                                "processor state sensor object path {}",
                                sensorpath);
                            dbus::utility::getDbusObject(
                                sensorpath,
                                std::array<std::string_view, 1>(
                                    {"xyz.openbmc_project.Inventory.Item.Port"}),
                                [asyncResp, sensorpath, processorId, portId](
                                    const boost::system::error_code ec3,
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        interfaceObj) {
                                    if (ec3)
                                    {
                                        // the path does not implement port
                                        // interfaces
                                        BMCWEB_LOG_DEBUG(
                                            "no port interface on object path {}",
                                            sensorpath);
                                        return;
                                    }

                                    sdbusplus::object_path objectPath(
                                        sensorpath);
                                    if (objectPath.filename() != portId)
                                    {
                                        return;
                                    }

                                    std::string clearPcieCountersActionInfoUri =
                                        std::format(
                                            "/redfish/v1/Systems/{}/Processors/",
                                            BMCWEB_REDFISH_SYSTEM_URI_NAME);
                                    clearPcieCountersActionInfoUri +=
                                        processorId;
                                    clearPcieCountersActionInfoUri += "/Ports/";
                                    clearPcieCountersActionInfoUri +=
                                        portId +
                                        "/Metrics/Oem/Nvidia/ClearPCIeCountersActionInfo";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        clearPcieCountersActionInfoUri;
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#ActionInfo.v1_2_0.ActionInfo";
                                    asyncResp->res.jsonValue["Name"] =
                                        "ClearPCIeCounters Action Info";
                                    asyncResp->res.jsonValue["Id"] =
                                        "ClearPCIeCountersActionInfo";

                                    for (const auto& [service, interfaces] :
                                         interfaceObj)
                                    {
                                        for (const auto& interface : interfaces)
                                        {
                                            if (interface ==
                                                "xyz.openbmc_project.PCIe.ClearPCIeCounters")
                                            {
                                                getClearablePcieCounters(
                                                    asyncResp, service,
                                                    sensorpath, interface);
                                                return;
                                            }
                                        }
                                    }
                                });
                        }
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        });
}

inline void getPortLinkStatusSetting(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& portPath, const std::string& service,
    const std::vector<uint8_t>& portsToDisable)
{
    dbus::utility::getAllProperties(
        service, portPath, "xyz.openbmc_project.Inventory.Item.Port",
        [aResp,
         portsToDisable](const boost::system::error_code& ec,
                         const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "PortNumber")
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for port number");
                        messages::internalError(aResp->res);
                        return;
                    }

                    if (*value == 0)
                    {
                        // no error
                        // Ports other than NVLinks will have default value
                        // (value-0) for PortNumber property on pdi. Valid
                        // values for link disable are 1 based.
                        return;
                    }

                    // check port number if present in vector
                    auto it = std::ranges::find(portsToDisable, *value);
                    if (it != portsToDisable.end())
                    {
                        aResp->res.jsonValue["LinkState"] = "Disabled";
                    }
                    else
                    {
                        aResp->res.jsonValue["LinkState"] = "Enabled";
                    }
                }
            }
        });
}

inline void getPortDisableFutureStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap,
    const std::string& portId)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList,
                              "com.nvidia.NVLink.NVLinkDisableFuture") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        // no interface = no failure
        BMCWEB_LOG_DEBUG(
            "NVLinkDisableFuture interface not found in getPortDisableFutureStatus");
        return;
    }

    dbus::utility::getAllProperties(
        *inventoryService, objectPath, "com.nvidia.NVLink.NVLinkDisableFuture",
        [aResp, processorId, portId,
         objectPath](const boost::system::error_code& ec,
                     const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                // no NVLinkDisableFuture = no failure
                return;
            }
            std::vector<uint8_t> portsToDisable;

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "PortDisableFuture")
                {
                    const auto* value =
                        std::get_if<std::vector<uint8_t>>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Port Disable Future mask");
                        messages::internalError(aResp->res);
                        return;
                    }
                    portsToDisable = *value;
                }
            }

            dbus::utility::getProperty<std::vector<std::string>>(
                "xyz.openbmc_project.ObjectMapper", objectPath + "/all_states",
                "xyz.openbmc_project.Association", "endpoints",
                [aResp, processorId, portId,
                 portsToDisable](const boost::system::error_code ec1,
                                 const std::vector<std::string>& resp) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error");
                        messages::internalError(aResp->res);
                        return;
                    }

                    for (const std::string& portPath : resp)
                    {
                        sdbusplus::object_path pPath(portPath);
                        if (pPath.filename() != portId)
                        {
                            continue;
                        }

                        dbus::utility::getDbusObject(
                            portPath,
                            std::array<std::string_view, 1>(
                                {"xyz.openbmc_project.Inventory.Item.Port"}),
                            [aResp, processorId, portId, portPath,
                             portsToDisable](
                                const boost::system::error_code ec2,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    object) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_DEBUG("No port interface on {}",
                                                     portPath);
                                    return;
                                }
                                getPortLinkStatusSetting(aResp, portPath,
                                                         object.front().first,
                                                         portsToDisable);
                            });
                    }
                });
        });
}

inline void getPortNumberAndCallSetAsync(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, const std::string& portId,
    const std::string& propertyValue, const std::string& propertyName,
    const std::string& processorPath, const std::string& processorService,
    const std::string& portService, const std::string& portPath,
    const std::vector<uint8_t>& portsToDisable)
{
    dbus::utility::getAllProperties(
        portService, portPath, "xyz.openbmc_project.Inventory.Item.Port",
        [aResp, processorId, portId, propertyValue, propertyName, processorPath,
         processorService,
         portsToDisable](const boost::system::error_code& ec,
                         const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                const std::string& propName = property.first;
                if (propName == "PortNumber")
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for port number");
                        messages::internalError(aResp->res);
                        return;
                    }
                    uint32_t portNumber = static_cast<uint32_t>(*value);

                    dbus::utility::getDbusObject(
                        processorPath,
                        std::array<std::string_view, 1>{
                            nvidia_async_operation_utils::
                                setAsyncInterfaceName},
                        [aResp, propertyValue, propertyName, portNumber,
                         processorId, processorPath, processorService,
                         portsToDisable](
                            const boost::system::error_code& ec1,
                            const dbus::utility::MapperGetObject& object) {
                            if (!ec1)
                            {
                                for (const auto& [serv, _] : object)
                                {
                                    if (serv != processorService)
                                    {
                                        continue;
                                    }

                                    std::vector<uint8_t> portListToDisable =
                                        portsToDisable;
                                    auto it =
                                        std::ranges::find(portListToDisable,

                                                          portNumber);
                                    if (propertyValue == "Disabled")
                                    {
                                        if (it == portListToDisable.end())
                                        {
                                            portListToDisable.push_back(
                                                static_cast<uint8_t>(
                                                    portNumber));
                                        }
                                    }
                                    else if (propertyValue == "Enabled")
                                    {
                                        if (it != portListToDisable.end())
                                        {
                                            portListToDisable.erase(it);
                                        }
                                    }
                                    else
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Invalid value for patch on property {}",
                                            propertyName);
                                        messages::internalError(aResp->res);
                                        return;
                                    }

                                    BMCWEB_LOG_DEBUG(
                                        "Performing Patch using Set Async Method Call for {}",
                                        propertyName);

                                    nvidia_async_operation_utils::
                                        doGenericSetAsyncAndGatherResult(
                                            aResp, std::chrono::seconds(60),
                                            processorService, processorPath,
                                            "com.nvidia.NVLink.NVLinkDisableFuture",
                                            propertyName,
                                            std::variant<std::vector<uint8_t>>(
                                                portListToDisable),
                                            nvidia_async_operation_utils::
                                                PatchPortDisableCallback{
                                                    aResp});
                                    return;
                                }
                            }
                        });
                }
            }
        });
}

inline void patchPortDisableFuture(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, const std::string& portId,
    const std::string& propertyValue, const std::string& propertyName,
    const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList,
                              "com.nvidia.NVLink.NVLinkDisableFuture") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "NVLinkDisableFuture interface not found while {} patch",
            propertyName);
        return;
    }

    dbus::utility::getAllProperties(
        *inventoryService, objectPath, "com.nvidia.NVLink.NVLinkDisableFuture",
        [aResp, processorId, portId, propertyValue, propertyName, objectPath,
         service = *inventoryService](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            std::vector<uint8_t> portsToDisable;

            for (const auto& property : properties)
            {
                const std::string& propName = property.first;
                if (propName == "PortDisableFuture")
                {
                    const auto* value =
                        std::get_if<std::vector<uint8_t>>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Port Disable Future mask");
                        messages::internalError(aResp->res);
                        return;
                    }
                    portsToDisable = *value;
                }
            }
            dbus::utility::getProperty<std::vector<std::string>>(
                "xyz.openbmc_project.ObjectMapper", objectPath + "/all_states",
                "xyz.openbmc_project.Association", "endpoints",
                [aResp, processorId, portId, propertyValue, propertyName,
                 objectPath, service,
                 portsToDisable](const boost::system::error_code ec1,
                                 const std::vector<std::string>& resp) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error");
                        messages::internalError(aResp->res);
                        return;
                    }

                    for (const std::string& portPath : resp)
                    {
                        // Get the portId object
                        sdbusplus::object_path pPath(portPath);
                        if (pPath.filename() != portId)
                        {
                            continue;
                        }

                        dbus::utility::getDbusObject(
                            portPath,
                            std::array<std::string_view, 1>(
                                {"xyz.openbmc_project.Inventory.Item.Port"}),
                            [aResp, processorId, portId, portPath,
                             propertyValue, propertyName, objectPath, service,
                             portsToDisable](
                                const boost::system::error_code ec2,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    object) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_DEBUG("No port interface on {}",
                                                     portPath);
                                    return;
                                }

                                getPortNumberAndCallSetAsync(
                                    aResp, processorId, portId, propertyValue,
                                    propertyName, objectPath, service,
                                    object.front().first, portPath,
                                    portsToDisable);
                            });
                    }
                });
        });
}

inline std::string getLinkDownReasonCode(const std::string& linkDownReasonCode)
{
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.NoLinkDown")
    {
        return "NoLinkDown";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.Unknown")
    {
        return "Unknown";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.HighBitErrorRate")
    {
        return "HighBitErrorRate";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.BlockLockLost")
    {
        return "BlockLockLost";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.AlignmentLost")
    {
        return "AlignmentLost";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.FECSyncLost")
    {
        return "FECSyncLost";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PllLockLost")
    {
        return "PllLockLost";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.FIFOOverflow")
    {
        return "FIFOOverflow";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.FalseSkipDetected")
    {
        return "FalseSkipDetected";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.MinorErrorThresholdExceeded")
    {
        return "MinorErrorThresholdExceeded";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PhyRetransmitTimeout")
    {
        return "PhyRetransmitTimeout";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.HeartbeatErrors")
    {
        return "HeartbeatErrors";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.CreditMonitorWatchdogTimeout")
    {
        return "CreditMonitorWatchdogTimeout";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.LinkLayerIntegrityThresholdExceeded")
    {
        return "LinkLayerIntegrityThresholdExceeded";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.LinkLayerBufferOverrun")
    {
        return "LinkLayerBufferOverrun";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.OOBCommandLinkHealthy")
    {
        return "OOBCommandLinkHealthy";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.OOBCommandLinkHighBER")
    {
        return "OOBCommandLinkHighBER";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.InbandCommandLinkHealthy")
    {
        return "InbandCommandLinkHealthy";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.InbandCommandLinkHighBER")
    {
        return "InbandCommandLinkHighBER";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.VerificationGatewayDown")
    {
        return "VerificationGatewayDown";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.RemoteFaultReceived")
    {
        return "RemoteFaultReceived";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.TrainingSequenceReceived")
    {
        return "TrainingSequenceReceived";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.ManagementCommandDown")
    {
        return "ManagementCommandDown";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.CableDisconnected")
    {
        return "CableDisconnected";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.CableAccessFault")
    {
        return "CableAccessFault";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.ThermalShutdown")
    {
        return "ThermalShutdown";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.CurrentIssue")
    {
        return "CurrentIssue";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PowerBudgetExceeded")
    {
        return "PowerBudgetExceeded";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.FastRawBERRecovery")
    {
        return "FastRawBERRecovery";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.FastEffectiveBERRecovery")
    {
        return "FastEffectiveBERRecovery";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.FastSymbolBERRecovery")
    {
        return "FastSymbolBERRecovery";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.FastCreditWatchdogRecovery")
    {
        return "FastCreditWatchdogRecovery";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PeerSleep")
    {
        return "PeerSleep";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PeerDisabled")
    {
        return "PeerDisabled";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PeerDisableLocked")
    {
        return "PeerDisableLocked";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PeerThermalEvent")
    {
        return "PeerThermalEvent";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PeerForcedEvent")
    {
        return "PeerForcedEvent";
    }
    if (linkDownReasonCode ==
        "xyz.openbmc_project.Metrics.IBPort.LinkDownReasonCodes.PeerResetEvent")
    {
        return "PeerResetEvent";
    }
    return "Unknown";
}

inline std::string getEarlyHealthIndication(
    const std::string& earlyHealthIndication)
{
    if (earlyHealthIndication ==
        "com.nvidia.NVLink.PortHealthMetrics.EarlyHealthIndicationValues"
        ".Unknown")
    {
        return "Unknown";
    }
    if (earlyHealthIndication ==
        "com.nvidia.NVLink.PortHealthMetrics.EarlyHealthIndicationValues"
        ".Attention")
    {
        return "Attention";
    }
    if (earlyHealthIndication ==
        "com.nvidia.NVLink.PortHealthMetrics.EarlyHealthIndicationValues"
        ".Healthy")
    {
        return "Healthy";
    }
    return "";
}

inline std::string getAttentionTriggerReason(
    const std::string& attentionTriggerReason)
{
    static constexpr std::string_view prefix =
        "com.nvidia.NVLink.PortHealthMetrics.AttentionTriggerReasonValues.";
    if (!attentionTriggerReason.starts_with(prefix))
    {
        return "Unknown";
    }
    std::string value(attentionTriggerReason.substr(prefix.size()));
    // D-Bus and Redfish both use "Unknown" for the not-applicable state
    if (value == "Unknown")
    {
        return "Unknown";
    }
    // Whitelist schema-valid enum members; unknown values → Unknown
    if (value == "RawBER" || value == "EffectiveBER" || value == "SymbolBER" ||
        value == "PLRTXBandwidthLoss" || value == "PLRRXBandwidthLoss" ||
        value == "RecoveryBandwidthLoss" || value == "PortTotalBandwidthLoss" ||
        value == "LinkDownCount" || value == "SymbolErrorCount")
    {
        return value;
    }
    return "Unknown";
}

inline void getWorkLoadPowerInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId)
{
    std::string powerProfileURI =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Processors/";
    powerProfileURI += processorId;
    powerProfileURI += "/Oem/Nvidia/WorkloadPowerProfile";
    aResp->res.jsonValue["Oem"]["Nvidia"]["WorkloadPowerProfile"]["@odata.id"] =
        powerProfileURI;
}

inline std::string convertMsbToLsb(const std::string* ibGuid)
{
    std::string lsb = *ibGuid;

    if (lsb.size() % 2 != 0)
    {
        BMCWEB_LOG_ERROR("Invalid IBGUID size");
        return "";
    }

    for (uint32_t i = 0; i < lsb.size(); i += 2)
    {
        std::swap(lsb[i], lsb[i + 1]);
    }

    // Reverse entire string
    std::ranges::reverse(lsb);

    return lsb;
}

inline void getMNNVLinkTopologyInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath,
    const std::string& interface)
{
    dbus::utility::getAllProperties(
        service, objPath, interface,
        [aResp, cpuId](const boost::system::error_code& ec,
                       const dbus::utility::DBusPropertiesMap& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            nlohmann::json& json = aResp->res.jsonValue;

            const std::string* chassisSerialNumber = nullptr;
            const std::string* ibGuid = nullptr;
            const std::string* traySerialNumber = nullptr;
            const std::string* systemGUID = nullptr;
            const std::string* peerType = nullptr;
            const uint32_t* moduleID = nullptr;
            const uint32_t* hostID = nullptr;
            const uint32_t* traySlotIndex = nullptr;
            const uint32_t* traySlotNumber = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), resp, "ChassisSerialNumber",
                chassisSerialNumber, "IBGUID", ibGuid, "TraySerialNumber",
                traySerialNumber, "SystemGUID", systemGUID, "ModuleID",
                moduleID, "HostID", hostID, "PeerType", peerType,
                "TraySlotIndex", traySlotIndex, "TraySlotNumber",
                traySlotNumber);

            if (!success)
            {
                BMCWEB_LOG_ERROR("failed to unpack");
                messages::internalError(aResp->res);
                return;
            }

            if (chassisSerialNumber != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]
                    ["ChassisSerialNumber"] = *chassisSerialNumber;
            }

            if (ibGuid != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["IBGUID"] = *ibGuid;
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["LsbIBGUID"] =
                    convertMsbToLsb(ibGuid);
            }

            if (traySerialNumber != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["TraySerialNumber"] =
                    *traySerialNumber;
            }

            if (systemGUID != nullptr && !systemGUID->empty())
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["SystemGUID"] =
                    *systemGUID;
            }

            if (moduleID != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["ModuleID"] =
                    *moduleID;
            }

            if (hostID != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["HostID"] = *hostID;
            }

            if (peerType != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["PeerType"] =
                    *peerType;
            }

            if (traySlotIndex != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["TraySlotIndex"] =
                    *traySlotIndex;
            }

            if (traySlotNumber != nullptr)
            {
                json["Oem"]["Nvidia"]["MNNVLinkTopology"]["TraySlotNumber"] =
                    *traySlotNumber;
            }
        });
}

inline void clearPCIeCounter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connection, const std::string& path,
    const std::string& counterType)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.PCIe.ClearPCIeCounters"},
        [asyncResp, path, connection,
         counterType](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != connection)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                    nvidia_async_operation_utils::
                        doGenericCallAsyncAndGatherResult<int>(
                            asyncResp, std::chrono::seconds(60), connection,
                            path, "xyz.openbmc_project.PCIe.ClearPCIeCounters",
                            "ClearCounter",
                            [asyncResp, counterType](
                                const std::string& status,
                                [[maybe_unused]] const int* retValue) {
                                if (status == nvidia_async_operation_utils::
                                                  asyncStatusValueSuccess)
                                {
                                    BMCWEB_LOG_DEBUG("Clear Counter Succeeded");
                                    messages::success(asyncResp->res);
                                    return;
                                }
                                BMCWEB_LOG_ERROR(
                                    "Clear Counter Throws error {}", status);
                                messages::internalError(asyncResp->res);
                            },
                            counterType);

                    return;
                }
            }
        });
};

inline void postPCIeClearCounter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& portId,
    const std::string& counterType)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 2>{
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Accelerator"},
        [processorId, portId, asyncResp,
         counterType](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path + "/all_states",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, processorId, portId,
                     counterType](const boost::system::error_code& e,
                                  const std::vector<std::string>& resp) {
                        if (e)
                        {
                            // no state sensors attached.
                            BMCWEB_LOG_ERROR(
                                "Object Mapper call failed while finding all_states association, with error {}",
                                e);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& sensorpath : resp)
                        {
                            // Check Interface in Object or not
                            BMCWEB_LOG_DEBUG(
                                "processor state sensor object path {}",
                                sensorpath);

                            sdbusplus::object_path path1(sensorpath);
                            if (path1.filename() != portId)
                            {
                                continue;
                            }

                            dbus::utility::getDbusObject(
                                sensorpath,
                                std::array<std::string_view, 2>(
                                    {"xyz.openbmc_project.Inventory.Item.Port",
                                     "xyz.openbmc_project.PCIe.ClearPCIeCounters"}),
                                [asyncResp, sensorpath, portId, counterType](
                                    const boost::system::error_code ec1,
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        interfaceObj) {
                                    if (ec1)
                                    {
                                        // the path does not implement port
                                        // interfaces
                                        BMCWEB_LOG_DEBUG(
                                            "no port interface on object path {}",
                                            sensorpath);
                                        return;
                                    }

                                    for (const auto& [connection, interfaces] :
                                         interfaceObj)
                                    {
                                        clearPCIeCounter(asyncResp, connection,
                                                         sensorpath,
                                                         counterType);
                                    }
                                });
                        }
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        });
}

inline void setOperatingSpeedRange(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::variant<uint32_t, std::tuple<uint32_t, uint32_t>>& value,
    const std::string& patchProp, const std::string& path)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>(
            {"xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig"}),
        [asyncResp, path, value, patchProp](
            const boost::system::error_code errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                                 errorno);
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& [service, interfaces] : objInfo)
            {
                if (std::ranges::find(
                        interfaces,
                        nvidia_async_operation_utils::setAsyncInterfaceName) ==
                    interfaces.end())
                {
                    continue;
                }

                if (patchProp == "SettingRange")
                {
                    const std::tuple<uint32_t, uint32_t>* requestedLimit =
                        std::get_if<std::tuple<uint32_t, uint32_t>>(&value);
                    std::vector<std::tuple<std::string, uint32_t>> clockLimits;
                    clockLimits.emplace_back("SettingMin",
                                             std::get<0>(*requestedLimit));
                    clockLimits.emplace_back("SettingMax",
                                             std::get<1>(*requestedLimit));
                    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                        asyncResp, std::chrono::seconds(60), service, path,
                        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                        "RequestedSpeedLimits",
                        std::variant<
                            std::vector<std::tuple<std::string, uint32_t>>>(
                            clockLimits),
                        nvidia_async_operation_utils::
                            PatchClockLimitControlCallback{asyncResp});
                }
                else if (patchProp == "SettingMin")

                {
                    const uint32_t* settingMin = std::get_if<uint32_t>(&value);
                    std::vector<std::tuple<std::string, uint32_t>> clockLimits;
                    clockLimits.emplace_back("SettingMin", *settingMin);
                    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                        asyncResp, std::chrono::seconds(60), service, path,
                        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                        "RequestedSpeedLimits",
                        std::variant<
                            std::vector<std::tuple<std::string, uint32_t>>>(
                            clockLimits),
                        nvidia_async_operation_utils::
                            PatchClockLimitControlCallback{asyncResp});
                }
                else if (patchProp == "SettingMax")

                {
                    const uint32_t* settingMax = std::get_if<uint32_t>(&value);
                    std::vector<std::tuple<std::string, uint32_t>> clockLimits;
                    clockLimits.emplace_back("SettingMax", *settingMax);
                    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                        asyncResp, std::chrono::seconds(60), service, path,
                        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                        "RequestedSpeedLimits",
                        std::variant<
                            std::vector<std::tuple<std::string, uint32_t>>>(
                            clockLimits),
                        nvidia_async_operation_utils::
                            PatchClockLimitControlCallback{asyncResp});
                }

                else
                {
                    BMCWEB_LOG_ERROR("Invalid patch properrty name: {}",
                                     patchProp);
                }

                return;
            }
        });
}

/**
 * Handle the PATCH operation of the OperatingSpeedRangeMHz Property
 * SettingMin/SettingMax.
 *
 * @param[in,out]   asyncResp          Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       value           value of the property to be patched.
 * @param[in]       patchProp       string representing property name
 * SettingMin/SettingMax
 * @param[in]       processorObjPath   Path of processor object used to get
 * clockLimit control path.
 */

inline void patchOperatingSpeedRangeMHz(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId,
    const std::variant<uint32_t, std::tuple<uint32_t, uint32_t>>& value,
    const std::string& patchProp, const std::string& processorObjPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper",
        processorObjPath + "/parent_chassis", "xyz.openbmc_project.Association",
        "endpoints",
        [asyncResp, value, patchProp, processorId,
         processorObjPath](const boost::system::error_code& ec,
                           const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("ObjectMapper call failed with error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& chassisPath : resp)
            {
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    chassisPath + "/clock_controls",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, value, patchProp, processorId,
                     chassisPath](const boost::system::error_code& ec2,
                                  const std::vector<std::string>& resp2) {
                        if (ec2)
                        {
                            return; // no clock Limit Path for the chassis
                                    // path
                        }

                        for (const auto& clockLimitPath : resp2)
                        {
                            setOperatingSpeedRange(asyncResp, value, patchProp,
                                                   clockLimitPath);
                        }
                    });
            }
        });
}

inline void getOperatingSpeedRangeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path)
{
    dbus::utility::getDbusObject(
        path, std::array<std::string_view, 0>(),
        [asyncResp, path](
            const boost::system::error_code errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                                 errorno);
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& element : objInfo)
            {
                for (const auto& interface : element.second)
                {
                    if (interface ==
                        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig")
                    {
                        dbus::utility::getAllProperties(
                            element.first, path, interface,
                            [asyncResp, path, interface](
                                const boost::system::error_code& errono1,
                                const dbus::utility::DBusPropertiesMap&
                                    propertiesList) {
                                if (errono1)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS Response Error:{} while calling GetAll",
                                        errono1);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const auto& property : propertiesList)
                                {
                                    std::string propertyName = property.first;
                                    if (propertyName == "MaxSpeed")
                                    {
                                        propertyName = "AllowableMax";
                                        const uint32_t* value =
                                            std::get_if<uint32_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for AllowableMax");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res
                                            .jsonValue["OperatingSpeedRangeMHz"]
                                                      [propertyName] = *value;
                                        continue;
                                    }
                                    if (propertyName == "MinSpeed")
                                    {
                                        propertyName = "AllowableMin";
                                        const uint32_t* value =
                                            std::get_if<uint32_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for AllowableMin");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res
                                            .jsonValue["OperatingSpeedRangeMHz"]
                                                      [propertyName] = *value;
                                        continue;
                                    }
                                    if (propertyName == "RequestedSpeedLimits")
                                    {
                                        const std::tuple<uint32_t, uint32_t>*
                                            value = std::get_if<
                                                std::tuple<uint32_t, uint32_t>>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for RequestedSpeedLimits");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res
                                            .jsonValue["OperatingSpeedRangeMHz"]
                                                      ["SettingMin"] =
                                            std::get<0>(*value);
                                        asyncResp->res
                                            .jsonValue["OperatingSpeedRangeMHz"]
                                                      ["SettingMax"] =
                                            std::get<1>(*value);
                                        continue;
                                    }
                                }
                            });
                    }
                }
            }
        });
}

/**
 * @brief Fill out the operating speed range of clock for the processor.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getOperatingSpeedRange(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp, objPath](const boost::system::error_code& ec,
                         const std::vector<std::string>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }

            for (const auto& chassisPath : resp)
            {
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    chassisPath + "/clock_controls",
                    "xyz.openbmc_project.Association", "endpoints",
                    [aResp,
                     chassisPath](const boost::system::error_code ec1,
                                  const std::vector<std::string>& chassisResp) {
                        if (ec1)
                        {
                            return; // no chassis = no failures
                        }
                        for (const auto& clockControlPath : chassisResp)
                        {
                            aResp->res.jsonValue["OperatingSpeedRangeMHz"]
                                                ["DataSourceUri"] =
                                "/redfish/v1/Chassis/" +
                                chassisPath.substr(
                                    chassisPath.find_last_of('/') + 1) +
                                "/Controls/" +
                                clockControlPath.substr(
                                    clockControlPath.find_last_of('/') + 1);
                            getOperatingSpeedRangeData(aResp, clockControlPath);
                        }
                    });
            }
        });
}

// PCIe root-port link enable mask (com.nvidia.PCIe.LinkEnableMask published
// by pldmd on an effecter reached through the pcie_link_controls association
// pldmd places on the processor inventory object), surfaced as the CPU OEM
// property Oem.Nvidia.PCIeLinkEnableMask.
constexpr const char* pcieLinkEnableMaskInterface =
    "com.nvidia.PCIe.LinkEnableMask";
constexpr const char* pcieLinkEnableMaskStatusInterface =
    "xyz.openbmc_project.State.Decorator.OperationalStatus";

// Mask and SupportedMask render at the same width so a client can compare them
// digit for digit. The width follows the device-reported mask; EnableMask is
// OR-ed in so a backend value with stray high bits is never truncated by
// intToHexString, which pads and truncates to exactly the digits requested.
inline size_t pcieLinkEnableMaskHexDigits(uint64_t enableMask,
                                          uint64_t supportedMask)
{
    return std::max<size_t>(
        1,
        (static_cast<size_t>(std::bit_width(enableMask | supportedMask)) + 3) /
            4);
}

inline std::string formatPCIeLinkEnableMask(uint64_t mask, size_t digits)
{
    return "0x" + intToHexString(mask, digits);
}

// The mask is writable only while host firmware is polling for authorization:
// OperationalStatus Enabled, or Deferring (DSP0248 ENABLED_UPDATEPENDING, a set
// in flight but still inside the same window). Every other state - including
// Starting before the host publishes its boot data, and UnavailableOffline when
// the terminus is lost - reports not-writable.
inline bool isPCIeLinkEnableMaskWritable(std::string_view state)
{
    return state ==
               "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Enabled" ||
           state ==
               "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Deferring";
}

// Seconds advertised in Retry-After whenever the mask is not writable right
// now. Matches the length of the UEFI wait, so a client that retries on the
// header lands inside the next window rather than after it.
constexpr const char* pcieLinkEnableMaskRetryAfter = "60";

// A refused write is never a client mistake - the value was valid, the window
// was shut - so the resolution has to say what the operator does next. The
// stock ServiceTemporarilyUnavailable resolution only says to wait.
constexpr const char* pcieLinkEnableMaskWindowClosedResolution =
    "The mask is not accepted right now. Host firmware accepts it only during "
    "the boot window it waits in, which Oem/Nvidia/PCIeLinkEnableMask/Writable "
    "reports. Poll GET on this Processor until Writable reads true, then "
    "re-send the same PATCH; nothing was applied.";

constexpr const char* pcieLinkEnableMaskUnavailableResolution =
    "The mask is not accepted right now. Oem/Nvidia/PCIeLinkEnableMask is "
    "unavailable on this Processor, so nothing was applied. Poll GET on this "
    "Processor until Oem/Nvidia/PCIeLinkEnableMask/Writable reads true, then "
    "re-send the same PATCH.";

inline void populatePCIeLinkEnableMask(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, uint64_t enableMask,
    uint64_t supportedMask, bool writable)
{
    const size_t digits =
        pcieLinkEnableMaskHexDigits(enableMask, supportedMask);
    nlohmann::json& oemNvidia = aResp->res.jsonValue["Oem"]["Nvidia"];
    oemNvidia["@odata.type"] = "#NvidiaProcessor.v1_9_0.NvidiaCPU";
    nlohmann::json& maskBlock = oemNvidia["PCIeLinkEnableMask"];
    maskBlock["Mask"] = formatPCIeLinkEnableMask(enableMask, digits);
    maskBlock["SupportedMask"] =
        formatPCIeLinkEnableMask(supportedMask, digits);
    maskBlock["Writable"] = writable;
}

inline void getPCIeLinkEnableMaskData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& effecterPath)
{
    dbus::utility::getDbusObject(
        effecterPath,
        std::array<std::string_view, 1>{pcieLinkEnableMaskInterface},
        [aResp, effecterPath](const boost::system::error_code& ec,
                              const dbus::utility::MapperGetObject& objInfo) {
            if (ec || objInfo.empty())
            {
                BMCWEB_LOG_ERROR(
                    "GetObject for {} on {} failed ({}, {} owner(s)); Processor GET omits PCIeLinkEnableMask",
                    pcieLinkEnableMaskInterface, effecterPath, ec.message(),
                    objInfo.size());
                return;
            }
            const std::string& service = objInfo.begin()->first;
            dbus::utility::getAllProperties(
                service, effecterPath, pcieLinkEnableMaskInterface,
                [aResp, effecterPath, service](
                    const boost::system::error_code& ec1,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
                    if (ec1)
                    {
                        // Omit the block rather than fail the whole Processor
                        // GET: one unreadable OEM effecter must not take down
                        // a resource whose standard properties are all fine.
                        BMCWEB_LOG_ERROR(
                            "GetAll {} on {} from {} failed ({}); Processor GET omits PCIeLinkEnableMask",
                            pcieLinkEnableMaskInterface, effecterPath, service,
                            ec1.message());
                        return;
                    }
                    std::optional<uint64_t> enableMask;
                    std::optional<uint64_t> supportedMask;
                    for (const auto& [propertyName, value] : propertiesList)
                    {
                        if (propertyName == "EnableMask" &&
                            std::holds_alternative<uint64_t>(value))
                        {
                            enableMask = std::get<uint64_t>(value);
                        }
                        else if (propertyName == "SupportedMask" &&
                                 std::holds_alternative<uint64_t>(value))
                        {
                            supportedMask = std::get<uint64_t>(value);
                        }
                    }
                    if (!enableMask || !supportedMask)
                    {
                        const char* missing = "SupportedMask";
                        if (!enableMask && !supportedMask)
                        {
                            missing = "EnableMask and SupportedMask";
                        }
                        else if (!enableMask)
                        {
                            missing = "EnableMask";
                        }
                        BMCWEB_LOG_ERROR(
                            "{} on {} published no uint64 {}; Processor GET omits PCIeLinkEnableMask",
                            pcieLinkEnableMaskInterface, effecterPath, missing);
                        return;
                    }
                    // Writable comes from the effecter operational state that
                    // the NumericEffecter base publishes on the same object.
                    // The block is rendered whole, so a failed State read
                    // reports not-writable rather than dropping the mask the
                    // client came for - a GET must answer in every state.
                    dbus::utility::getProperty<std::string>(
                        service, effecterPath,
                        pcieLinkEnableMaskStatusInterface, "State",
                        [aResp, effecterPath, mask = *enableMask,
                         supported = *supportedMask](
                            const boost::system::error_code& ec2,
                            const std::string& state) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Get {} State on {} failed ({}); PCIeLinkEnableMask reports Writable false",
                                    pcieLinkEnableMaskStatusInterface,
                                    effecterPath, ec2.message());
                                populatePCIeLinkEnableMask(aResp, mask,
                                                           supported, false);
                                return;
                            }
                            populatePCIeLinkEnableMask(
                                aResp, mask, supported,
                                isPCIeLinkEnableMaskWritable(state));
                        });
                });
        });
}

inline void getPCIeLinkEnableMask(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/pcie_link_controls",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec,
                const std::vector<std::string>& resp) {
            if (ec)
            {
                return; // association absent = feature absent
            }
            for (const auto& effecterPath : resp)
            {
                getPCIeLinkEnableMaskData(aResp, effecterPath);
            }
        });
}

class PatchPCIeLinkEnableMaskCallback
{
  public:
    explicit PatchPCIeLinkEnableMaskCallback(
        std::shared_ptr<bmcweb::AsyncResp> response, std::string mask,
        std::string effecterPath) :
        resp(std::move(response)), maskStr(std::move(mask)),
        path(std::move(effecterPath))
    {}

    void operator()(const std::string& status) const
    {
        if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
        {
            BMCWEB_LOG_INFO("PCIeLinkEnableMask {} accepted for {}", maskStr,
                            path);
            messages::success(resp->res);
            return;
        }

        if (status ==
            nvidia_async_operation_utils::asyncStatusValueWriteFailure)
        {
            // The device rejected the write after the gates passed.
            BMCWEB_LOG_ERROR(
                "PCIeLinkEnableMask {} not applied: {} reported a device write failure",
                maskStr, path);
            messages::operationFailed(resp->res);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueUnavailable)
        {
            // mapDbusErrorNameToAsyncStatus folds NotAllowed into Unavailable,
            // so this one status covers both the closed window and the
            // unreachable terminus. The resolution names the window, which is
            // the case an operator can act on.
            BMCWEB_LOG_WARNING(
                "PCIeLinkEnableMask {} not applied: {} is not writable (window closed or terminus unreachable)",
                maskStr, path);
            messages::serviceTemporarilyUnavailableMsg(
                resp->res, pcieLinkEnableMaskRetryAfter,
                pcieLinkEnableMaskWindowClosedResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueTimeout)
        {
            std::string errTimeout = "0x600";
            std::string errTimeoutResolution =
                "Settings may or may not have been applied; check the value with a GET before patching again";

            // bmcweb's own 60 s timer expired on a call that may still be in
            // flight, so this is the one outcome that is neither applied nor
            // definitely not applied.
            BMCWEB_LOG_ERROR(
                "PCIeLinkEnableMask {} timed out on {}; the write may or may not have landed",
                maskStr, path);
            messages::asyncError(resp->res, errTimeout, errTimeoutResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueInvalidArgument)
        {
            // Backstop for the SupportedMask pre-check in
            // changePCIeLinkEnableMask.
            BMCWEB_LOG_ERROR(
                "PCIeLinkEnableMask {} not applied: {} rejected the value",
                maskStr, path);
            messages::propertyValueIncorrect(
                resp->res, "PCIeLinkEnableMask/Mask", maskStr);
        }
        else
        {
            BMCWEB_LOG_ERROR(
                "PCIeLinkEnableMask {} not applied: {} returned unmapped async status '{}'",
                maskStr, path, status);
            messages::internalError(resp->res);
        }
    }

  private:
    std::shared_ptr<bmcweb::AsyncResp> resp;
    std::string maskStr;
    std::string path;
};

inline void setPCIeLinkEnableMask(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path, uint64_t enableMask,
    const std::string& maskStr)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [asyncResp, path, enableMask, maskStr,
         service](const boost::system::error_code& ec,
                  const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call");
                    nvidia_async_operation_utils::
                        doGenericSetAsyncAndGatherResult(
                            asyncResp, std::chrono::seconds(60), service, path,
                            pcieLinkEnableMaskInterface, "EnableMask",
                            dbus::utility::DbusVariantType(enableMask),
                            PatchPCIeLinkEnableMaskCallback{asyncResp, maskStr,
                                                            path});

                    return;
                }
            }

            BMCWEB_LOG_DEBUG("Performing Patch using set-property Call");

            dbus::utility::async_method_call(
                [asyncResp, maskStr, path](const boost::system::error_code& ec2,
                                           sdbusplus::message::message& msg) {
                    if (!ec2)
                    {
                        BMCWEB_LOG_INFO("PCIeLinkEnableMask {} accepted for {}",
                                        maskStr, path);
                        messages::success(asyncResp->res);
                        return;
                    }
                    // Read and convert dbus error message to redfish error
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PCIeLinkEnableMask {} not applied: Set on {} failed ({}) with no D-Bus error name",
                            maskStr, path, ec2.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                        0)
                    {
                        // Invalid value (unsupported or reserved bit set)
                        BMCWEB_LOG_ERROR(
                            "PCIeLinkEnableMask {} not applied: {} rejected the value as InvalidArgument",
                            maskStr, path);
                        messages::propertyValueIncorrect(
                            asyncResp->res, "PCIeLinkEnableMask/Mask", maskStr);
                    }
                    else if (
                        strcmp(dbusError->name,
                               "xyz.openbmc_project.Common.Error.NotAllowed") ==
                        0)
                    {
                        // The window is shut: the effecter is still
                        // initializing, or it has closed for this boot. The
                        // value was fine, so the resolution tells the operator
                        // how to find the next window instead of blaming it.
                        BMCWEB_LOG_WARNING(
                            "PCIeLinkEnableMask {} not applied: {} is outside its write window (NotAllowed)",
                            maskStr, path);
                        messages::serviceTemporarilyUnavailableMsg(
                            asyncResp->res, pcieLinkEnableMaskRetryAfter,
                            pcieLinkEnableMaskWindowClosedResolution);
                    }
                    else if (
                        strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                        0)
                    {
                        // Same 503, different cause: the terminus did not
                        // answer, so the window state is unknown rather than
                        // known-shut.
                        BMCWEB_LOG_WARNING(
                            "PCIeLinkEnableMask {} not applied: {} is unavailable (terminus unreachable)",
                            maskStr, path);
                        messages::serviceTemporarilyUnavailableMsg(
                            asyncResp->res, pcieLinkEnableMaskRetryAfter,
                            pcieLinkEnableMaskUnavailableResolution);
                    }
                    else if (strcmp(dbusError->name,
                                    "xyz.openbmc_project.Common."
                                    "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        BMCWEB_LOG_ERROR(
                            "PCIeLinkEnableMask {} not applied: {} reported a device write failure",
                            maskStr, path);
                        messages::operationFailed(asyncResp->res);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "PCIeLinkEnableMask {} not applied: {} returned unmapped D-Bus error {}",
                            maskStr, path, dbusError->name);
                        messages::internalError(asyncResp->res);
                    }
                },
                service, path, "org.freedesktop.DBus.Properties", "Set",
                pcieLinkEnableMaskInterface, "EnableMask",
                dbus::utility::DbusVariantType(enableMask));
        });
}

inline void changePCIeLinkEnableMask(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& effecterPath, uint64_t enableMask,
    const std::string& maskStr)
{
    dbus::utility::getDbusObject(
        effecterPath,
        std::array<std::string_view, 1>{pcieLinkEnableMaskInterface},
        [asyncResp, effecterPath, enableMask,
         maskStr](const boost::system::error_code& ec,
                  const dbus::utility::MapperGetObject& objInfo) {
            if (ec || objInfo.empty())
            {
                BMCWEB_LOG_ERROR(
                    "PCIeLinkEnableMask {} not applied: GetObject for {} on {} failed ({}, {} owner(s))",
                    maskStr, pcieLinkEnableMaskInterface, effecterPath,
                    ec.message(), objInfo.size());
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string& service = objInfo.begin()->first;
            // GET advertises SupportedMask; writes that set bits outside the
            // device-reported mask are rejected rather than silently masked.
            dbus::utility::getProperty<uint64_t>(
                service, effecterPath, pcieLinkEnableMaskInterface,
                "SupportedMask",
                [asyncResp, effecterPath, enableMask, maskStr,
                 service](const boost::system::error_code& ecSupported,
                          uint64_t supportedMask) {
                    if (ecSupported)
                    {
                        BMCWEB_LOG_ERROR(
                            "PCIeLinkEnableMask {} not applied: Get SupportedMask on {} from {} failed ({})",
                            maskStr, effecterPath, service,
                            ecSupported.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if ((enableMask & ~supportedMask) != 0)
                    {
                        BMCWEB_LOG_ERROR(
                            "PCIeLinkEnableMask {} not applied: sets bits outside {} SupportedMask {:#x}",
                            maskStr, effecterPath, supportedMask);
                        messages::propertyValueOutOfRange(
                            asyncResp->res, maskStr, "PCIeLinkEnableMask/Mask");
                        return;
                    }
                    setPCIeLinkEnableMask(asyncResp, service, effecterPath,
                                          enableMask, maskStr);
                });
        });
}

inline void patchPCIeLinkEnableMask(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& objPath,
    uint64_t enableMask, const std::string& maskStr)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/pcie_link_controls",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, processorId, enableMask,
         maskStr](const boost::system::error_code& ec,
                  const std::vector<std::string>& resp) {
            if (ec || resp.empty())
            {
                // Platform does not publish the link mask effecter
                BMCWEB_LOG_ERROR(
                    "Processor {} has no pcie_link_controls association",
                    processorId);
                messages::propertyNotWritable(asyncResp->res,
                                              "PCIeLinkEnableMask");
                return;
            }
            // One effecter per CPU package, so the first endpoint is the
            // effecter. Writing only it keeps a single completion path into
            // asyncResp - looping would let a late success overwrite an
            // earlier rejection.
            changePCIeLinkEnableMask(asyncResp, resp.front(), enableMask,
                                     maskStr);
        });
}

// Function to handle the getEgmModePendingData async method call response
static void getEgmModePendingDataHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const boost::system::error_code& ec,
    const OperatingConfigProperties& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error");
        messages::internalError(aResp->res);
        return;
    }

    nlohmann::json& json = aResp->res.jsonValue;
    json["Oem"]["Nvidia"]["@odata.type"] = "#NvidiaProcessor.v1_7_0.NvidiaGPU";
    for (const auto& property : properties)
    {
        if (property.first == "PendingEGMModeState")
        {
            const bool* pendingEgmState = std::get_if<bool>(&property.second);
            if (pendingEgmState == nullptr)
            {
                BMCWEB_LOG_ERROR("Get PendingEGMModeState property failed");
                messages::internalError(aResp->res);
                return;
            }
            json["Oem"]["Nvidia"]["EGMModeEnabled"] = *pendingEgmState;
        }
    }
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */

inline void getEgmModePendingData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get pending egmMode path:{}, id:{}", objPath, cpuId);

    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.EgmMode",
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            getEgmModePendingDataHandler(aResp, ec, properties);
        });
}

// Function to handle the getEgmModeData async method call response
inline void getEgmModeDataHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const boost::system::error_code& ec,
    const OperatingConfigProperties& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error");
        messages::internalError(aResp->res);
        return;
    }
    nlohmann::json& json = aResp->res.jsonValue;
    for (const auto& property : properties)
    {
        json["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaProcessor.v1_7_0.NvidiaGPU";
        if (property.first == "EGMModeEnabled")
        {
            const bool* egmModeEnabled = std::get_if<bool>(&property.second);
            if (egmModeEnabled == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            json["Oem"]["Nvidia"]["EGMModeEnabled"] = *egmModeEnabled;
        }
    }
}

/**
 * @brief Fill out processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */

inline void getEgmModeData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& cpuId, const std::string& service,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get egmMode path:{}, id:{}", objPath, cpuId);

    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.EgmMode",
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            getEgmModeDataHandler(aResp, ec, properties);
        });
}

// Function to handle the getAdaptiveTGPModeData async method call response
inline void getAdaptiveTGPModeDataHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const boost::system::error_code& ec,
    const OperatingConfigProperties& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error");
        messages::internalError(aResp->res);
        return;
    }
    nlohmann::json& json = aResp->res.jsonValue;
    for (const auto& property : properties)
    {
        json["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaProcessor.v1_7_0.NvidiaGPU";
        if (property.first == "AdaptiveTGPModeEnabled")
        {
            const bool* adaptiveTGPModeEnabled =
                std::get_if<bool>(&property.second);
            if (adaptiveTGPModeEnabled == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            json["Oem"]["Nvidia"]["AdaptiveTGPMode"] = *adaptiveTGPModeEnabled;
        }
    }
}

/**
 * @brief Fill out processor AdaptiveTGPMode info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getAdaptiveTGPModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get adaptiveTGPMode path:{}, id:{}", objPath, cpuId);

    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.AdaptiveTGPMode",
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            getAdaptiveTGPModeDataHandler(aResp, ec, properties);
        });
}

/**
 * @brief Fill out the pending AdaptiveTGPMode of a processor by requesting
 * data from the given D-Bus object. Serves the Settings sub-resource, so the
 * pending D-Bus state is reported under the same "AdaptiveTGPMode" property
 * name the main resource uses for the current state.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       Processor ID.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getAdaptiveTGPModePendingData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get pending adaptiveTGPMode path:{}, id:{}", objPath,
                     cpuId);

    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.AdaptiveTGPMode",
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            json["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaProcessor.v1_7_0.NvidiaGPU";
            for (const auto& property : properties)
            {
                if (property.first == "PendingAdaptiveTGPMode")
                {
                    const bool* pendingAdaptiveTGPMode =
                        std::get_if<bool>(&property.second);
                    if (pendingAdaptiveTGPMode == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get PendingAdaptiveTGPMode property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["AdaptiveTGPMode"] =
                        *pendingAdaptiveTGPMode;
                }
            }
        });
}

/**
 * Handle the PATCH operation of the AdaptiveTGPMode property.
 * Validates input data and sets the D-Bus property directly.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       adaptiveTGPMode New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchAdaptiveTGPMode(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    [[maybe_unused]] const std::string& processorId, const bool adaptiveTGPMode,
    const std::string& cpuObjectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    const std::string* inventoryService = nullptr;

    BMCWEB_LOG_DEBUG("patchAdaptiveTGPMode path:{} mode:{}", cpuObjectPath,
                     adaptiveTGPMode);

    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, "com.nvidia.AdaptiveTGPMode") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR("AdaptiveTGPMode interface not found on {}",
                         cpuObjectPath);
        messages::internalError(resp->res);
        return;
    }

    nvidia_async_operation_utils::patch(
        resp, *inventoryService, cpuObjectPath, "com.nvidia.AdaptiveTGPMode",
        "AdaptiveTGPModeEnabled", adaptiveTGPMode);
}

} // namespace nvidia_processor_utils
} // namespace redfish
