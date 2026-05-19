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

#include <app.hpp>
#include <health.hpp>
#include <openbmc_dbus_rest.hpp>
#include <query.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/nvidia_chassis_util.hpp>

#include <iostream>
#include <regex>
#include <variant>

namespace redfish
{

/**
 * @brief Get all assembly info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisId   Chassis that contains the assembly.
 */
inline void updateAssemblies(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath,
    const std::string& chassisId)
{
    BMCWEB_LOG_DEBUG("Get Assemblies Data");
    // Get Assembly ID
    size_t devStart = objPath.rfind('/');
    if (devStart == std::string::npos)
    {
        BMCWEB_LOG_ERROR("Assembly not found {}", objPath);
        return;
    }
    std::string assemblyName = objPath.substr(devStart + 1);
    if (assemblyName.empty())
    {
        BMCWEB_LOG_ERROR("Empty assembly");
        return;
    }
    // seek the last number of the path leaf name.
    std::regex assemblyRegex(".*[^0-9]([0-9]+)$");
    std::cmatch match;
    std::string memberId;
    if (regex_match(assemblyName.c_str(), match, assemblyRegex))
    {
        const std::string& assemblyId = match[1].first;
        memberId = assemblyId;
    }
    // Get interface properties
    dbus::utility::getAllProperties(
        service, objPath, "",
        [asyncResp{asyncResp}, chassisId, memberId,
         objPath](const boost::system::error_code& ec,
                  const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for assembly properties");
                return;
            }
            // Assemblies data
            std::string id = memberId;
            nlohmann::json assemblyRes;
            for (const auto& property : propertiesList)
            {
                // Store DBus properties that are also Redfish
                // properties with same name and a string value
                const std::string& propertyName = property.first;
                if ((propertyName == "Model") || (propertyName == "Name") ||
                    (propertyName == "PartNumber") ||
                    (propertyName == "SerialNumber") ||
                    (propertyName == "Version"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for asset properties");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (!value->empty())
                    {
                        assemblyRes[propertyName] = *value;
                    }
                }
                else if (propertyName == "Manufacturer")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for manufacturer");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    assemblyRes["Vendor"] = *value;
                }
                else if (propertyName == "BuildDate")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for build date");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    assemblyRes["ProductionDate"] = *value;
                }
                else if (propertyName == "LocationContext")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for PartLocationContext");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    assemblyRes["Location"]["PartLocationContext"] = *value;
                }
                else if (propertyName == "LocationType")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for LocationType");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    assemblyRes["Location"]["PartLocation"]["LocationType"] =
                        redfish::dbus_utils::toLocationType(*value);
                }
                else if (propertyName == "LocationCode")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for ServiceLabel");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    assemblyRes["Location"]["PartLocation"]["ServiceLabel"] =
                        *value;
                }
                else if (propertyName == "PhysicalContext")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for PhysicalContext");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    assemblyRes["PhysicalContext"] =
                        redfish::dbus_utils::toPhysicalContext(*value);
                }
                else if (propertyName == "AssemblyID")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("NULL Value returned AssemblyID");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    id = std::to_string(*value);
                }
            }
            assemblyRes["@odata.id"] = "/redfish/v1/Chassis/" + chassisId +
                                       "/Assembly#/Assemblies/" + id;
            assemblyRes["MemberId"] = id;
            nlohmann::json& jResp = asyncResp->res.jsonValue["Assemblies"];
            // inserting the assembly object to Assemblies array
            std::string sortField = "MemberId";
            redfish::nvidia_chassis_utils::insertSorted(jResp, assemblyRes,
                                                        sortField);
            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                // Assembly OEM properties if exist, search by association
                redfish::nvidia_chassis_utils::getOemAssemblyAssert(
                    asyncResp, id, objPath);
            }
        });
}

/**
 * Assembly override class for delivering Chassis/Assembly Schema
 * Functions triggers appropriate requests on DBus
 */
