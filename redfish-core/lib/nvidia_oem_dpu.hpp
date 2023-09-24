#pragma once

#include "nlohmann/json.hpp"

#include <app.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/privilege_utils.hpp>

namespace redfish
{
namespace bluefield
{
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_BF3_PROPERTIES
struct PropertyInfo
{
    const std::string intf;
    const std::string prop;
    const std::unordered_map<std::string, std::string> dbusToRedfish = {};
    const std::unordered_map<std::string, std::string> redfishToDbus = {};
};

struct ObjectInfo
{
    // ObjectInfo() = delete;
    const std::string service;
    const std::string obj;
    const PropertyInfo propertyInfo;
    const bool required = false;
};
class DpuCommonProperties
{
  public:
    DpuCommonProperties(
        const std::unordered_map<std::string, ObjectInfo>& objects) :
        objects(objects)
    {}
    // function assumes input is valid
    inline std::string toRedfish(const std::string& str,
                                 const std::string& name) const
    {
        auto it = objects.find(name);

        if (it == objects.end())
            return "";

        auto it2 = it->second.propertyInfo.dbusToRedfish.find(str);
        if (it2 == it->second.propertyInfo.dbusToRedfish.end())
            return "";
        return it2->second;
    }
    inline std::string toDbus(const std::string& str,
                              const std::string& name) const
    {
        auto it = objects.find(name);

        if (it == objects.end())
            return "";

        auto it2 = it->second.propertyInfo.redfishToDbus.find(str);
        if (it2 == it->second.propertyInfo.redfishToDbus.end())
            return "";
        return it2->second;
    }

    inline bool isValueAllowed(const std::string& str,
                               const std::string& name) const
    {
        bool ret = false;
        auto it = objects.find(name);

        if (it != objects.end())
        {
            ret = it->second.propertyInfo.redfishToDbus.count(str) != 0;
        }

        return ret;
    }

    inline std::vector<std::string> getAllowableValues(const std::string& name)
    {
        std::vector<std::string> ret;
        auto it = objects.find(name);

        if (it != objects.end())
        {
            for (auto pair : it->second.propertyInfo.redfishToDbus)
            {
                ret.push_back(pair.first);
            }
        }
        return ret;
    }

    const std::unordered_map<std::string, ObjectInfo> objects;
};

class DpuGetProperties : virtual public DpuCommonProperties
{
  public:
    DpuGetProperties(
        const std::unordered_map<std::string, ObjectInfo>& objects) :
        DpuCommonProperties(objects)
    {}

    int getObject(nlohmann::json* const json,
                  const std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                  std::string name, const ObjectInfo& objectInfo) const
    {

        crow::connections::systemBus->async_method_call(
            [&, json, asyncResp,
             name](const boost::system::error_code ec,
                   const std::variant<std::string>& variant) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG << "DBUS response error for " << name;
                    return;
                }

                (*json)[name] =
                    toRedfish(*std::get_if<std::string>(&variant), name);
            },
            objectInfo.service, objectInfo.obj,
            "org.freedesktop.DBus.Properties", "Get",
            objectInfo.propertyInfo.intf, objectInfo.propertyInfo.prop);

        return 0;
    }
    int getProperty(nlohmann::json* const json,
                    const std::shared_ptr<bmcweb::AsyncResp> asyncResp)
    {
        for (auto pair : objects)
        {
            getObject(json, asyncResp, pair.first, pair.second);
        }
        return 0;
    }
};

class DpuActionSetProperties : virtual public DpuCommonProperties
{
  public:
    DpuActionSetProperties(
        const std::unordered_map<std::string, ObjectInfo>& objects,
        const std::string target) :
        DpuCommonProperties(objects),
        target(target)
    {}
    std::string getActionTarget()
    {
        return target;
    }
    void getActionInfo(nlohmann::json* const json)
    {
        nlohmann::json actionInfo;
        nlohmann::json::array_t parameters;
        for (auto& pair : objects)
        {
            auto& name = pair.first;
            auto& objectInfo = pair.second;
            nlohmann::json::object_t parameter;
            parameter["Name"] = name;
            parameter["Required"] = objectInfo.required;
            parameter["DataType"] = "String";
            parameter["AllowableValues"] = getAllowableValues(name);

            parameters.push_back(std::move(parameter));
        }

        (*json)["target"] = target;
        (*json)["Parameters"] = std::move(parameters);
    }

