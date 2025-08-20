// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "http_request.hpp"
#include "nvidia_managers.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/manager_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
namespace redfish
{
constexpr std::array<const char*, 1> bmcInterfaces = {
    "xyz.openbmc_project.Inventory.Item.BMC"};

/**
 * Fill out Asset information from from given D-Bus object
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       service         D-Bus service to query.
 * @param[in]       objpath         D-Bus object to query.
 *                                  successfully finding object.
 */
inline void getBmcAssetData(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                            const std::string& service,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get BMC Asset Data");
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [objPath, asyncResp{std::move(asyncResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* name = nullptr;
            const std::string* model = nullptr;
            const std::string* manufacturer = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "Name", name,
                "Model", model, "Manufacturer", manufacturer);

            if (!success)
            {
                BMCWEB_LOG_ERROR("Unpack Error while fetching BMC Asset data");
                return;
            }

            if (name != nullptr && !name->empty())
            {
                std::string description = "Redfish Service On ";
                description += *name;
                asyncResp->res.jsonValue["Description"] = description;
            }

            if ((model != nullptr) && !model->empty())
            {
                asyncResp->res.jsonValue["Product"] = *model;
            }

            if (manufacturer != nullptr)
            {
                asyncResp->res.jsonValue["Vendor"] = *manufacturer;
            }
        });
}

inline void getBMCObject(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Get available BMC resources.");

    // GetSubTree on all interfaces which provide info about BMC
    crow::connections::systemBus->async_method_call(
        [asyncResp](
            boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                // Ignore any objects which don't end with our desired bmcid
                if (!objectPath.ends_with(BMCWEB_REDFISH_MANAGER_URI_NAME))
                {
                    continue;
                }

                bool found = false;
                // Filter out objects that don't have the BMC-specific
                // interfaces to make sure we can return 404 on non-BMC
                for (const auto& [serviceName, interfaceList] : serviceMap)
                {
                    if (std::find_first_of(
                            interfaceList.begin(), interfaceList.end(),
                            bmcInterfaces.begin(), bmcInterfaces.end()) !=
                        interfaceList.end())
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    continue;
                }

                for (const auto& [serviceName, interfaceList] : serviceMap)
                {
                    for (const auto& interface : interfaceList)
                    {
                        if (interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Asset")
                        {
                            getBmcAssetData(asyncResp, serviceName, objectPath);
                        }
                    }
                }

                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Decorator.Asset"});
}

} // namespace redfish
