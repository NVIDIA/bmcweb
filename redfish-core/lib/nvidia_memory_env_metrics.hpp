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
inline void requestRoutesMemoryEnvironmentMetrics(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Memory/<str>/EnvironmentMetrics")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName, const std::string& dimmId) {
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
                std::string envMetricsURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Memory/";
                envMetricsURI += dimmId;
                envMetricsURI += "/EnvironmentMetrics";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#EnvironmentMetrics.v1_2_0.EnvironmentMetrics";
                asyncResp->res.jsonValue["@odata.id"] = envMetricsURI;
                asyncResp->res.jsonValue["Id"] = "EnvironmentMetrics";
                asyncResp->res.jsonValue["Name"] =
                    dimmId + " Environment Metrics";
                redfish::nvidia_env_utils::getMemoryEnvironmentMetricsData(
                    asyncResp, dimmId);
            });
}
} // namespace redfish
