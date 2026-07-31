#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "cpu_diag.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"

#include <boost/system/error_code.hpp>

#include <memory>
#include <string>

namespace redfish
{
inline void afterSystemSpiInterfacesFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& /*paths*/)
{
    if (ec)
    {
        // NO spi interfaces found.  This is fine.
        return;
    }
    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];

    // AuxPowerReset
    oemActions["#NvidiaSystem.VariableSpiErase"]["target"] =
        boost_swap_impl::format(
            "/redfish/v1/Systems/{}/Actions/Oem/NvidiaProcessor.VariableSpiErase",
            chassisId);

    oemActions["#NvidiaSystem.VariableSpiRead"]["target"] =
        boost_swap_impl::format(
            "/redfish/v1/Systems/{}/Actions/Oem/NvidiaProcessor.VariableSpiRead",
            chassisId);
}

inline void getSystemsOemNvidiaProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        // Nothing to do if the option isn't enabled
        return;
    }

    std::array<std::string_view, 1> interfaces{"com.nvidia.GraceSPI"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(&afterSystemSpiInterfacesFound, asyncResp, systemId));
}

// OEM-only pre-boot diagnostic action handlers. Kept in redfish::nvidia so the
// NVIDIA surface stays separated from the generic Redfish code; the generic
// route registration below delegates into this namespace.
namespace nvidia
{
inline void handleProcessorDiagActionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    // Not std::optional: ProcessorDiagState is a required action parameter
    // (Nullable="false" in the CSDL), so readJsonAction must reject a body
    // that omits it rather than silently returning 200 with no action taken.
    nlohmann::json processorDiagState;

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
    if (!json_util::readJsonAction(req, asyncResp->res, "ProcessorDiagState",
                                   processorDiagState))
    {
        return;
    }
    handleDiagPostReq(asyncResp, processorDiagState);
}

inline void handleSystemProcessorDiagStateActionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
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

    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + systemName +
        "/Oem/Nvidia/SetProcessorDiagModeActionInfo";
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_5_0.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "SetProcessorDiagMode Action Info";
    asyncResp->res.jsonValue["Id"] = "SetProcessorDiagModeActionInfo";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;

    parameter["Name"] = "ProcessorDiagState";
    parameter["Required"] = true;
    parameter["DataType"] = "Object";
    parameter["ObjectDataType"] =
        "#NvidiaComputerSystem.v1_10_0.ProcessorDiagStateRequest";
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void handleProcessorDiagSysConfigActionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    // Required action parameter; see handleProcessorDiagActionPost.
    nlohmann::json processorDiagSysConfig;

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
    if (!json_util::readJsonAction(req, asyncResp->res,
                                   "ProcessorDiagSysConfig",
                                   processorDiagSysConfig))
    {
        return;
    }
    handleDiagSysConfigPostReq(asyncResp, processorDiagSysConfig);
}

inline void handleSystemProcessorDiagSysConfigActionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
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

    asyncResp->res.jsonValue["@odata.id"] =
        std::string("/redfish/v1/Systems/")
            .append(systemName)
            .append("/Oem/Nvidia/ConfigProcessorDiagActionInfo");
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_5_0.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "ConfigProcessorDiag Action Info";
    asyncResp->res.jsonValue["Id"] = "ConfigProcessorDiagActionInfo";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;

    parameter["Name"] = "ProcessorDiagSysConfig";
    parameter["Required"] = true;
    parameter["DataType"] = "ObjectArray";
    parameter["ObjectDataType"] =
        "#NvidiaComputerSystem.v1_10_0.ProcessorDiagSysConfigEntry";
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void handleProcessorDiagTidConfigActionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    // Required action parameter; see handleProcessorDiagActionPost.
    nlohmann::json processorDiagTidConfig;

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
    if (!json_util::readJsonAction(req, asyncResp->res,
                                   "ProcessorDiagTidConfig",
                                   processorDiagTidConfig))
    {
        return;
    }
    handleDiagTidConfigPostReq(asyncResp, processorDiagTidConfig);
}

inline void handleSystemProcessorDiagTidConfigActionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
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

    asyncResp->res.jsonValue["@odata.id"] =
        std::string("/redfish/v1/Systems/")
            .append(systemName)
            .append("/Oem/Nvidia/ConfigProcessorDiagTidActionInfo");
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_5_0.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "ConfigProcessorDiagTid Action Info";
    asyncResp->res.jsonValue["Id"] = "ConfigProcessorDiagTidActionInfo";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;

    parameter["Name"] = "ProcessorDiagTidConfig";
    parameter["Required"] = true;
    parameter["DataType"] = "ObjectArray";
    parameter["ObjectDataType"] =
        "#NvidiaComputerSystem.v1_10_0.ProcessorDiagTidConfigEntry";
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}
} // namespace nvidia

inline void requestRoutesSystemsCPUDiag(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.SetProcessorDiagMode/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia::handleProcessorDiagActionPost, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SetProcessorDiagModeActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            nvidia::handleSystemProcessorDiagStateActionGet, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.ConfigProcessorDiag/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia::handleProcessorDiagSysConfigActionPost, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/ConfigProcessorDiagActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(nvidia::handleSystemProcessorDiagSysConfigActionGet,
                            std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.ConfigProcessorDiagTid/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia::handleProcessorDiagTidConfigActionPost, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/ConfigProcessorDiagTidActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(nvidia::handleSystemProcessorDiagTidConfigActionGet,
                            std::ref(app)));
}
} // namespace redfish