    void setAction(crow::App& app, const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::list<std::tuple<ObjectInfo, std::string, std::string>> updates;
        nlohmann::json jsonRequest;
        if (!json_util::processJsonFromRequest(asyncResp->res, req,
                                               jsonRequest))
        {

            return;
        }

        for (auto pair : objects)
        {
            auto& name = pair.first;
            auto& objectInfo = pair.second;
            std::optional<std::string> value;
            if (objectInfo.required && !jsonRequest.contains(name.c_str()))
            {

                BMCWEB_LOG_DEBUG << "Missing required param: " << name;
                messages::actionParameterMissing(asyncResp->res, name, target);
                return;
            }
        }
        for (auto item : jsonRequest.items())
        {
            auto name = item.key();
            auto it = objects.find(name);
            if (it == objects.end())
            {
                messages::actionParameterNotSupported(asyncResp->res, name,
                                                      target);
                return;
            }
            auto value = item.value().get_ptr<const std::string*>();
            if (!value)
            {
                messages::actionParameterValueError(asyncResp->res, name,
                                                    target);
                return;
            }
            if (!isValueAllowed(*value, name))
            {
                messages::actionParameterValueFormatError(asyncResp->res,
                                                          *value, name, target);
                return;
            }
        }

        for (auto item : jsonRequest.items())
        {
            auto name = item.key();
            auto value = item.value().get<std::string>();
            auto objectInfo = objects.find(name)->second;
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR << "Set failed " << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    messages::success(asyncResp->res);
                },
                objectInfo.service, objectInfo.obj,
                "org.freedesktop.DBus.Properties", "Set",
                objectInfo.propertyInfo.intf, objectInfo.propertyInfo.prop,
                std::variant<std::string>(toDbus(value, name)));
        }
    }

    const std::string target;
};

class DpuActionSetAndGetProp :
    public DpuActionSetProperties,
    public DpuGetProperties
{
  public:
    DpuActionSetAndGetProp(
        const std::unordered_map<std::string, ObjectInfo>& objects,
        const std::string target) :
        DpuCommonProperties(objects),
        DpuActionSetProperties(objects, target), DpuGetProperties(objects)
    {}
};

const PropertyInfo modeInfo = {
    .intf = "xyz.openbmc_project.Control.NicAttribute",
    .prop = "NicAttribute",
    .dbusToRedfish =
        {{"xyz.openbmc_project.Control.NicAttribute.Modes.Enabled", "DpuMode"},
         {"xyz.openbmc_project.Control.NicAttribute.Modes.Disabled", "NicMode"},
         {"xyz.openbmc_project.Control.NicAttribute.Modes.Invaild", "Invaild"}},
    .redfishToDbus = {
        {"DpuMode", "xyz.openbmc_project.Control.NicAttribute.Modes.Enabled"},
        {"NicMode",
         "xyz.openbmc_project.Control.NicAttribute.Modes.Disabled"}}};

const PropertyInfo nicAttributeInfo = {
    .intf = "xyz.openbmc_project.Control.NicAttribute",
    .prop = "NicAttribute",
    .dbusToRedfish = {{"xyz.openbmc_project.Control.NicAttribute.Modes.Enabled", "Enabled"},
                {"xyz.openbmc_project.Control.NicAttribute.Modes.Disabled", "Disabled"},
                {"xyz.openbmc_project.Control.NicAttribute.Modes.Invaild", "Invaild"}},
    .redfishToDbus = {{"Enabled", "xyz.openbmc_project.Control.NicAttribute.Modes.Enabled"},
                {"Disabled", "xyz.openbmc_project.Control.NicAttribute.Modes.Disabled"}}};

const PropertyInfo nicTristateAttributeInfo = {
    .intf = "xyz.openbmc_project.Control.NicTristateAttribute",
    .prop = "NicTristateAttribute",
    .dbusToRedfish = {{"xyz.openbmc_project.Control.NicTristateAttribute.Modes.Default", "Default"},
                 {"xyz.openbmc_project.Control.NicTristateAttribute.Modes.Enabled", "Enabled"},
                 {"xyz.openbmc_project.Control.NicTristateAttribute.Modes.Disabled", "Disabled"},
                 {"xyz.openbmc_project.Control.NicTristateAttribute.Modes.Invaild", "Invaild"}},
    .redfishToDbus = {{"Default", "xyz.openbmc_project.Control.NicTristateAttribute.Modes.Default"},
                 {"Enabled", "xyz.openbmc_project.Control.NicTristateAttribute.Modes.Enabled"},
                 {"Disabled", "xyz.openbmc_project.Control.NicTristateAttribute.Modes.Disabled"}}};

constexpr char oemNvidiaGet[] =
    "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Oem/Nvidia";

constexpr char hostRhimTarget[] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                  "/Oem/Nvidia/Actions/HostRshim.Set";

constexpr char modeTarget[] =
    "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Oem/Nvidia/Actions/Mode.Set";
	constexpr char dpuStrpOptionGet[] =
    "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Oem/Nvidia/Connectx/StrapOptions";
