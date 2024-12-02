#pragma once

#include "utils/nvidia_async_set_callbacks.hpp"

namespace redfish
{
namespace nvidia_processor_utils
{
using DbusProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;
using OperatingConfigProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

using InbandReconfigPermission = std::tuple<std::string, std::string, bool>;

/**
 * Parses the json of the InbandReconfigSettings properties.
 *
 * @param[in,out]   resp                Async HTTP response.
 * @param[in]       json                New json data to apply.
 * @param[in]       featureName         Name of permission feature
 * @param[in,out]   permissions         Collection of parsed permissions
 */
inline void parseInbandReconfigSettingsJson(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, nlohmann::json& json,
    const std::string& featureName,
    std::vector<InbandReconfigPermission>& permissions)
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
            permissions.emplace_back(std::make_tuple(
                featureName, "AllowOneShotConfig", *allowOneShotConfig));
        }
        if (allowPersistentConfig)
        {
            permissions.emplace_back(std::make_tuple(
                featureName, "AllowPersistentConfig", *allowPersistentConfig));
        }
        if (allowFLRPersistentConfig)
        {
            permissions.emplace_back(
                std::make_tuple(featureName, "AllowFLRPersistentConfig",
                                *allowFLRPersistentConfig));
        }
    }
}

/**
 * Parses the json of the InbandReconfigPermissions.
 *
 * @param[in,out]   resp                Async HTTP response.
 * @param[in]       json                New json data to apply.
 *
 * @return Collection of parsed Inband Reconfig Permissions requests
 */
