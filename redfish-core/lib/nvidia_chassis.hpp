#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "erot_chassis.hpp"
#include "error_messages.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/nvidia_chassis_util.hpp"

#include <sdbusplus/message/types.hpp>

namespace redfish
{
// Forward declarations
void handleChassisGet(App& app, const crow::Request& req,
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
        chassisId,
        [&app, &req, asyncResp, chassisId](bool isEROT, bool isCpuEROT) {
            if (isEROT)
            {
                BMCWEB_LOG_DEBUG(" EROT chassis");
                getEROTChassis(req, asyncResp, chassisId, isCpuEROT);
            }
            else
            {
                // Call standard chassis handler for non-EROT chassis
                handleChassisGet(app, req, asyncResp, chassisId);
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
            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 objectPath](const boost::system::error_code& ec2,
                             const std::variant<std::string>& state) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG("[mapper] Bad D-Bus request error: ",
                                         ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const std::string* hostState =
                        std::get_if<std::string>(&state);
                    if (*hostState ==
                        "xyz.openbmc_project.State.Host.HostState.Running")
                    {
                        crow::connections::systemBus->async_method_call(
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
                },
                "xyz.openbmc_project.State.Host", objectPath,
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.State.Host", "CurrentHostState");
        });
}

inline void afterChassisSpiInterfacesFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& /*paths*/)
{
    BMCWEB_LOG_DEBUG("afterChassisSpiInterfacesFound");
    if (ec)
    {
        // NO spi interfaces found. This is fine.
        BMCWEB_LOG_DEBUG("NO spi interfaces found. This is fine.");
        return;
    }
    BMCWEB_LOG_DEBUG("spi interfaces found");
    // Only add actions for ProcessorModule chassis
    if (!chassisId.starts_with("HGX_ProcessorModule_"))
    {
        BMCWEB_LOG_DEBUG("Not a ProcessorModule chassis");
        return;
    }
    BMCWEB_LOG_DEBUG("ProcessorModule chassis found");

    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];
    BMCWEB_LOG_DEBUG("oemActions: {}", oemActions.dump());
    BMCWEB_LOG_DEBUG("chassisId: {}", chassisId);
    oemActions["#NvidiaChassis.VariableSpiErase"]["target"] =
        boost::urls::format(
            "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.VariableSpiErase",
            chassisId);

    oemActions["#NvidiaChassis.VariableSpiRead"]["target"] =
        boost::urls::format(
            "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.VariableSpiRead",
            chassisId);
}

// Find the existing chassis handler and add SPI interface detection
inline void getChassisOemNvidiaProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    BMCWEB_LOG_DEBUG("getChassisOemNvidiaProperties");
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        // Nothing to do if the option isn't enabled
        return;
    }

    BMCWEB_LOG_DEBUG("Checking for SPI interfaces");

    // Add SPI interface detection
    std::array<std::string_view, 1> interfaces{"com.nvidia.GraceSPI"};
    std::string inventoryPath =
        "/xyz/openbmc_project/inventory/system/" + chassisId;
    BMCWEB_LOG_DEBUG("inventoryPath: {}", inventoryPath);
    dbus::utility::getSubTreePaths(
        inventoryPath, 0, interfaces,
        std::bind_front(&afterChassisSpiInterfacesFound, asyncResp, chassisId));
}

} // namespace nvidia_chassis
} // namespace redfish