constexpr char dpuHostPrivGet[] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                  "/Oem/Nvidia/Connectx/ExternalHostPrivileges";
constexpr char externalHostPrivilegeTarget[] =
    "/redfish/v1/Systems/" PLATFORMSYSTEMID
    "/Oem/Nvidia/Connectx/ExternalHostPrivileges/Actions/ExternalHostPrivileges.Set";

bluefield::DpuActionSetAndGetProp externalHostPrivilege(
    {{"HostPrivFlashAccess",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_FLASH_ACCESS",
       .propertyInfo = bluefield::nicTristateAttributeInfo}},
     {"HostPrivFwUpdate",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_FW_UPDATE",
       .propertyInfo = bluefield::nicTristateAttributeInfo}},
     {"HostPrivNicReset",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_NIC_RESET",
       .propertyInfo = bluefield::nicTristateAttributeInfo}},
     {"HostPrivNvGlobal",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_NV_GLOBAL",
       .propertyInfo = bluefield::nicTristateAttributeInfo}},
     {"HostPrivNvHost",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_NV_HOST",
       .propertyInfo = bluefield::nicTristateAttributeInfo}},
     {"HostPrivNvInternalCpu",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_NV_INTERNAL_CPU",
       .propertyInfo = bluefield::nicTristateAttributeInfo}},
     {"HostPrivNvPort",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_NV_PORT",
       .propertyInfo = bluefield::nicTristateAttributeInfo}},
     {"HostPrivPccUpdate",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/external_host_privileges/external_host_privileges/HOST_PRIV_PCC_UPDATE",
       .propertyInfo = bluefield::nicTristateAttributeInfo}}},
    bluefield::externalHostPrivilegeTarget);
bluefield::DpuGetProperties starpOptions(
    {{"2PcoreActive",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/2PCORE_ACTIVE",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"CoreBypassN",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/CORE_BYPASS_N",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"DisableInbandRecover",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/DISABLE_INBAND_RECOVER",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"Fnp",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/FNP",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"OscFreq0",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/OSC_FREQ_0",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"OscFreq1",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/OSC_FREQ_1",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PciPartition0",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/PCI_PARTITION_0",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PciPartition1",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/PCI_PARTITION_1",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PciReversal",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/PCI_REVERSAL",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PrimaryIsPcore1",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/PRIMARY_IS_PCORE_1",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"SocketDirect",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/strap_options/SOCKET_DIRECT",
       .propertyInfo = bluefield::nicAttributeInfo}}});
bluefield::DpuGetProperties starpOptionsMask(
    {{"2PcoreActive",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/2PCORE_ACTIVE",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"CoreBypassN",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/CORE_BYPASS_N",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"DisableInbandRecover",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/DISABLE_INBAND_RECOVER",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"Fnp",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj = "/xyz/openbmc_project/network/connectx/strap_options/mask/FNP",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"OscFreq0",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/OSC_FREQ_0",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"OscFreq1",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/OSC_FREQ_1",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PciPartition0",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/PCI_PARTITION_0",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PciPartition1",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/PCI_PARTITION_1",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PciReversal",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/PCI_REVERSAL",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"PrimaryIsPcore1",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/PRIMARY_IS_PCORE_1",
       .propertyInfo = bluefield::nicAttributeInfo}},
     {"SocketDirect",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/strap_options/mask/SOCKET_DIRECT",
       .propertyInfo = bluefield::nicAttributeInfo}}});
bluefield::DpuActionSetAndGetProp hostRshim(
    {{"HostRshim",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/host_access/HOST_PRIV_RSHIM",
       .propertyInfo = bluefield::nicAttributeInfo,
       .required = true}}},
    bluefield::hostRhimTarget);
DpuActionSetAndGetProp mode(
    {{"Mode",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/smartnic_mode/smartnic_mode/INTERNAL_CPU_OFFLOAD_ENGINE",
       .propertyInfo = bluefield::modeInfo,
       .required = true}}},
    modeTarget);

#endif //BMCWEB_ENABLE_NVIDIA_OEM_BF3_PROPERTIES
inline void getIsOemNvidiaRshimEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const char* systemdServiceBf = "org.freedesktop.systemd1";
    const char* systemdUnitIntfBf = "org.freedesktop.systemd1.Unit";
    const char* rshimSystemdObjBf =
        "/org/freedesktop/systemd1/unit/rshim_2eservice";
    std::filesystem::path rshimDir = "/dev/rshim0";

    if (!std::filesystem::exists(rshimDir))
    {
        BMCWEB_LOG_DEBUG << "No /dev/rshim0. Interface not started";
        asyncResp->res.jsonValue["BmcRShim"]["BmcRShimEnabled"] = false;
        return;
    }

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, systemdServiceBf, rshimSystemdObjBf,
        systemdUnitIntfBf, "ActiveState",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& rshimActiveState) {
            if (ec)
            {
                BMCWEB_LOG_ERROR
                    << "DBUS response error for getIsOemNvidiaRshimEnable";
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["BmcRShim"]["BmcRShimEnabled"] =
                (rshimActiveState == "active");
        });
}

