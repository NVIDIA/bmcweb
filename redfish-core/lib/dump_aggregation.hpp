#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "error_messages.hpp"
#include "redfish_aggregator.hpp"

namespace redfish
{
namespace dump_aggregation
{

inline void handleSetUpRedfishRoute(
    crow::App& app, [[maybe_unused]] const std::string& dumpType,
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& managerId,
    [[maybe_unused]] const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_DEBUG("Aggregation is enabled. Forwarding URI");
        return;
    }

    // Do nothing for the Host BMC because it will not invoke this function
    return;
}

inline void requestRoutes(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/" +
                     std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX) +
                     "<str>/LogServices/Dump/Entries/<str>/attachment/")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSetUpRedfishRoute, std::ref(app), "BMC"));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/" +
                     std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX) +
                     "<str>/LogServices/Dump/Entries/<str>/attachment/")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSetUpRedfishRoute, std::ref(app), "System"));

    if constexpr (BMCWEB_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG)
    {
        BMCWEB_ROUTE(
            app, "/redfish/v1/Systems/" +
                     std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX) +
                     "<str>/LogServices/FaultLog/Entries/<str>/attachment/")
            .privileges({{"ConfigureComponents", "ConfigureManager"}})
            .methods(boost::beast::http::verb::get)(std::bind_front(
                handleSetUpRedfishRoute, std::ref(app), "FaultLog"));
    }
}

} // namespace dump_aggregation
} // namespace redfish 