inline std::vector<InbandReconfigPermission> parseInbandReconfigPermissionsJson(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, nlohmann::json& json)
{
    std::vector<InbandReconfigPermission> permissions;
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
            features["PowerSmoothingPrivilegeLevel2"]))
    {
        for (auto& [featureName, feature] : features)
        {
            if (feature)
            {
                parseInbandReconfigSettingsJson(aResp, *feature, featureName,
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
    auto patchRequests = parseInbandReconfigPermissionsJson(asyncResp, json);
    processor_utils::getProcessorObject(
        asyncResp, processorId,
        [patchRequests](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        [[maybe_unused]] const std::string& processorId,
                        const std::string& objectPath,
                        const MapperServiceMap& serviceMap,
                        [[maybe_unused]] const std::string& deviceType) {
            for (const auto& [service, _] : serviceMap)
            {
                for (const auto& [featureName, property, value] : patchRequests)
                {
                    nvidia_async_operation_utils::patch(
                        asyncResp, service,
                        objectPath + "/InbandReconfigPermissions/" +
                            featureName,
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
                        const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.CCMode") != interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(" CCDevMode interface not found ");
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
            crow::connections::systemBus->async_method_call(
                [resp, processorId](boost::system::error_code ec,
                                    sdbusplus::message::message& msg) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("Set CC Mode property succeeded");
                        return;
                    }
                    BMCWEB_LOG_DEBUG("CPU:{} set CC Mode  property failed: {}",
                                     processorId, ec);

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
                           const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.CCMode") != interfaceList.end())
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
            crow::connections::systemBus->async_method_call(
                [resp, processorId](boost::system::error_code ec,
                                    sdbusplus::message::message& msg) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("Set CC Dev Mode property succeeded");
                        return;
                    }

                    BMCWEB_LOG_DEBUG(
                        "CPU:{} set CC Dev Mode  property failed: {}",
                        processorId, ec);
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

/*
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getSysGUID(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                       const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get System-GUID");
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "com.nvidia.SysGUID.SysGUID", "SysGUID",
        [objPath, asyncResp{std::move(asyncResp)}](
            const boost::system::error_code& ec, const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaProcessor.v1_3_0.NvidiaGPU";
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
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](const boost::system::error_code ec,
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
                    "#NvidiaProcessor.v1_3_0.NvidiaGPU";
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.CCMode");
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
inline void getInbandReconfigPermissionsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId, objPath](const boost::system::error_code ec,
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
                "#NvidiaProcessor.v1_3_0.NvidiaGPU";
            auto& reconfigPermissionsJson =
                json["Oem"]["Nvidia"]["InbandReconfigPermissions"]
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.InbandReconfigSettings");
}

inline void getInbandReconfigPermissionsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& objPath)
{
    // Ask for all objects implementing OperatingConfig so we can search
    // for one with a matching name
    crow::connections::systemBus->async_method_call(
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
                    getInbandReconfigPermissionsData(aResp, cpuId, serviceName,
                                                     objectPath);
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", objPath, 0,
        std::array<const char*, 1>{"com.nvidia.InbandReconfigSettings"});
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
    processor_utils::getProcessorObject(
        aResp, cpuId,
        [](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
           const std::string& processorId, const std::string& path,
           [[maybe_unused]] const MapperServiceMap& serviceMap,
           [[maybe_unused]] const std::string& deviceType) {
            crow::connections::systemBus->async_method_call(
                [aResp, processorId, path](const boost::system::error_code ec,
                                           const MapperServiceMap& serviceMap) {
                    if (ec)
                    {
                        return;
                    }

                    for (const auto& [_, interfaces] : serviceMap)
                    {
                        if (std::find(
                                interfaces.begin(), interfaces.end(),
                                "com.nvidia.ErrorInjection.ErrorInjection") ==
                            interfaces.end())
                        {
                            continue;
                        }
                        aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaProcessor.v1_3_0.NvidiaGPU";
                        aResp->res
                            .jsonValue["Oem"]["Nvidia"]["ErrorInjection"] = {
                            {"@odata.id",
                             "/redfish/v1/Systems/" +
                                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                 "/Processors/" + processorId +
                                 "/Oem/Nvidia/ErrorInjection"}};
                        return;
                    }
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject",
                path + "/ErrorInjection", std::array<const char*, 0>());
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
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](const boost::system::error_code ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            json["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaProcessor.v1_3_0.NvidiaGPU";
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.CCMode");
}

/**
 * @brief Populate Property SMUtilizationPercent by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getSMUtilizationData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                 const std::string& service,
                                 const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor metrics SMUtilizationPercent data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.SMUtilization");
}

inline void getNvLinkTotalCount(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](const boost::system::error_code ec,
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.NVLink.NvLinkTotalCount");
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

inline void getClearablePcieCounters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath,
    const std::string& interface)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](
            const boost::system::error_code ec,
            const std::vector<
                std::pair<std::string, std::variant<std::vector<std::string>>>>&
                propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Get All call Failed for the interface. ec: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            std::vector<std::string> clearableDataSource;
            for (const std::pair<std::string,
                                 std::variant<std::vector<std::string>>>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "ClearableCounters")
                {
                    const std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&property.second);

                    if (data)
                    {
                        for (auto counter : *data)
                        {
                            clearableDataSource.push_back(
                                counter.substr(counter.find_last_of('.') + 1));
                        }
                    }
                }
            }
            asyncResp->res.jsonValue["Parameters"]["AllowableValues"] =
                clearableDataSource;
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        interface.c_str());
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
    crow::connections::systemBus->async_method_call(
        [processorId, portId, asyncResp](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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
                crow::connections::systemBus->async_method_call(
                    [asyncResp, processorId,
                     portId](const boost::system::error_code& e,
                             std::variant<std::vector<std::string>>& resp) {
                        if (e)
                        {
                            // no state sensors attached.
                            BMCWEB_LOG_ERROR(
                                "Object Mapper call failed while finding all_states association, with error {}",
                                e);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);
                        if (data == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "No Association for all_states found");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& sensorpath : *data)
                        {
                            // Check Interface in Object or not
                            BMCWEB_LOG_DEBUG(
                                "processor state sensor object path {}",
                                sensorpath);
                            crow::connections::systemBus->async_method_call(
                                [asyncResp, sensorpath, processorId, portId](
                                    const boost::system::error_code ec,
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        object) {
                                    if (ec)
                                    {
                                        // the path does not implement port
                                        // interfaces
                                        BMCWEB_LOG_DEBUG(
                                            "no port interface on object path {}",
                                            sensorpath);
                                        return;
                                    }

                                    sdbusplus::message::object_path path(
                                        sensorpath);
                                    if (path.filename() != portId)
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
                                         object)
                                    {
                                        for (auto interface : interfaces)
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
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper", "GetObject",
                                sensorpath,
                                std::array<std::string, 1>(
                                    {"xyz.openbmc_project.Inventory.Item.Port"}));
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper", path + "/all_states",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Accelerator"});
}

inline void getPortLinkStatusSetting(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& portPath, const std::string& service,
    const std::vector<uint8_t>& portsToDisable)
{
    using PropertyType =
        std::variant<std::string, bool, size_t, std::vector<uint8_t>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;

    crow::connections::systemBus->async_method_call(
        [aResp, portsToDisable](const boost::system::error_code ec,
                                const PropertiesMap& properties) {
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
                    auto it = std::find(portsToDisable.begin(),
                                        portsToDisable.end(), *value);
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
        },
        service, portPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.Port");
}

inline void getPortDisableFutureStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap,
    const std::string& portId)
{
    using PropertyType =
        std::variant<std::string, bool, size_t, std::vector<uint8_t>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;

    crow::connections::systemBus->async_method_call(
        [aResp, processorId, portId,
         objectPath](const boost::system::error_code ec,
                     const PropertiesMap& properties) {
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
                    auto* value =
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

            crow::connections::systemBus->async_method_call(
                [aResp, processorId, portId,
                 portsToDisable](const boost::system::error_code ec,
                                 std::variant<std::vector<std::string>>& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error");
                        messages::internalError(aResp->res);
                        return;
                    }
                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting ports");
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const std::string& portPath : *data)
                    {
                        sdbusplus::message::object_path pPath(portPath);
                        if (pPath.filename() != portId)
                        {
                            continue;
                        }

                        crow::connections::systemBus->async_method_call(
                            [aResp, processorId, portId, portPath,
                             portsToDisable](
                                const boost::system::error_code ec,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    object) {
                                if (ec)
                                {
                                    BMCWEB_LOG_DEBUG("No port interface on {}",
                                                     portPath);
                                    return;
                                }
                                getPortLinkStatusSetting(aResp, portPath,
                                                         object.front().first,
                                                         portsToDisable);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetObject",
                            portPath,
                            std::array<std::string, 1>(
                                {"xyz.openbmc_project.Inventory.Item.Port"}));
                    }
                },
                "xyz.openbmc_project.ObjectMapper", objectPath + "/all_states",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        serviceMap.front().first, objectPath, "org.freedesktop.DBus.Properties",
        "GetAll", "com.nvidia.NVLink.NVLinkDisableFuture");
}

inline void getPortNumberAndCallSetAsync(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, const std::string& portId,
    const std::string& propertyValue, const std::string& propertyName,
    const std::string& processorPath, const std::string& processorService,
    const std::string& portService, const std::string& portPath,
    const std::vector<uint8_t>& portsToDisable)
{
    using PropertyType =
        std::variant<std::string, bool, size_t, std::vector<uint8_t>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;

    crow::connections::systemBus->async_method_call(
        [aResp, processorId, portId, propertyValue, propertyName, processorPath,
         processorService, portsToDisable](const boost::system::error_code ec,
                                           const PropertiesMap& properties) {
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
                            const boost::system::error_code& ec,
                            const dbus::utility::MapperGetObject& object) {
                            if (!ec)
                            {
                                for (const auto& [serv, _] : object)
                                {
                                    if (serv != processorService)
                                    {
                                        continue;
                                    }

                                    std::vector<uint8_t> portListToDisable =
                                        portsToDisable;
                                    auto it = std::find(
                                        portListToDisable.begin(),
                                        portListToDisable.end(), portNumber);
                                    if (propertyValue == "Disabled")
                                    {
                                        if (it == portListToDisable.end())
                                            portListToDisable.push_back(
                                                static_cast<uint8_t>(
                                                    portNumber));
                                    }
                                    else if (propertyValue == "Enabled")
                                    {
                                        if (it != portListToDisable.end())
                                            portListToDisable.erase(it);
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
        },
        portService, portPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.Port");
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
        if (std::find(interfaceList.begin(), interfaceList.end(),
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
        messages::internalError(aResp->res);
        return;
    }

    using PropertyType =
        std::variant<std::string, bool, size_t, std::vector<uint8_t>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;

    crow::connections::systemBus->async_method_call(
        [aResp, processorId, portId, propertyValue, propertyName, objectPath,
         service = *inventoryService](const boost::system::error_code ec,
                                      const PropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            std::vector<uint8_t> portsToDisable;

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "PortDisableFuture")
                {
                    auto* value =
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
            crow::connections::systemBus->async_method_call(
                [aResp, processorId, portId, propertyValue, propertyName,
                 objectPath, service,
                 portsToDisable](const boost::system::error_code ec,
                                 std::variant<std::vector<std::string>>& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error");
                        messages::internalError(aResp->res);
                        return;
                    }
                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting ports");
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const std::string& portPath : *data)
                    {
                        // Get the portId object
                        sdbusplus::message::object_path pPath(portPath);
                        if (pPath.filename() != portId)
                        {
                            continue;
                        }

                        crow::connections::systemBus->async_method_call(
                            [aResp, processorId, portId, portPath,
                             propertyValue, propertyName, objectPath, service,
                             portsToDisable](
                                const boost::system::error_code ec,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    object) {
                                if (ec)
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
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetObject",
                            portPath,
                            std::array<std::string, 1>(
                                {"xyz.openbmc_project.Inventory.Item.Port"}));
                    }
                },
                "xyz.openbmc_project.ObjectMapper", objectPath + "/all_states",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        serviceMap.front().first, objectPath, "org.freedesktop.DBus.Properties",
        "GetAll", "com.nvidia.NVLink.NVLinkDisableFuture");
}
inline void
    getWorkLoadPowerInfo(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
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

inline void
    clearPCIeCounter(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
    crow::connections::systemBus->async_method_call(
        [processorId, portId, asyncResp, counterType](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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
                crow::connections::systemBus->async_method_call(
                    [asyncResp, processorId, portId, counterType](
                        const boost::system::error_code& e,
                        std::variant<std::vector<std::string>>& resp) {
                        if (e)
                        {
                            // no state sensors attached.
                            BMCWEB_LOG_ERROR(
                                "Object Mapper call failed while finding all_states association, with error {}",
                                e);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);
                        if (data == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "No Association for all_states found");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& sensorpath : *data)
                        {
                            // Check Interface in Object or not
                            BMCWEB_LOG_DEBUG(
                                "processor state sensor object path {}",
                                sensorpath);

                            sdbusplus::message::object_path path1(sensorpath);
                            if (path1.filename() != portId)
                            {
                                return;
                            }

                            crow::connections::systemBus->async_method_call(
                                [asyncResp, sensorpath, portId, counterType](
                                    const boost::system::error_code ec,
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        object) {
                                    if (ec)
                                    {
                                        // the path does not implement port
                                        // interfaces
                                        BMCWEB_LOG_DEBUG(
                                            "no port interface on object path {}",
                                            sensorpath);
                                        return;
                                    }

                                    for (auto [connection, interfaces] : object)
                                    {
                                        clearPCIeCounter(asyncResp, connection,
                                                         sensorpath,
                                                         counterType);
                                    }
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper", "GetObject",
                                sensorpath,
                                std::array<std::string, 2>(
                                    {"xyz.openbmc_project.Inventory.Item.Port",
                                     "xyz.openbmc_project.PCIe.ClearPCIeCounters"}));
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper", path + "/all_states",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Accelerator"});
}

inline void setOperatingSpeedRange(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const uint32_t& value,
    const std::string& patchProp, const std::string& path)
{
    crow::connections::systemBus->async_method_call(
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

            for (auto& [service, interfaces] : objInfo)
            {
                if (std::find(
                        interfaces.begin(), interfaces.end(),
                        nvidia_async_operation_utils::setAsyncInterfaceName) ==
                    interfaces.end())
                {
                    continue;
                }

                if (patchProp == "SettingMin")

                {
                    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                        asyncResp, std::chrono::seconds(60), service, path,
                        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                        "RequestedSpeedLimitMin", std::variant<uint32_t>(value),
                        nvidia_async_operation_utils::
                            PatchClockLimitControlCallback{asyncResp});
                }
                else if (patchProp == "SettingMax")

                {
                    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                        asyncResp, std::chrono::seconds(60), service, path,
                        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                        "RequestedSpeedLimitMax", std::variant<uint32_t>(value),
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
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", path,
        std::array<const char*, 1>(
            {"xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig"}));
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
    const std::string& processorId, const uint32_t& value,
    const std::string& patchProp, const std::string processorObjPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, value, patchProp, processorId,
         processorObjPath](const boost::system::error_code ec,
                           std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("ObjectMapper call failed with error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            const std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);

            if (data)
            {
                for (auto chassisPath : *data)
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp, value, patchProp, processorId, chassisPath](
                            const boost::system::error_code ec,
                            std::variant<std::vector<std::string>>& resp) {
                            if (ec)
                            {
                                return; // no clock Limit Path for the chassis
                                        // path
                            }

                            std::vector<std::string>* data =
                                std::get_if<std::vector<std::string>>(&resp);

                            for (auto clockLimitPath : *data)
                            {
                                setOperatingSpeedRange(asyncResp, value,
                                                       patchProp,
                                                       clockLimitPath);
                            }
                        },

                        "xyz.openbmc_project.ObjectMapper",
                        chassisPath + "/clock_controls",
                        "org.freedesktop.DBus.Properties", "Get",
                        "xyz.openbmc_project.Association", "endpoints");
                }
            }
            else
            {
                BMCWEB_LOG_ERROR("Chassis Path not found for processorId {}",
                                 processorId);
                messages::internalError(asyncResp->res);
                return;
            }
        },

        "xyz.openbmc_project.ObjectMapper",
        processorObjPath + "/parent_chassis", "org.freedesktop.DBus.Properties",
        "Get", "xyz.openbmc_project.Association", "endpoints");
}

inline void getOperatingSpeedRangeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path)
{
    crow::connections::systemBus->async_method_call(
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
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, path, interface](
                                const boost::system::error_code errorno,
                                const std::vector<std::pair<
                                    std::string,
                                    std::variant<uint32_t, std::string>>>&
                                    propertiesList) {
                                if (errorno)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "ObjectMapper::GetObject call failed:{}",
                                        errorno);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const std::pair<
                                         std::string,
                                         std::variant<uint32_t, std::string>>&
                                         property : propertiesList)
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
                                    else if (propertyName == "MinSpeed")
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
                                    else if (propertyName ==
                                             "RequestedSpeedLimitMax")
                                    {
                                        propertyName = "SettingMax";
                                        const uint32_t* value =
                                            std::get_if<uint32_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for SettingMax");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res
                                            .jsonValue["OperatingSpeedRangeMHz"]
                                                      [propertyName] = *value;
                                        continue;
                                    }
                                    else if (propertyName ==
                                             "RequestedSpeedLimitMin")
                                    {
                                        propertyName = "SettingMin";
                                        const uint32_t* value =
                                            std::get_if<uint32_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for SettingMin");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res
                                            .jsonValue["OperatingSpeedRangeMHz"]
                                                      [propertyName] = *value;
                                        continue;
                                    }
                                }
                            },
                            element.first, path,
                            "org.freedesktop.DBus.Properties", "GetAll",
                            interface);
                    }
                }
            }
        },

        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", path,
        std::array<const char*, 0>());
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);

            for (auto chassisPath : *data)
            {
                crow::connections::systemBus->async_method_call(
                    [aResp, chassisPath](
                        const boost::system::error_code ec,
                        std::variant<std::vector<std::string>>& resp) {
                        if (ec)
                        {
                            return; // no chassis = no failures
                        }
                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);

                        for (auto clockControlPath : *data)
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
                    },

                    "xyz.openbmc_project.ObjectMapper",
                    chassisPath + "/clock_controls",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },

        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

} // namespace nvidia_processor_utils
} // namespace redfish