inline void
    requestOemNvidiaRshim(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const bool& bmcRshimEnabled)
{
    const char* systemdServiceBf = "org.freedesktop.systemd1";
    const char* systemdUnitIntfBf = "org.freedesktop.systemd1.Unit";
    const char* rshimSystemdObjBf =
        "/org/freedesktop/systemd1/unit/rshim_2eservice";
    std::string method = bmcRshimEnabled ? "Start" : "Stop";

    BMCWEB_LOG_DEBUG << "requestOemNvidiaRshim: " << method.c_str()
                     << " rshim interface";

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR
                    << "DBUS response error for rshim enable/disable";
                messages::internalError(asyncResp->res);
                return;
            }
        },
        systemdServiceBf, rshimSystemdObjBf, systemdUnitIntfBf, method.c_str(),
        "replace");

    messages::success(asyncResp->res);
}

} // namespace bluefield

inline void requestRoutesNvidiaOemBf(App& app)
{

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID "/Oem/Nvidia")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                bluefield::getIsOemNvidiaRshimEnable(asyncResp);
            });
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID "/Oem/Nvidia/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<nlohmann::json> bmcRshim;
                if (!redfish::json_util::readJsonPatch(req, asyncResp->res,
                                                       "BmcRShim", bmcRshim))
                {
                    BMCWEB_LOG_ERROR
                        << "Illegal Property "
                        << asyncResp->res.jsonValue.dump(
                               2, ' ', true,
                               nlohmann::json::error_handler_t::replace);
                    return;
                }
                if (bmcRshim)
                {
                    std::optional<bool> bmcRshimEnabled;
                    if (!redfish::json_util::readJson(*bmcRshim, asyncResp->res,
                                                      "BmcRShimEnabled",
                                                      bmcRshimEnabled))
                    {
                        BMCWEB_LOG_ERROR
                            << "Illegal Property "
                            << asyncResp->res.jsonValue.dump(
                                   2, ' ', true,
                                   nlohmann::json::error_handler_t::replace);
                        return;
                    }

                    bluefield::requestOemNvidiaRshim(asyncResp,
                                                     *bmcRshimEnabled);
                }
            });
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_BF3_PROPERTIES

	    BMCWEB_ROUTE(app, bluefield::hostRhimTarget)
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(&bluefield::DpuActionSetAndGetProp::setAction,
                            &bluefield::hostRshim, std::ref(app)));

    BMCWEB_ROUTE(app, bluefield::modeTarget)
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(&bluefield::DpuActionSetAndGetProp::setAction,
                            &bluefield::mode, std::ref(app)));


    BMCWEB_ROUTE(app, bluefield::externalHostPrivilegeTarget)
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(&bluefield::DpuActionSetAndGetProp::setAction,
                            &bluefield::externalHostPrivilege, std::ref(app)));

    BMCWEB_ROUTE(app, bluefield::oemNvidiaGet)
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                auto& nvidia = asyncResp->res.jsonValue;
                auto& connectx = nvidia["Connectx"];
                auto& actions = nvidia["Actions"];
                auto& hostRshimAction = actions["#HostRshim.Set"];
                auto& modeAction = actions["#Mode.Set"];

                bluefield::mode.getProperty(&nvidia, asyncResp);
                bluefield::hostRshim.getProperty(&nvidia, asyncResp);
                connectx["StrapOptions"]["@odata.id"] = bluefield::dpuStrpOptionGet;
                connectx["ExternalHostPrivilege"]["@odata.id"] = bluefield::dpuHostPrivGet;
                bluefield::mode.getActionInfo(&modeAction);
				bluefield::hostRshim.getActionInfo(&hostRshimAction);
            });

    BMCWEB_ROUTE(app, bluefield::dpuStrpOptionGet)
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                auto& strapOptionsJson =
                    asyncResp->res.jsonValue["StrapOptions"];
                auto& mask = asyncResp->res.jsonValue["Mask"];

                bluefield::starpOptions.getProperty(&strapOptionsJson, asyncResp);
                bluefield::starpOptionsMask.getProperty(&mask, asyncResp);
            });

    BMCWEB_ROUTE(app, bluefield::dpuHostPrivGet)
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                auto& hostPriv =
                    asyncResp->res.jsonValue["ExternalHostPrivilege"];
                auto& actions =
                    asyncResp->res
                        .jsonValue["Actions"]["#ExternalHostPrivilege.Set"];

                bluefield::externalHostPrivilege.getProperty(&hostPriv, asyncResp);
                bluefield::externalHostPrivilege.getActionInfo(&actions);
            });
			#endif //BMCWEB_ENABLE_NVIDIA_OEM_BF3_PROPERTIES
}

} // namespace redfish
