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

inline void handleProcessorDiagActionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    std::optional<nlohmann::json> processorDiagCapabilities;

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
                                   "ProcessorDiagCapabilities",
                                   processorDiagCapabilities))
    {
        return;
    }
    if (processorDiagCapabilities)
    {
        handleDiagPostReq(asyncResp, *processorDiagCapabilities);
    }
}

inline void handleSystemProcessorDiagCapabilitiesActionGet(
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
        "/Oem/Nvidia/ProcessorDiagCapabilitiesActionInfo";
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_5_0.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "DiagMode Action Info";
    asyncResp->res.jsonValue["Id"] = "DiagModeActionInfo";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;

    parameter["Name"] = "DiagMode";
    parameter["Required"] = true;
    parameter["DataType"] = "Boolean";
    nlohmann::json::array_t allowableValues;
    allowableValues.emplace_back("Enable");
    allowableValues.emplace_back("Disable");
    parameter["AllowableValues"] = std::move(allowableValues);
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void handleProcessorDiagSysConfigActionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    std::optional<nlohmann::json> processorDiagSysConfig;

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
    if (processorDiagSysConfig)
    {
        handleDiagSysConfigPostReq(asyncResp, *processorDiagSysConfig);
    }
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
            .append("/Oem/Nvidia/ProcessorDiagSysConfigActionInfo");
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_5_0.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "DiagSysConfig Action Info";
    asyncResp->res.jsonValue["Id"] = "DiagSysConfigActionInfo";

    nlohmann::json::array_t parameters;

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "ConfigType";
        parameter["Required"] = true;
        parameter["DataType"] = "Number";
        nlohmann::json::array_t allowableNumbers;
        allowableNumbers.emplace_back("0:1:1");
        parameter["AllowableNumbers"] = std::move(allowableNumbers);
        parameters.emplace_back(std::move(parameter));
    }

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "TestDuration";
        parameter["Required"] = true;
        parameter["DataType"] = "Number";
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 255;
        parameters.emplace_back(std::move(parameter));
    }

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "DynamicData";
        parameter["Required"] = true;
        parameter["DataType"] = "NumberArray";
        parameter["ArraySizeMaximum"] = 199;
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 255;
        parameters.emplace_back(std::move(parameter));
    }

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void handleProcessorDiagTidConfigActionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    std::optional<nlohmann::json> processorDiagTidConfig;

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
    if (processorDiagTidConfig)
    {
        handleDiagTidConfigPostReq(asyncResp, *processorDiagTidConfig);
    }
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
            .append("/Oem/Nvidia/ProcessorDiagTidConfigActionInfo");
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_5_0.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "DiagTidConfig Action Info";
    asyncResp->res.jsonValue["Id"] = "DiagTidConfigActionInfo";

    nlohmann::json::array_t parameters;

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "Tid";
        parameter["Required"] = true;
        parameter["DataType"] = "Number";
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 255;
        parameters.emplace_back(std::move(parameter));
    }

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "TestDuration";
        parameter["Required"] = true;
        parameter["DataType"] = "Number";
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 255;
        parameters.emplace_back(std::move(parameter));
    }

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "Loops";
        parameter["Required"] = true;
        parameter["DataType"] = "Number";
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 65535;
        parameters.emplace_back(std::move(parameter));
    }

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "LogLevel";
        parameter["Required"] = true;
        parameter["DataType"] = "Number";
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 255;
        parameters.emplace_back(std::move(parameter));
    }

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "DynamicDataSize";
        parameter["Required"] = true;
        parameter["DataType"] = "Number";
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 255;
        parameters.emplace_back(std::move(parameter));
    }

    {
        nlohmann::json::object_t parameter;
        parameter["Name"] = "DynamicData";
        parameter["Required"] = true;
        parameter["DataType"] = "NumberArray";
        parameter["ArraySizeMaximum"] = 194;
        parameter["MinimumValue"] = 0;
        parameter["MaximumValue"] = 255;
        parameters.emplace_back(std::move(parameter));
    }

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void requestRoutesSystemsCPUDiag(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Oem/Nvidia/ProcessorDiagCapabilities")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleProcessorDiagActionPost, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/ProcessorDiagCapabilitiesActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSystemProcessorDiagCapabilitiesActionGet, std::ref(app)));
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Oem/Nvidia/ProcessorDiagSysConfig")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleProcessorDiagSysConfigActionPost, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/ProcessorDiagSysConfigActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSystemProcessorDiagSysConfigActionGet, std::ref(app)));
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Oem/Nvidia/ProcessorDiagTidConfig")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleProcessorDiagTidConfigActionPost, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/ProcessorDiagTidConfigActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSystemProcessorDiagTidConfigActionGet, std::ref(app)));
}
} // namespace redfish
