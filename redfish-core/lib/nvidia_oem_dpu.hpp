/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "certificate_service.hpp"
#include "generated/enums/port.hpp"
#include "nlohmann/json.hpp"
#include "task.hpp"

#include <app.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/privilege_utils.hpp>

namespace redfish
{
namespace bluefield
{
static const char* dbusPropertyInterface = "org.freedesktop.DBus.Properties";
static const char* systemdServiceBf = "org.freedesktop.systemd1";
static const char* systemdUnitIntfBf = "org.freedesktop.systemd1.Unit";

static const char* switchModeSystemdObj =
    "/org/freedesktop/systemd1/unit/torswitch_2dmode_2eservice";
static const char* ctlBMCSwitchModeService = "xyz.openbmc_project.Settings";
static const char* ctlBMCSwitchModeBMCObj =
    "/xyz/openbmc_project/control/torswitchportsmode";
static const char* ctlBMCSwitchModeIntf =
    "xyz.openbmc_project.Control.TorSwitchPortsMode";
static const char* ctlBMCSwitchMode = "TorSwitchPortsMode";

static const char* ctl3PortSwitchLinkStatusTool =
    "/usr/bin/get_3port_switch_link";

static const std::string& truststoreBiosService =
    "xyz.openbmc_project.Certs.Manager.AuthorityBios.TruststoreBios";
static const std::string& truststoreBiosPath =
    "/xyz/openbmc_project/certs/authorityBios/truststoreBios";

static const std::string dpuFruObj = "xyz.openbmc_project.Control.dpu_fru";
static const std::string dpuFruPath =
    "/xyz/openbmc_project/inventory/system/board";

static const std::string socForceResetTraget =
    "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
    "/Oem/Nvidia/SOC.ForceReset";

static const char* oemFruService = "xyz.openbmc_project.DPU.Config_oem_fru";
static const char* oemFruObj = "/xyz/openbmc_project/oem_fru";
static const char* oemFruIntf = "xyz.openbmc_project.OemFruDevice";

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
        const std::unordered_map<std::string, ObjectInfo>& objects2) :
        DpuCommonProperties(objects2)
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
                    BMCWEB_LOG_DEBUG("DBUS response error for {}", name);
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
        const std::unordered_map<std::string, ObjectInfo>& objects2,
        const std::string targetIn) :
        DpuCommonProperties(objects2), target(targetIn)
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
                BMCWEB_LOG_DEBUG("Missing required param: {}", name);
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
                        BMCWEB_LOG_ERROR("Set failed {}", ec);
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
        const std::unordered_map<std::string, ObjectInfo>& objects3,
        const std::string target2) :
        DpuCommonProperties(objects3),
        DpuActionSetProperties(objects3, target2), DpuGetProperties(objects3)

    {}
};

const PropertyInfo modeInfo = {
    .intf = "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute",
    .prop = "NicAttribute",
    .dbusToRedfish =
        {{"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Enabled",
          "DpuMode"},
         {"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Disabled",
          "NicMode"},
         {"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Invaild",
          "Invaild"}},
    .redfishToDbus = {
        {"DpuMode",
         "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Enabled"},
        {"NicMode",
         "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Disabled"}}};

const PropertyInfo nicAttributeInfo = {
    .intf = "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute",
    .prop = "NicAttribute",
    .dbusToRedfish =
        {{"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Enabled",
          "Enabled"},
         {"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Disabled",
          "Disabled"},
         {"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Invaild",
          "Invaild"}},
    .redfishToDbus = {
        {"Enabled",
         "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Enabled"},
        {"Disabled",
         "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicAttribute.Modes.Disabled"}}};

const PropertyInfo nicTristateAttributeInfo = {
    .intf = "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute",
    .prop = "NicTristateAttribute",
    .dbusToRedfish =
        {{"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute.Modes.Default",
          "Default"},
         {"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute.Modes.Enabled",
          "Enabled"},
         {"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute.Modes.Disabled",
          "Disabled"},
         {"xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute.Modes.Invaild",
          "Invaild"}},
    .redfishToDbus = {
        {"Default",
         "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute.Modes.Default"},
        {"Enabled",
         "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute.Modes.Enabled"},
        {"Disabled",
         "xyz.openbmc_project.Control.NcSi.OEM.Nvidia.NicTristateAttribute.Modes.Disabled"}}};

const std::string hostRhimTarget =
    "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
    "/Oem/Nvidia/Actions/HostRshim.Set";

const std::string modeTarget =
    "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
    "/Oem/Nvidia/Actions/Mode.Set";
const std::string dpuStrpOptionGet =
    "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
    "/Oem/Nvidia/Connectx/StrapOptions";
const std::string dpuHostPrivGet =
    "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
    "/Oem/Nvidia/Connectx/ExternalHostPrivileges";
const std::string externalHostPrivilegeTarget =
    "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
    "/Oem/Nvidia/Connectx/ExternalHostPrivileges/Actions/ExternalHostPrivileges.Set";