inline void requestAssemblyRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId) {
                BMCWEB_LOG_DEBUG("Assembly doGet enter");
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                const std::array<std::string_view, 1> interface = {
                    "xyz.openbmc_project.Inventory.Item.Chassis"};
                // Get chassis collection
                dbus::
                    utility::getSubTree("/xyz/openbmc_project/inventory", 0,
                                        interface,
                                        [asyncResp,
                                         chassisId(std::string(chassisId))](
                                            const boost::system::error_code& ec,
                                            const dbus::utility::GetSubTreeType&
                                                subtree) {
                                            if (ec)
                                            {
                                                BMCWEB_LOG_DEBUG(
                                                    "DBUS response error");
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            // Iterate over all retrieved
                                            // ObjectPaths.
                                            for (const std::pair<
                                                     std::string,
                                                     std::vector<std::pair<
                                                         std::string,
                                                         std::vector<
                                                             std::string>>>>&
                                                     object : subtree)
                                            {
                                                const std::string& path =
                                                    object.first;
                                                const std::vector<std::pair<
                                                    std::string,
                                                    std::vector<std::string>>>&
                                                    connectionNames =
                                                        object.second;
                                                sdbusplus::message::object_path
                                                    objPath(path);
                                                if (objPath.filename() !=
                                                    chassisId)
                                                {
                                                    continue;
                                                }
                                                if (connectionNames.empty())
                                                {
                                                    std::cerr
                                                        << "Got 0 Connection names";
                                                    continue;
                                                }
                                                const std::string&
                                                    connectionName =
                                                        connectionNames[0]
                                                            .first;
                                                // Chassis assembly properties
                                                asyncResp->res
                                                    .jsonValue["@odata.type"] =
                                                    "#Assembly.v1_3_0.Assembly";
                                                asyncResp->res
                                                    .jsonValue["@odata.id"] =
                                                    "/redfish/v1/Chassis/" +
                                                    chassisId + "/Assembly";
                                                asyncResp->res.jsonValue["Id"] =
                                                    "Assembly";
                                                asyncResp->res
                                                    .jsonValue["Name"] =
                                                    "Assembly data for " +
                                                    chassisId;
                                                // Get Chassis assembly through
                                                // association
                                                dbus::utility::getProperty<std::vector<
                                                    std::string>>("xyz.openbmc_project.ObjectMapper",
                                                                  path +
                                                                      "/assembly",
                                                                  "xyz.openbmc_project.Association",
                                                                  "endpoints",
                                                                  [asyncResp,
                                                                   path,
                                                                   chassisId(std::string(
                                                                       chassisId)),
                                                                   connectionName](
                                                                      const boost::
                                                                          system::error_code&
                                                                              ec1,
                                                                      const std::
                                                                          variant<std::vector<std::string>>& resp) {
                                                                      const std::array<
                                                                          std::
                                                                              string_view,
                                                                          1>
                                                                          assemblyIntf =
                                                                              {"xyz.openbmc_project.Inventory.Item.Assembly"};
                                                                      if (ec1)
                                                                      {
                                                                          BMCWEB_LOG_DEBUG(
                                                                              "Chassis and assembly are not connected through association. ec : {}",
                                                                              ec1);

                                                                          dbus::utility::getSubTreePaths(
                                                                              path +
                                                                                  "/",
                                                                              0,
                                                                              assemblyIntf,
                                                                              [asyncResp,
                                                                               chassisId(
                                                                                   std::string(
                                                                                       chassisId)),
                                                                               connectionName](
                                                                                  const boost::
                                                                                      system::error_code&
                                                                                          ec2,
                                                                                  const std::vector<
                                                                                      std::
                                                                                          string>&
                                                                                      assemblyList) {
                                                                                  if (ec2)
                                                                                  {
                                                                                      BMCWEB_LOG_DEBUG(
                                                                                          "DBUS response error");
                                                                                      messages::internalError(
                                                                                          asyncResp
                                                                                              ->res);
                                                                                      return;
                                                                                  }
                                                                                  // Update the Assemblies
                                                                                  asyncResp
                                                                                      ->res
                                                                                      .jsonValue
                                                                                          ["Assemblies"] =
                                                                                      nlohmann::json::
                                                                                          array();
                                                                                  for (
                                                                                      const std::
                                                                                          string&
                                                                                              assembly :
                                                                                      assemblyList)
                                                                                  {
                                                                                      updateAssemblies(
                                                                                          asyncResp,
                                                                                          connectionName,
                                                                                          assembly,
                                                                                          chassisId);
                                                                                  }
                                                                              });
                                                                          return;
                                                                      }

                                                                      const std::vector<
                                                                          std::
                                                                              string>* assemblyList =
                                                                          std::get_if<
                                                                              std::vector<
                                                                                  std::
                                                                                      string>>(
                                                                              &resp);

                                                                      for (
                                                                          const std::
                                                                              string&
                                                                                  assembly :
                                                                          *assemblyList)
                                                                      {
                                                                          BMCWEB_LOG_DEBUG(
                                                                              "Found Assembly Path, {}",
                                                                              assembly);
                                                                          asyncResp
                                                                              ->res
                                                                              .jsonValue
                                                                                  ["Assemblies"] =
                                                                              nlohmann::json::
                                                                                  array();
                                                                          dbus::utility::getDbusObject(
                                                                              assembly,
                                                                              assemblyIntf,
                                                                              [asyncResp,
                                                                               assembly,
                                                                               chassisId(
                                                                                   std::string(
                                                                                       chassisId))](
                                                                                  const boost::
                                                                                      system::error_code&
                                                                                          ec2,
                                                                                  const std::vector<std::pair<
                                                                                      std::
                                                                                          string,
                                                                                      std::vector<
                                                                                          std::
                                                                                              string>>>&
                                                                                      objInfo) mutable {
                                                                                  if (ec2)
                                                                                  {
                                                                                      BMCWEB_LOG_ERROR(
                                                                                          "error_code = {}",
                                                                                          ec2);

                                                                                      messages::internalError(
                                                                                          asyncResp
                                                                                              ->res);

                                                                                      return;
                                                                                  }

                                                                                  updateAssemblies(
                                                                                      asyncResp,
                                                                                      objInfo[0]
                                                                                          .first,
                                                                                      assembly,
                                                                                      chassisId);
                                                                              });
                                                                      }
                                                                  });

                                                return;
                                            }
                                            // Couldn't find an object with that
                                            // name. return an error
                                            messages::resourceNotFound(
                                                asyncResp->res,
                                                "#Chassis.v1_15_0.Chassis",
                                                chassisId);
                                        });
            });
}

} // namespace redfish
