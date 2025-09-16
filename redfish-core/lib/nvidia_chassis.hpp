#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "erot_chassis.hpp"
#include "utils/chassis_utils.hpp"

namespace redfish
{
// Forward declarations
void handleChassisGet(App& app, const crow::Request& req,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& chassisId);
void handleChassisPatch(App& app, const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisId);

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
    redfish::chassis_utils::isEROTChassis(
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
    redfish::chassis_utils::isEROTChassis(
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

} // namespace nvidia_chassis
} // namespace redfish