inline bluefield::DpuActionSetAndGetProp externalHostPrivilege(
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
inline bluefield::DpuGetProperties starpOptions(
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
inline bluefield::DpuGetProperties starpOptionsMask(
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
inline bluefield::DpuActionSetAndGetProp hostRshim(
    {{"HostRshim",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/host_access/HOST_PRIV_RSHIM",
       .propertyInfo = bluefield::nicAttributeInfo,
       .required = true}}},
    bluefield::hostRhimTarget);
inline DpuActionSetAndGetProp mode(
    {{"Mode",
      {.service = "xyz.openbmc_project.Settings.connectx",
       .obj =
           "/xyz/openbmc_project/network/connectx/smartnic_mode/smartnic_mode/INTERNAL_CPU_OFFLOAD_ENGINE",
       .propertyInfo = bluefield::modeInfo,
       .required = true}}},
    modeTarget);

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
        BMCWEB_LOG_DEBUG("No /dev/rshim0. Interface not started");
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
                BMCWEB_LOG_ERROR(
                    "DBUS response error for getIsOemNvidiaRshimEnable");
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

    BMCWEB_LOG_DEBUG("requestOemNvidiaRshim: {} rshim interface",
                     method.c_str());

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for rshim enable/disable");
                messages::internalError(asyncResp->res);
                return;
            }
        },
        systemdServiceBf, rshimSystemdObjBf, systemdUnitIntfBf, method.c_str(),
        "replace");

    messages::success(asyncResp->res);
}

inline void getOemNvidiaSwitchLinkStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portName, const std::string& portIndex)
{
    std::string command =
        std::string(ctl3PortSwitchLinkStatusTool) + " " + portIndex;

    auto callback = [asyncResp, portName](const boost::system::error_code& ec,
                                          int exitCode) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to execute command with error: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        port::LinkStatus status;
        if (exitCode == static_cast<int>(port::LinkStatus::LinkUp))
        {
            status = port::LinkStatus::LinkUp;
        }
        else if (exitCode == static_cast<int>(port::LinkStatus::LinkDown))
        {
            status = port::LinkStatus::LinkDown;
        }
        else
        {
            status = port::LinkStatus::Invalid;
            BMCWEB_LOG_ERROR("Unexpected exit code: {}", exitCode);
        }

        asyncResp->res.jsonValue["LinkStatus"][portName] = status;
    };

    boost::process::async_system(
        crow::connections::systemBus->get_io_context(), std::move(callback),
        command, boost::process::std_in.close(),
        boost::process::std_out > boost::process::null);
}

/**
 * @brief Retrieve the current switch status and append to the response message
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @return None
 */
inline void getOemNvidiaSwitchStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    bluefield::getOemNvidiaSwitchLinkStatus(asyncResp, "RJ45", "1");
    bluefield::getOemNvidiaSwitchLinkStatus(asyncResp, "DPU", "2");
    // As BMC is not connected through PHY, it cannot manage properties like
    // Link status, Duplex, Speed, etc. The values come hardcoded in the HW
    // and are constant.
    asyncResp->res.jsonValue["LinkStatus"]["BMC"] = port::LinkStatus::LinkUp;

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    std::variant<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for getting OOB status");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string* strValue = std::get_if<std::string>(&resp);
            if (strValue == nullptr)
            {
                return;
            }
            if (*strValue ==
                "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.All")
            {
                asyncResp->res.jsonValue["TorSwitchMode"]["BmcOobEnabled"] =
                    true;
                asyncResp->res.jsonValue["TorSwitchMode"]["DpuOobEnabled"] =
                    true;
                return;
            }
            if (*strValue ==
                "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.BMC")
            {
                asyncResp->res.jsonValue["TorSwitchMode"]["BmcOobEnabled"] =
                    true;
                asyncResp->res.jsonValue["TorSwitchMode"]["DpuOobEnabled"] =
                    false;
                return;
            }
            if (*strValue ==
                "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.DPU")
            {
                asyncResp->res.jsonValue["TorSwitchMode"]["BmcOobEnabled"] =
                    false;
                asyncResp->res.jsonValue["TorSwitchMode"]["DpuOobEnabled"] =
                    true;
                return;
            }
            if (*strValue ==
                "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.None")
            {
                asyncResp->res.jsonValue["TorSwitchMode"]["BmcOobEnabled"] =
                    false;
                asyncResp->res.jsonValue["TorSwitchMode"]["DpuOobEnabled"] =
                    false;
                return;
            }
            if (*strValue ==
                "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.Disable")
            {
                asyncResp->res.jsonValue["TorSwitchMode"]["BmcOobEnabled"] =
                    false;
                asyncResp->res.jsonValue["TorSwitchMode"]["DpuOobEnabled"] =
                    false;
                return;
            }
        },
        ctlBMCSwitchModeService, ctlBMCSwitchModeBMCObj, dbusPropertyInterface,
        "Get", ctlBMCSwitchModeIntf, ctlBMCSwitchMode);
}

