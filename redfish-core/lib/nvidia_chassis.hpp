#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "erot_chassis.hpp"
#include "error_messages.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_chassis_util.hpp"

#include <boost/beast/http/verb.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/types.hpp>

namespace redfish
{
// Forward declarations
void handleChassisGetNoRequestParameter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId);
void handleChassisPatch(App& app, const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisId);
void doChassisPowerCycle(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp);

namespace nvidia_chassis
{
/**
 * @brief Handle chassis GET request with EROT chassis support
 *
 * This function checks for EROT chassis and routes accordingly.
 * DMTF code: Standard chassis handling
 * OEM Code: EROT chassis detection and routing
 */
inline void handleChassisGetPreCheck(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_chassis_utils::isEROTChassis(
        chassisId, [asyncResp, chassisId](bool isEROT, bool isCpuEROT) {
            if (isEROT)
            {
                BMCWEB_LOG_DEBUG(" EROT chassis");
                getEROTChassis(asyncResp, chassisId, isCpuEROT);
            }
            else
            {
                // Call standard chassis handler for non-EROT chassis
                handleChassisGetNoRequestParameter(asyncResp, chassisId);
            }
        });
}

/**
 * @brief Handle chassis PATCH request with EROT chassis support
 *
 * This function checks for EROT chassis and routes accordingly.
 * DMTF code: Standard chassis handling
 * OEM Code: EROT chassis detection and routing
 */
inline void handleChassisPatchReq(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_chassis_utils::isEROTChassis(
        chassisId,
        [&app, &req, asyncResp, chassisId](bool isEROT, bool isCpuEROT) {
            if (isEROT)
            {
                BMCWEB_LOG_DEBUG(" EROT chassis");
                handleEROTChassisPatch(req, asyncResp, chassisId, isCpuEROT);
            }
            else
            {
                // Call standard chassis handler for non-EROT chassis
                handleChassisPatch(app, req, asyncResp, chassisId);
            }
        });
}

/**
 * @brief Power cycle the chassis
 *
 * This function handles the power cycle operation for chassis reset.
 */
inline void powerCycle(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getSubTreePaths(
        "/", 0,
        std::array<std::string_view, 1>{"xyz.openbmc_project.State.Host"},
        [asyncResp](const boost::system::error_code& ec,
                    const std::vector<std::string>& hostList) {
            if (ec)
            {
                doChassisPowerCycle(asyncResp);
            }
            std::string objectPath = "/xyz/openbmc_project/state/host_system0";
            if ((std::find(hostList.begin(), hostList.end(), objectPath)) ==
                hostList.end())
            {
                objectPath = "/xyz/openbmc_project/state/host0";
            }
            dbus::utility::getProperty<std::string>(
                "xyz.openbmc_project.State.Host", objectPath,
                "xyz.openbmc_project.State.Host", "CurrentHostState",
                [asyncResp, objectPath](const boost::system::error_code& ec2,
                                        const std::string& hostState) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG("[mapper] Bad D-Bus request error: ",
                                         ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (hostState ==
                        "xyz.openbmc_project.State.Host.HostState.Running")
                    {
                        dbus::utility::async_method_call(
                            [asyncResp,
                             objectPath](const boost::system::error_code& ec3) {
                                // Use "Set" method to set the property
                                // value.
                                if (ec3)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "[Set] Bad D-Bus request error: ", ec3);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                messages::success(asyncResp->res);
                            },
                            "xyz.openbmc_project.State.Host", objectPath,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.State.Host",
                            "RequestedHostTransition",
                            dbus::utility::DbusVariantType{
                                "xyz.openbmc_project.State.Host.Transition.Reboot"});
                    }
                    else
                    {
                        doChassisPowerCycle(asyncResp);
                    }
                });
        });
}

inline void afterChassisSpiInterfacesFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& paths)
{
    BMCWEB_LOG_DEBUG("afterChassisSpiInterfacesFound");
    if (ec)
    {
        // NO spi interfaces found. This is fine.
        BMCWEB_LOG_DEBUG("NO spi interfaces found. This is fine.");
        return;
    }

    if (paths.empty())
    {
        BMCWEB_LOG_ERROR(
            "No SPI interface object paths found for chassisId: {}", chassisId);
        return;
    }

    int objPathCount = 0;
    for (const auto& path : paths)
    {
        if (path.find(chassisId) != std::string::npos)
        {
            objPathCount++;
        }
    }
    if (objPathCount != 1)
    {
        BMCWEB_LOG_ERROR(
            "Multiple SPI interface object paths {} found for chassisId: {}",
            objPathCount, chassisId);
        return;
    }

    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];
    oemActions["#NvidiaChassis.VariableSpiErase"]["target"] =
        boost::urls::format(
            "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.VariableSpiErase",
            chassisId);

    oemActions["#NvidiaChassis.VariableSpiRead"]["target"] =
        boost::urls::format(
            "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.VariableSpiRead",
            chassisId);
}

inline void afterChassisSetRecoveryModeInterfacesFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& paths)
{
    if (ec)
    {
        return;
    }

    if (paths.empty())
    {
        return;
    }

    int objPathCount = 0;
    for (const auto& path : paths)
    {
        if (std::filesystem::path(path).filename() == chassisId)
        {
            objPathCount++;
        }
    }
    if (objPathCount != 1)
    {
        BMCWEB_LOG_ERROR(
            "Multiple SetRecoveryMode interface object paths {} found for chassisId: {}",
            objPathCount, chassisId);
        return;
    }

    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];
    oemActions["#NvidiaChassis.SetRecoveryMode"]["target"] =
        boost::urls::format(
            "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.SetRecoveryMode",
            chassisId);
}

/**
 * @brief Get chassis OEM NVIDIA properties and actions
 *
 * This function detects and adds NVIDIA-specific chassis OEM actions
 */
inline void getChassisOemNvidiaProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        // Nothing to do if the option isn't enabled
        return;
    }

    // Detect and add SPI interface actions
    std::array<std::string_view, 1> spiInterfaces{"com.nvidia.GraceSPI"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, spiInterfaces,
        std::bind_front(afterChassisSpiInterfacesFound, asyncResp, chassisId));

    // Detect and add SetRecoveryMode action for non-platform chassis.
    // Platform chassis uses SetCPURecoveryMode instead.
    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        std::array<std::string_view, 1> recoveryInterfaces{
            "com.nvidia.SetRecoveryMode"};
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/inventory", 0, recoveryInterfaces,
            std::bind_front(afterChassisSetRecoveryModeInterfacesFound,
                            asyncResp, chassisId));
    }
}

/**
 * @brief Add SetCPURecoveryMode OEM action to chassis response if supported
 *
 * Checks if the chassis supports CPU recovery mode by looking for the
 * com.nvidia.SetRecoveryMode D-Bus interface and adds the OEM action
 * to the chassis response.
 *
 * @param asyncResp Async response object
 * @param chassisId Target chassis identifier
 */
inline void addCpuRecoveryModeAction(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        return;
    }

    constexpr std::array<std::string_view, 1> recoveryInterfaces = {
        "com.nvidia.SetRecoveryMode"};
    const std::string chassisObjPath =
        "/xyz/openbmc_project/inventory/system/chassis/" + chassisId;

    dbus::utility::getDbusObject(
        chassisObjPath, recoveryInterfaces,
        [asyncResp,
         chassisId](const boost::system::error_code& ecRecovery,
                    const dbus::utility::MapperGetObject& mapperResponse) {
            if (ecRecovery || mapperResponse.empty())
            {
                return;
            }

            asyncResp->res.jsonValue["Actions"]["Oem"]
                                    ["#NvidiaChassis.SetCPURecoveryMode"]
                                    ["target"] = boost::urls::format(
                "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.SetCPURecoveryMode",
                chassisId);
        });
}

