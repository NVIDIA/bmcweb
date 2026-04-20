#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/processor_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
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
            features["RISTDiagnostic"]))
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
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["MNNVLinkTopology"]["SystemGUID"] =
                property;
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
                sdbusplus::message::object_path(objPath).filename();
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

inline void getClearablePcieCounters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath,
    const std::string& interface)
{
    dbus::utility::getAllProperties(
        service, objPath, interface,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Get All call Failed for the interface. ec: {}", ec);
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

                    if (data)
                    {
                        for (const auto& counter : *data)
                        {
                            clearableDataSource.push_back(
                                counter.substr(counter.find_last_of('.') + 1));
                        }
                    }
                }
            }
            asyncResp->res.jsonValue["Parameters"]["AllowableValues"] =
                clearableDataSource;
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

                                    sdbusplus::message::object_path objectPath(
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
                                                asyncResp->res
                                                    .jsonValue["Parameters"]
                                                              ["Name"] =
                                                    "CounterType";
                                                asyncResp->res
                                                    .jsonValue["Parameters"]
                                                              ["Required"] =
                                                    true;
                                                asyncResp->res
                                                    .jsonValue["Parameters"]
                                                              ["DataType"] =
                                                    "String";
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
                        sdbusplus::message::object_path pPath(portPath);
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
                        sdbusplus::message::object_path pPath(portPath);
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

                            sdbusplus::message::object_path path1(sensorpath);
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

} // namespace nvidia_processor_utils
} // namespace redfish