/**
 * @brief Modify switch port status from user requests
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] bmcOobEnabled true when BMC OOB Port is enabled to access outside
 * network
 * @param[in] dpuOobEnabled true when DPU OOB Port is enabled to access outside
 * network
 * @return None
 */
inline void
    requestOemNvidiaSwitch(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const bool& bmcOobEnabled, const bool& dpuOobEnabled)
{
    std::string method = dpuOobEnabled ? "Enable" : "Disable";
    std::string strValue;
    BMCWEB_LOG_DEBUG("requestOemNvidiaSwitch: {} DPU OOB Port", method.c_str());

    // Only "dpuOobEnabled" take effect.
    // User can't use redfish to disable the BMC OOB Port.
    // If user set BMC Port as disabled, it will return
    // actionParameterValueError error.
    if (bmcOobEnabled == true && dpuOobEnabled == true)
    {
        strValue = "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.All";
    }
    else if (bmcOobEnabled == true && dpuOobEnabled == false)
    {
        strValue = "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.BMC";
    }
    else
    {
        messages::actionParameterValueError(asyncResp->res, "bmcOobEnabled",
                                            "false");
        return;
    }

    std::variant<std::string> variantValue(strValue);

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for setting DPU OOB enable/disable");
                messages::internalError(asyncResp->res);
                return;
            }
            // Reload switch service to make the new configuration take effect
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error for resetting switch mode service");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                },
                systemdServiceBf, switchModeSystemdObj, systemdUnitIntfBf,
                "Restart", "replace");
        },
        ctlBMCSwitchModeService, ctlBMCSwitchModeBMCObj, dbusPropertyInterface,
        "Set", ctlBMCSwitchModeIntf, ctlBMCSwitchMode, variantValue);

    messages::success(asyncResp->res);
}

/**
 * @brief Reset the switch setting
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @return None
 */
inline void resetTorSwitch(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    try
    {
        boost::process::child execProg("/usr/sbin/mlnx_bf_reset_control",
                                       "do_tor_eswitch_reset");
        execProg.wait();
        if (!execProg.exit_code())
        {
            BMCWEB_LOG_DEBUG("Reset switch to default");
        }
    }
    catch (const std::runtime_error& e)
    {
        BMCWEB_LOG_ERROR("mlnx_bf_reset_control script failed with error: {}",
                         e.what());
    }

    // Restore the Dbus property after the switch reset successful
    std::variant<std::string> variantValue(
        "xyz.openbmc_project.Control.TorSwitchPortsMode.Modes.All");
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for setting DPU OOB enable/disable");
                messages::internalError(asyncResp->res);
                return;
            }
        },
        ctlBMCSwitchModeService, ctlBMCSwitchModeBMCObj, dbusPropertyInterface,
        "Set", ctlBMCSwitchModeIntf, ctlBMCSwitchMode, variantValue);

    messages::success(asyncResp->res);
}

inline void handleTruststoreCertificatesCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/Truststore/Certificates";
    asyncResp->res.jsonValue["@odata.type"] =
        "#CertificateCollection.CertificateCollection";
    asyncResp->res.jsonValue["Name"] = "TruststoreBios Certificate Collection";
    asyncResp->res.jsonValue["@Redfish.SupportedCertificates"] = {"PEM"};

    const std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Certs.Certificate"};
    redfish::collection_util::getCollectionMembers(
        asyncResp,
        boost::urls::url("/redfish/v1/Systems/" +
                         std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                         "/Oem/Nvidia/Truststore/Certificates"),
        interfaces, std::string(truststoreBiosPath).c_str());
}

inline void createPendingRequest(
    const crow::Request& req, const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    auto task = task::TaskData::createTask(
        [](boost::system::error_code, sdbusplus::message_t&,
           const std::shared_ptr<task::TaskData>&) { return false; },
        "0");
    task->payload.emplace(req);
    task->state = "Pending";
    task->populateResp(aResp->res);
    return;
}

inline void handleTruststoreCertificatesCollectionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::string certString;
    std::string certType;
    std::optional<std::string> owner;
    if (!json_util::readJsonAction(req, asyncResp->res, "CertificateString",
                                   certString, "CertificateType", certType,
                                   "UefiSignatureOwner", owner))
    {
        return;
    }

    if (certString.size() == 0)
    {
        messages::propertyValueIncorrect(asyncResp->res, "CertificateString",
                                         certString);
        return;
    }

    if ((certType != "PEM") && (certType != "PEMchain"))
    {
        messages::propertyValueNotInList(asyncResp->res, certType,
                                         "CertificateType");
        return;
    }

    privilege_utils::isBiosPrivilege(
        req, [req, asyncResp, certString, certType,
              owner](const boost::system::error_code ec, const bool isBios) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (isBios == false)
            {
                createPendingRequest(req, asyncResp);
                return;
            }

            std::shared_ptr<CertificateFile> certFile =
                std::make_shared<CertificateFile>(certString);

            crow::connections::systemBus->async_method_call(
                [asyncResp, owner, certFile](const boost::system::error_code ec,
                                             const std::string& objectPath) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    sdbusplus::message::object_path path(objectPath);
                    std::string certId = path.filename();
                    messages::created(asyncResp->res);
                    asyncResp->res.addHeader(
                        boost::beast::http::field::location,
                        "/redfish/v1/Systems/" +
                            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                            "/Oem/Nvidia/Truststore/Certificates/" + certId);

                    if (owner)
                    {
                        crow::connections::systemBus->async_method_call(
                            [asyncResp](const boost::system::error_code ec) {
                                if (ec)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                            },
                            truststoreBiosService, objectPath,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.Common.UUID", "UUID",
                            dbus::utility::DbusVariantType(*owner));
                    }
                },
                truststoreBiosService, truststoreBiosPath,
                "xyz.openbmc_project.Certs.Install", "Install",
                certFile->getCertFilePath());
        });
}

inline void handleTruststoreCertificatesGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName, const std::string& certId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/Truststore/Certificates/" + certId;
    asyncResp->res.jsonValue["@odata.type"] = "#Certificate.v1_7_0.Certificate";
    asyncResp->res.jsonValue["Id"] = certId;
    asyncResp->res.jsonValue["Name"] = "TruststoreBios Certificate";

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, truststoreBiosService,
        truststoreBiosPath + "/" + certId, "",
        [asyncResp,
         certId](const boost::system::error_code ec,
                 const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                messages::resourceNotFound(asyncResp->res, "Certificate",
                                           certId);
                return;
            }

            const std::string* certificateString = nullptr;
            const std::vector<std::string>* keyUsage = nullptr;
            const std::string* issuer = nullptr;
            const std::string* subject = nullptr;
            const uint64_t* validNotAfter = nullptr;
            const uint64_t* validNotBefore = nullptr;
            const std::string* owner = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList,
                "CertificateString", certificateString, "KeyUsage", keyUsage,
                "Issuer", issuer, "Subject", subject, "ValidNotAfter",
                validNotAfter, "ValidNotBefore", validNotBefore, "UUID", owner);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["CertificateString"] = "";
            asyncResp->res.jsonValue["KeyUsage"] = nlohmann::json::array();

            if (certificateString != nullptr)
            {
                asyncResp->res.jsonValue["CertificateString"] =
                    *certificateString;
                asyncResp->res.jsonValue["CertificateType"] = "PEM";
            }

            if (keyUsage != nullptr)
            {
                asyncResp->res.jsonValue["KeyUsage"] = *keyUsage;
            }

            if (issuer != nullptr)
            {
                updateCertIssuerOrSubject(asyncResp->res.jsonValue["Issuer"],
                                          *issuer);
            }

            if (subject != nullptr)
            {
                updateCertIssuerOrSubject(asyncResp->res.jsonValue["Subject"],
                                          *subject);
            }

            if (validNotAfter != nullptr)
            {
                asyncResp->res.jsonValue["ValidNotAfter"] =
                    redfish::time_utils::getDateTimeUint(*validNotAfter);
            }

            if (validNotBefore != nullptr)
            {
                asyncResp->res.jsonValue["ValidNotBefore"] =
                    redfish::time_utils::getDateTimeUint(*validNotBefore);
            }

            if (owner != nullptr)
            {
                asyncResp->res.jsonValue["UefiSignatureOwner"] = *owner;
            }
        });
}

inline void handleTruststoreCertificatesDelete(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName, const std::string& certId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    privilege_utils::isBiosPrivilege(
        req, [req, asyncResp,
              certId](const boost::system::error_code ec, const bool isBios) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (isBios == false)
            {
                createPendingRequest(req, asyncResp);
                return;
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp, certId](const boost::system::error_code ec) {
                    if (ec.value() == EBADR)
                    {
                        messages::resourceNotFound(asyncResp->res, "certId",
                                                   certId);
                        return;
                    }
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.result(
                        boost::beast::http::status::no_content);
                },
                truststoreBiosService, truststoreBiosPath + "/" + certId,
                "xyz.openbmc_project.Object.Delete", "Delete");
        });
}

inline void handleTruststoreCertificatesResetKeys(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::string resetKeysType;
    if (!json_util::readJsonAction(req, asyncResp->res, "ResetKeysType",
                                   resetKeysType))
    {
        return;
    }

    if (resetKeysType != "DeleteAllKeys")
    {
        messages::propertyValueNotInList(asyncResp->res, resetKeysType,
                                         "ResetKeysType");
        return;
    }

    privilege_utils::isBiosPrivilege(req, [req, asyncResp](
                                              const boost::system::error_code
                                                  ec,
                                              const bool isBios) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        if (isBios == false)
        {
            // UEFI requires the "Action" target to be under
            // "Truststore/Certificates" in order to identify the source of this
            // action. Since the action is placed under the general "Action"
            // section, The request is being edited with the required TargetUri
            crow::Request reqFixedTar(req);
            reqFixedTar.target(
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/Oem/Nvidia/Truststore/Certificates/Actions/TruststoreCertificates.ResetKeys");
            createPendingRequest(reqFixedTar, asyncResp);
            return;
        }

        // BIOS does use action. It DELETE and POST certificates and
        // signatures
        messages::actionNotSupported(asyncResp->res, "ResetKeys");
        return;
    });
}

