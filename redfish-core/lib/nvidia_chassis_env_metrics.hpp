#pragma once
#include "app.hpp"
#include "async_resp.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "str_utility.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/environment_util.hpp>
#include <utils/json_utils.hpp>
#include <utils/processor_utils.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace redfish
{

inline void handleEnvironmentMetricsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_env_utils::handleEnvironmentMetricsPatchBody(
        req, asyncResp, chassisId);
}

inline void requestRoutesEnvironmentMetricsPatch(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::patchChassis)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleEnvironmentMetricsPatch, std::ref(app)));
}

inline void requestRoutesChassisEnvironmentMetricsClearOOBSetPoint(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/Actions/Oem"
                      "/NvidiaEnvironmentMetrics.ClearOOBSetPoint/")
        .privileges({{"ConfigureComponents"}})
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            const std::array<const char*, 1> interfaces = {
                "xyz.openbmc_project.Inventory.Item.Chassis"};

            dbus::utility::async_method_call(
                [asyncResp,
                 chassisId](const boost::system::error_code& ec,
                            const dbus::utility::GetSubTreeType& subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Iterate over all retrieved ObjectPaths.
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
                             object : subtree)
                    {
                        const std::string& path = object.first;
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            connectionNames = object.second;

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

                        const std::string& connectionName =
                            connectionNames[0].first;
                        dbus::utility::getProperty<std::vector<std::string>>(
                            "xyz.openbmc_project.ObjectMapper",
                            path + "/power_controls",
                            "xyz.openbmc_project.Association", "endpoints",
                            [asyncResp, connectionName,
                             chassisId](const boost::system::error_code& ec1,
                                        const std::vector<std::string>& resp) {
                                if (ec1)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const std::string& ctrlPath : resp)
                                {
                                    redfish::chassis_utils::resetPowerLimit(
                                        asyncResp, ctrlPath, connectionName);
                                }
                            });
                        return;
                    }
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interfaces);
        });
}
} // namespace redfish
