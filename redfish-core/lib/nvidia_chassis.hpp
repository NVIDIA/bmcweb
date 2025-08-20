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

} // namespace nvidia_chassis
} // namespace redfish