inline void handleGetOemFru([[maybe_unused]] crow::App& app,
                            const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Check if the OEM FRU is enabled
    // OEM FRU only available when the "Enabled" property is true
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/oem_fru",
        "xyz.openbmc_project.Object.Enable", "Enabled",
        [req, asyncResp](const boost::system::error_code& ec, bool enabled) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error: Checking OEM FRU Enabled error{}",
                    ec);
                return;
            }
            if (!enabled)
            {
                // If OEM FRU is disabled, do not report an error
                // This is because same URL will also be used for other features
                return;
            }
            // Fetch all properties of the OEM FRU object
            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, oemFruService, oemFruObj,
                oemFruIntf,
                [asyncResp{asyncResp}](
                    const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& propertyList) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error: Get All OEM FRU Property error{}",
                            ec);
                        return;
                    }
                    const std::string* productManufacturer = nullptr;
                    const std::string* productSerialNumber = nullptr;
                    const std::string* productPartNumber = nullptr;
                    const std::string* productVersion = nullptr;
                    const std::string* productExtra = nullptr;
                    const std::string* productManufactureDate = nullptr;
                    const std::string* ProductAssetTag = nullptr;
                    const std::string* productGUID = nullptr;
                    // Unpack properties from the property list
                    const bool success = sdbusplus::unpackPropertiesNoThrow(
                        dbus_utils::UnpackErrorPrinter(), propertyList,
                        "PRODUCT_MANUFACTURER", productManufacturer,
                        "PRODUCT_SERIAL_NUMBER", productSerialNumber,
                        "PRODUCT_PART_NUMBER", productPartNumber,
                        "PRODUCT_VERSION", productVersion, "PRODUCT_INFO_AM1",
                        productExtra, "BOARD_MANUFACTURE_DATE",
                        productManufactureDate, "PRODUCT_ASSET_TAG",
                        ProductAssetTag, "CHASSIS_INFO_AM1", productGUID);
                    if (!success)
                    {
                        BMCWEB_LOG_ERROR("Unpack OEM FRU Property error");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Populate the response with the OEM data values
                    if (productManufacturer != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductManufacturer"] =
                            *productManufacturer;
                    }
                    if (productSerialNumber != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductSerialNumber"] =
                            *productSerialNumber;
                    }
                    if (productPartNumber != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductPartNumber"] =
                            *productPartNumber;
                    }
                    if (productVersion != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductVersion"] =
                            *productVersion;
                    }
                    if (productExtra != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductExtra"] =
                            *productExtra;
                    }
                    if (productManufactureDate != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductManufactureDate"] =
                            *productManufactureDate;
                    }
                    if (ProductAssetTag != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductAssetTag"] =
                            *ProductAssetTag;
                    }
                    if (productGUID != nullptr)
                    {
                        asyncResp->res.jsonValue["ProductGUID"] = *productGUID;
                    }
                });
        });
}

inline void
    setOemFruProperty(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& dbusProperty,
                      const std::string& lastProperty, const std::string& value)
{
    std::variant<std::string> variantValue(value);
    crow::connections::systemBus->async_method_call(
        [asyncResp, dbusProperty, lastProperty,
         value](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error: Set OEM FRU Property error{}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            // Sync OEM FRU data only when the last property is set
            // This is to avoid multiple SyncOemFru calls
            if (lastProperty == dbusProperty)
            {
                // Make an asynchronous DBUS call to sync the OEM FRU data
                // The FRU DBUS object and config flash will be updated
                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "DBUS response error: Sync OEM FRU Data error{}",
                                ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    },
                    oemFruService, oemFruObj, oemFruIntf, "SyncOemFru");
                // Send success response after set last property and save the
                // FRU data
                messages::success(asyncResp->res);
            }
        },
        oemFruService, oemFruObj, dbusPropertyInterface, "Set", oemFruIntf,
        dbusProperty, variantValue);
}

