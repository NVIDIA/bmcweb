/*
// Copyright (c) 2023 Nvidia Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

/*!
 * @file    log_services_util.hpp
 * @brief   Source code for utility functions of log service.
 */

#pragma once
#include "bmcweb_config.h"

#include "com/nvidia/Dump/AllowableValues/server.hpp"
#include "dbus_singleton.hpp"

#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <string>
#include <variant>
#include <vector>

namespace redfish
{

using AllowableValuesIface = sdbusplus::server::object::object<
    sdbusplus::com::nvidia::Dump::server::AllowableValues>;
using DumpType = AllowableValuesIface::DumpType;

inline static std::string getLogEntryDataId(const std::string& id)
{
    return std::string{"/redfish/v1/Systems/" +
                       std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                       "/LogServices/EventLog/Entries/" + id};
}

inline static std::string getLogEntryAdditionalDataURI(const std::string& id)
{
    return getLogEntryDataId(id) + "/attachment";
}

inline static std::string convertEventSeverity(const std::string& severity)
{
    if (severity == "Informational")
    {
        return "OK";
    }
    return severity;
}

inline void populateBootEntryId(crow::Response& resp)
{
    std::string bootEntryId{""};
    std::string filePath{"/run/bootentryid"};

    std::ifstream ifs(filePath);

    if (!ifs.is_open())
    {
        BMCWEB_LOG_ERROR("Can't open file {}!\n", filePath);
        return;
    }

    ifs >> bootEntryId;

    BMCWEB_LOG_ERROR("BootEntryID is {}.\n", bootEntryId);

    resp.jsonValue["Oem"]["Nvidia"]["BootEntryID"] = bootEntryId;
}

template <typename Callback>
inline void getOEMDiagnosticAllowableValues(const std::string& dumpType,
                                            Callback&& callback)
{
    sdbusplus::asio::getProperty<std::map<DumpType, std::vector<std::string>>>(
        *crow::connections::systemBus, "xyz.openbmc_project.Dump.Manager",
        "/xyz/openbmc_project/dump/oem_allowable_values",
        "com.nvidia.Dump.AllowableValues", "OEMDataTypeAllowableValues",
        [dumpType, callback](const boost::system::error_code& ec,
                             const std::map<DumpType, std::vector<std::string>>&
                                 oemAllowableValuesMap) {
        if (ec)
        {
            callback(std::vector<std::string>());
            return;
        }

        for (const auto& [type, oemAllowableValues] : oemAllowableValuesMap)
        {
            std::string typeStr =
                AllowableValuesIface::convertDumpTypeToString(type);
            std::string typeName = typeStr;
            std::size_t pos = typeStr.rfind('.');
            if (pos != std::string::npos)
            {
                typeName = typeStr.substr(pos + 1);
            }

            if (typeName == dumpType && !oemAllowableValues.empty())
            {
                callback(oemAllowableValues);
                return;
            }
        }

        callback(std::vector<std::string>());
    });
}

} // namespace redfish