/**
 * @brief Callback for SetRecoveryMode D-Bus method call
 *
 * @param asyncResp Async response object
 * @param ec Error code from D-Bus call
 */
inline void handleSetRecoveryModeResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("SetCPURecoveryMode D-Bus call failed: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    messages::success(asyncResp->res);
}

/**
 * @brief Invoke SetRecoveryMode on the discovered service
 *
 * @param asyncResp Async response object
 * @param service D-Bus service name
 * @param chassisObjPath Chassis object path
 */
inline void invokeSetRecoveryMode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& chassisObjPath)
{
    constexpr std::string_view recoveryInterface = "com.nvidia.SetRecoveryMode";
    constexpr std::string_view recoveryMethod = "SetRecoveryMode";

    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp](const boost::system::error_code& ec) {
            handleSetRecoveryModeResponse(asyncResp, ec);
        },
        service, chassisObjPath, std::string(recoveryInterface),
        std::string(recoveryMethod));
}

} // namespace nvidia_chassis

/**
 * @brief POST handler for CPU recovery mode action
 *
 * Forces all CPUs within the chassis into USB RCM recovery mode.
 * This is a chassis-level action that triggers recovery for all CPUs
 * at once via GPIO sequence.
 *
 * Uses D-Bus interface: com.nvidia.SetRecoveryMode
 * Method: SetRecoveryMode() - void method, throws exception on failure
 *
 * Exit from recovery mode is done via power cycle.
 *
 * @param app       Crow application
 * @param req       HTTP request
 * @param asyncResp Async response object
 * @param chassisId Target chassis identifier
 */
inline void handleChassisSetCPURecoveryModePost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    // SetCPURecoveryMode accepts only empty body or empty JSON object {}; fail
    // if any parameters or other JSON is given.
    if (!json_util::requireEmptyOrEmptyJsonObject(
            asyncResp->res, req.body(), "NvidiaChassis.SetCPURecoveryMode"))
    {
        return;
    }

    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    BMCWEB_LOG_DEBUG("SetCPURecoveryMode POST request for: {}", chassisId);

    constexpr std::string_view recoveryInterface = "com.nvidia.SetRecoveryMode";

    const std::string chassisObjPath =
        "/xyz/openbmc_project/inventory/system/chassis/" + chassisId;

    dbus::utility::getDbusObject(
        chassisObjPath, std::array<std::string_view, 1>{recoveryInterface},
        [asyncResp,
         chassisObjPath](const boost::system::error_code& ec,
                         const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetDbusObject failed for SetRecoveryMode: {}",
                                 ec);
                messages::internalError(asyncResp->res);
                return;
            }

            if (object.empty())
            {
                BMCWEB_LOG_ERROR(
                    "SetRecoveryMode interface not found on chassis");
                messages::internalError(asyncResp->res);
                return;
            }
            if (object.size() != 1)
            {
                BMCWEB_LOG_ERROR("SetRecoveryMode mapper response size {}",
                                 object.size());
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string& service = object.begin()->first;
            nvidia_chassis::invokeSetRecoveryMode(asyncResp, service,
                                                  chassisObjPath);
        });
}

/**
 * @brief Registers Redfish route for CPU recovery mode action
 *
 * Route: POST
 * /redfish/v1/Chassis/{ChassisId}/Actions/Oem/NvidiaChassis.SetCPURecoveryMode/
 *
 * Enables forcing all CPUs within a chassis into recovery mode.
 * Only available for chassis with CPU recovery support configured
 * in Entity Manager (USBRCMRecovery type with ChassisName).
 */
inline void requestRoutesChassisSetCPURecoveryMode(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.SetCPURecoveryMode/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleChassisSetCPURecoveryModePost, std::ref(app)));
}

} // namespace redfish