inline void handleSetOemFru([[maybe_unused]] crow::App& app,
                            const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Set OEM FRU info");

    // Check if the request has host interface privilege
    // The redfish host interface will be prevented to accesss the OEM FRU
    privilege_utils::isBiosPrivilege(req, [req, asyncResp](
                                              const boost::system::error_code
                                                  ec,
                                              const bool isBios) {
        if (ec)
        {
            messages::insufficientPrivilege(asyncResp->res);
            return;
        }
        if (isBios == false)
        {
            // Check if the OEM FRU is enabled
            // OEM FRU only available when the "Enabled" property is true
            sdbusplus::asio::getProperty<bool>(
                *crow::connections::systemBus, "xyz.openbmc_project.Settings",
                "/xyz/openbmc_project/control/oem_fru",
                "xyz.openbmc_project.Object.Enable", "Enabled",
                [req,
                 asyncResp](const boost::system::error_code& ec, bool enabled) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error: Checking OEM FRU Enabled error{}",
                            ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (!enabled)
                    {
                        messages::actionNotSupported(asyncResp->res,
                                                     "OEM FRU not enabled");
                        return;
                    }
                    // Map of property names to their corresponding DBUS
                    // property names
                    std::unordered_map<std::string, std::string> propertyMap = {
                        {"ProductManufacturer", "PRODUCT_MANUFACTURER"},
                        {"ProductSerialNumber", "PRODUCT_SERIAL_NUMBER"},
                        {"ProductPartNumber", "PRODUCT_PART_NUMBER"},
                        {"ProductVersion", "PRODUCT_VERSION"},
                        {"ProductExtra", "PRODUCT_INFO_AM1"},
                        {"ProductManufactureDate", "BOARD_MANUFACTURE_DATE"},
                        {"ProductAssetTag", "PRODUCT_ASSET_TAG"},
                        {"ProductGUID", "CHASSIS_INFO_AM1"}};
                    // Initialize the last property to be set
                    std::string lastProperty = "CHASSIS_INFO_AM1";
                    std::optional<std::string> productManufacturer;
                    std::optional<std::string> productSerialNumber;
                    std::optional<std::string> productPartNumber;
                    std::optional<std::string> productVersion;
                    std::optional<std::string> productExtra;
                    std::optional<std::string> productManufactureDate;
                    std::optional<std::string> productAssetTag;
                    std::optional<std::string> productGUID;
                    // Read the data from the post request and populate the
                    // optional variables
                    if (!json_util::readJsonPatch(
                            req, asyncResp->res, "ProductManufacturer",
                            productManufacturer, "ProductSerialNumber",
                            productSerialNumber, "ProductPartNumber",
                            productPartNumber, "ProductVersion", productVersion,
                            "ProductExtra", productExtra,
                            "ProductManufactureDate", productManufactureDate,
                            "ProductAssetTag", productAssetTag, "ProductGUID",
                            productGUID))
                    {
                        return;
                    }
                    // Only sync the OEM FRU data one time when setting the last
                    // property Determine the last property to set based on the
                    // provided values
                    if (productManufacturer)
                    {
                        lastProperty = propertyMap["ProductManufacturer"];
                    }
                    if (productSerialNumber)
                    {
                        lastProperty = propertyMap["ProductSerialNumber"];
                    }
                    if (productPartNumber)
                    {
                        lastProperty = propertyMap["ProductPartNumber"];
                    }
                    if (productVersion)
                    {
                        lastProperty = propertyMap["ProductVersion"];
                    }
                    if (productExtra)
                    {
                        lastProperty = propertyMap["ProductExtra"];
                    }
                    if (productManufactureDate)
                    {
                        lastProperty = propertyMap["ProductManufactureDate"];
                    }
                    if (productAssetTag)
                    {
                        lastProperty = propertyMap["ProductAssetTag"];
                    }
                    if (productGUID)
                    {
                        lastProperty = propertyMap["ProductGUID"];
                    }
                    // Set each property with OEM data using the
                    // setOemFruProperty function
                    if (productManufacturer)
                    {
                        setOemFruProperty(asyncResp,
                                          propertyMap["ProductManufacturer"],
                                          lastProperty, *productManufacturer);
                    }
                    if (productSerialNumber)
                    {
                        setOemFruProperty(asyncResp,
                                          propertyMap["ProductSerialNumber"],
                                          lastProperty, *productSerialNumber);
                    }
                    if (productPartNumber)
                    {
                        setOemFruProperty(asyncResp,
                                          propertyMap["ProductPartNumber"],
                                          lastProperty, *productPartNumber);
                    }
                    if (productVersion)
                    {
                        setOemFruProperty(asyncResp,
                                          propertyMap["ProductVersion"],
                                          lastProperty, *productVersion);
                    }
                    if (productExtra)
                    {
                        setOemFruProperty(asyncResp,
                                          propertyMap["ProductExtra"],
                                          lastProperty, *productExtra);
                    }
                    if (productManufactureDate)
                    {
                        setOemFruProperty(
                            asyncResp, propertyMap["ProductManufactureDate"],
                            lastProperty, *productManufactureDate);
                    }
                    if (productAssetTag)
                    {
                        setOemFruProperty(asyncResp,
                                          propertyMap["ProductAssetTag"],
                                          lastProperty, *productAssetTag);
                    }
                    if (productGUID)
                    {
                        setOemFruProperty(asyncResp, propertyMap["ProductGUID"],
                                          lastProperty, *productGUID);
                    }
                });
            return;
        }
        // Respond with action not supported if the request is from Redfish Host
        // Interface
        messages::actionNotSupported(
            asyncResp->res, "Setting OEM FRU Data from Redfish Host Interface");
        return;
    });
}

} // namespace bluefield

inline void requestRoutesNvidiaOemBf(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                bluefield::getIsOemNvidiaRshimEnable(asyncResp);
            });
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<nlohmann::json> bmcRshim;
                if (!redfish::json_util::readJsonPatch(req, asyncResp->res,
                                                       "BmcRShim", bmcRshim))
                {
                    BMCWEB_LOG_ERROR(
                        "Illegal Property {}",
                        asyncResp->res.jsonValue.dump(
                            2, ' ', true,
                            nlohmann::json::error_handler_t::replace));
                    return;
                }
                if (bmcRshim)
                {
                    std::optional<bool> bmcRshimEnabled;
                    if (!redfish::json_util::readJson(*bmcRshim, asyncResp->res,
                                                      "BmcRShimEnabled",
                                                      bmcRshimEnabled))
                    {
                        BMCWEB_LOG_ERROR(
                            "Illegal Property {}",
                            asyncResp->res.jsonValue.dump(
                                2, ' ', true,
                                nlohmann::json::error_handler_t::replace));
                        return;
                    }

                    bluefield::requestOemNvidiaRshim(asyncResp,
                                                     *bmcRshimEnabled);
                }
            });
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/Switch")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                bluefield::getOemNvidiaSwitchStatus(asyncResp);
            });
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/Switch")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<nlohmann::json> torSwitchMode;
                if (!redfish::json_util::readJsonPatch(
                        req, asyncResp->res, "TorSwitchMode", torSwitchMode))
                {
                    BMCWEB_LOG_ERROR(
                        "Illegal Property {}",
                        asyncResp->res.jsonValue.dump(
                            2, ' ', true,
                            nlohmann::json::error_handler_t::replace));
                    return;
                }
                if (torSwitchMode)
                {
                    std::optional<bool> bmcOobEnabled;
                    std::optional<bool> dpuOobEnabled;
                    if (!redfish::json_util::readJson(
                            *torSwitchMode, asyncResp->res, "BmcOobEnabled",
                            bmcOobEnabled, "DpuOobEnabled", dpuOobEnabled))
                    {
                        BMCWEB_LOG_ERROR(
                            "Illegal Property {}",
                            asyncResp->res.jsonValue.dump(
                                2, ' ', true,
                                nlohmann::json::error_handler_t::replace));
                        return;
                    }
                    bluefield::requestOemNvidiaSwitch(asyncResp, *bmcOobEnabled,
                                                      *dpuOobEnabled);
                }
            });
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/Switch.Reset")
        .privileges(redfish::privileges::postSwitch)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                bluefield::resetTorSwitch(asyncResp);
            });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Oem/Nvidia/Truststore/Certificates")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            bluefield::handleTruststoreCertificatesCollectionGet,
            std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Oem/Nvidia/Truststore/Certificates")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            bluefield::handleTruststoreCertificatesCollectionPost,
            std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/Truststore/Certificates/<str>")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            bluefield::handleTruststoreCertificatesGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/Truststore/Certificates/<str>")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            bluefield::handleTruststoreCertificatesDelete, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/Actions/TruststoreCertificates.ResetKeys")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            bluefield::handleTruststoreCertificatesResetKeys, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/SOC.ForceReset")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          [[maybe_unused]] const std::string& systemName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            auto dataOut = std::make_shared<boost::process::ipstream>();
            auto dataErr = std::make_shared<boost::process::ipstream>();
            auto callback = [asyncResp, dataOut,
                             dataErr](const boost::system::error_code& ec,
                                      int errorCode) mutable {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "mlnx_bf_reset_control script failed with error code: {} {}",
                        ec, errorCode);
                    messages::operationFailed(asyncResp->res);
                    return;
                }
                BMCWEB_LOG_DEBUG("SOC Hard Reset");
                messages::success(asyncResp->res);
            };

            std::string command =
                "/usr/sbin/mlnx_bf_reset_control soc_hard_reset_ignore_host";
            boost::process::async_system(
                crow::connections::systemBus->get_io_context(),
                std::move(callback), command, bp::std_in.close(),
                bp::std_out > *dataOut, bp::std_err > *dataErr);
        });

    if constexpr (BMCWEB_NVIDIA_OEM_BF3_PROPERTIES)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Oem/Nvidia/Actions/HostRshim.Set")
            .privileges(redfish::privileges::postComputerSystem)
            .methods(boost::beast::http::verb::post)(
                std::bind_front(&bluefield::DpuActionSetAndGetProp::setAction,
                                &bluefield::hostRshim, std::ref(app)));

        BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Oem/Nvidia/Actions/Mode.Set")
            .privileges(redfish::privileges::postComputerSystem)
            .methods(boost::beast::http::verb::post)(
                std::bind_front(&bluefield::DpuActionSetAndGetProp::setAction,
                                &bluefield::mode, std::ref(app)));

        BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Oem/Nvidia/Actions/Mode.Set")
            .privileges(redfish::privileges::postComputerSystem)
            .methods(boost::beast::http::verb::post)(
                std::bind_front(&bluefield::DpuActionSetAndGetProp::setAction,
                                &bluefield::mode, std::ref(app)));

        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/Oem/Nvidia/Connectx/ExternalHostPrivileges/Actions/ExternalHostPrivileges.Set")
            .privileges(redfish::privileges::postComputerSystem)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                &bluefield::DpuActionSetAndGetProp::setAction,
                &bluefield::externalHostPrivilege, std::ref(app)));
    }
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia")
        .privileges(redfish::privileges::putComputerSystem)
        .methods(boost::beast::http::verb::put)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                bluefield::handleSetOemFru(app, req, asyncResp);
            });
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Oem/Nvidia";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#NvidiaComputerSystem.v1_0_0.NvidiaComputerSystem";
                auto& nvidia = asyncResp->res.jsonValue;
                auto& actions = nvidia["Actions"];
                auto& socForceReset = actions["#SOC.ForceReset"];
                if constexpr (BMCWEB_NVIDIA_OEM_BF3_PROPERTIES)
                {
                    auto& connectx = nvidia["Connectx"];
                    auto& hostRshimAction = actions["#HostRshim.Set"];
                    auto& modeAction = actions["#Mode.Set"];

                    bluefield::mode.getProperty(&nvidia, asyncResp);
                    bluefield::hostRshim.getProperty(&nvidia, asyncResp);
                    connectx["StrapOptions"]["@odata.id"] =
                        bluefield::dpuStrpOptionGet;
                    connectx["ExternalHostPrivilege"]["@odata.id"] =
                        bluefield::dpuHostPrivGet;
                    bluefield::mode.getActionInfo(&modeAction);
                    bluefield::hostRshim.getActionInfo(&hostRshimAction);

                    nvidia["Truststore"]["Certificates"]["@odata.id"] =
                        "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/Oem/Nvidia/Truststore/Certificates";

                    actions["#TruststoreCertificates.ResetKeys"]["target"] =
                        "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/Oem/Nvidia/Actions/TruststoreCertificates.ResetKeys";

                    actions["#TruststoreCertificates.ResetKeys"]
                           ["ResetKeysType@Redfish.AllowableValues"] = {
                               "DeleteAllKeys"};
                }
                socForceReset["target"] = bluefield::socForceResetTraget;
                bluefield::handleGetOemFru(app, req, asyncResp);
                sdbusplus::asio::getAllProperties(
                    *crow::connections::systemBus, bluefield::dpuFruObj,
                    bluefield::dpuFruPath,
                    "xyz.openbmc_project.Inventory.Host.BfFruInfo",
                    [asyncResp](const boost::system::error_code ec,
                                const dbus::utility::DBusPropertiesMap&
                                    propertiesList) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                            return;
                        }

                        const std::string* baseMac = nullptr;
                        const std::string* baseGuid = nullptr;
                        const std::string* description = nullptr;

                        const bool success = sdbusplus::unpackPropertiesNoThrow(
                            dbus_utils::UnpackErrorPrinter(), propertiesList,
                            "Description", description, "BaseGUID", baseGuid,
                            "BaseMAC", baseMac);

                        if (!success)
                        {
                            return;
                        }

                        if (description != nullptr)
                        {
                            asyncResp->res.jsonValue["Description"] =
                                *description;
                        }

                        if (baseGuid != nullptr)
                        {
                            asyncResp->res.jsonValue["BaseGUID"] = *baseGuid;
                        }

                        if (baseMac != nullptr)
                        {
                            asyncResp->res.jsonValue["BaseMAC"] = *baseMac;
                        }
                    });
                if constexpr (BMCWEB_PROFILE_CONFIGURATION)
                {
                    nvidia["Profiles"]["@odata.id"] =
                        "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/Oem/Nvidia/SystemConfigurationProfile";
                }
            });
    if constexpr (BMCWEB_NVIDIA_OEM_BF3_PROPERTIES)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Oem/Nvidia/Connectx/StrapOptions")
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

                    bluefield::starpOptions.getProperty(&strapOptionsJson,
                                                        asyncResp);
                    bluefield::starpOptionsMask.getProperty(&mask, asyncResp);
                });

        BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Oem/Nvidia/Connectx/ExternalHostPrivileges")
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

                    bluefield::externalHostPrivilege.getProperty(&hostPriv,
                                                                 asyncResp);
                    bluefield::externalHostPrivilege.getActionInfo(&actions);
                });
    }
}

} // namespace redfish
