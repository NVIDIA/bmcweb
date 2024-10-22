/*
// Copyright (c) 2019 Intel Corporation
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
#pragma once

// #include <dbus_utility.hpp>
// #include <error_messages.hpp>
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"

#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message.hpp>

#include <array>
#include <charconv>
#include <ranges>
#include <string_view>

namespace redfish
{

enum NetworkProtocolUnitStructFields
{
    NET_PROTO_UNIT_NAME,
    NET_PROTO_UNIT_DESC,
    NET_PROTO_UNIT_LOAD_STATE,
    NET_PROTO_UNIT_ACTIVE_STATE,
    NET_PROTO_UNIT_SUB_STATE,
    NET_PROTO_UNIT_DEVICE,
    NET_PROTO_UNIT_OBJ_PATH,
    NET_PROTO_UNIT_ALWAYS_0,
    NET_PROTO_UNIT_ALWAYS_EMPTY,
    NET_PROTO_UNIT_ALWAYS_ROOT_PATH
};

enum NetworkProtocolListenResponseElements
{
    NET_PROTO_LISTEN_TYPE,
    NET_PROTO_LISTEN_STREAM
};

/**
 * @brief D-Bus Unit structure returned in array from ListUnits Method
 */
using UnitStruct =
    std::tuple<std::string, std::string, std::string, std::string, std::string,
               std::string, sdbusplus::message::object_path, uint32_t,
               std::string, sdbusplus::message::object_path>;

using GetObjectType =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

template <typename CallbackFunc>
void getMainChassisId(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                      CallbackFunc&& callback)
{
    // Find managed chassis
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [callback = std::forward<CallbackFunc>(callback),
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("{}", ec);
            return;
        }
        if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG("Can't find chassis!");
            return;
        }

        std::size_t idPos = subtree[0].first.rfind('/');
        if (idPos == std::string::npos ||
            (idPos + 1) >= subtree[0].first.size())
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_DEBUG("Can't parse chassis ID!");
            return;
        }
        std::string chassisId = subtree[0].first.substr(idPos + 1);
        BMCWEB_LOG_DEBUG("chassisId = {}", chassisId);
        callback(chassisId, asyncResp);
    });
}

template <typename CallbackFunc>
void getPortStatusAndPath(
    std::span<const std::pair<std::string_view, std::string_view>>
        protocolToDBus,
    CallbackFunc&& callback)
{
    crow::connections::systemBus->async_method_call(
        [protocolToDBus, callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const std::vector<UnitStruct>& r) {
        std::vector<std::tuple<std::string, std::string, bool>> socketData;
        if (ec)
        {
            BMCWEB_LOG_ERROR("{}", ec);
            // return error code
            callback(ec, socketData);
            return;
        }

        // save all service output into vector
        for (const UnitStruct& unit : r)
        {
            // Only traverse through <xyz>.socket units
            const std::string& unitName = std::get<NET_PROTO_UNIT_NAME>(unit);

            // find "." into unitsName
            size_t lastCharPos = unitName.rfind('.');
            if (lastCharPos == std::string::npos)
            {
                continue;
            }

            // is unitsName end with ".socket"
            std::string unitNameEnd = unitName.substr(lastCharPos);
            if (unitNameEnd != ".socket")
            {
                continue;
            }

            // find "@" into unitsName
            if (size_t atCharPos = unitName.rfind('@');
                atCharPos != std::string::npos)
            {
                lastCharPos = atCharPos;
            }

            // unitsName without "@eth(x).socket", only <xyz>
            // unitsName without ".socket", only <xyz>
            std::string unitNameStr = unitName.substr(0, lastCharPos);

            for (const auto& kv : protocolToDBus)
            {
                // We are interested in services, which starts with
                // mapped service name
                if (unitNameStr != kv.second)
                {
                    continue;
                }

                const std::string_view protocolName = kv.first;
                // if TLS authentication is disabled then don't support HTTPS.
                // even if SSL is enabled
#ifdef BMCWEB_ENABLE_SSL
                if (protocolName == "HTTPS" &&
                    !persistent_data::getConfig().isTLSAuthEnabled())
                {
                    continue;
                }
#else
                if (protocolName == "HTTPS")
                {
                    continue;
                }
#endif
                if (protocolName == "HTTP" &&
                    persistent_data::getConfig().isTLSAuthEnabled())
                {
                    continue;
                }
                const std::string& socketPath =
                    std::get<NET_PROTO_UNIT_OBJ_PATH>(unit);
                const std::string& unitState =
                    std::get<NET_PROTO_UNIT_SUB_STATE>(unit);

                bool isProtocolEnabled = ((unitState == "running") ||
                                          (unitState == "listening"));

                // Some protocols may have multiple services associated with
                // them (for example IPMI). Look to see if we've already added
                // an entry for the current protocol.
                auto find = std::ranges::find_if(
                    socketData,
                    [&kv](const std::tuple<std::string, std::string, bool>& i) {
                    return std::get<1>(i) == kv.first;
                });
                if (find != socketData.end())
                {
                    // It only takes one enabled systemd service to consider a
                    // protocol enabled so if the current entry already has it
                    // enabled (or the new one is disabled) then just continue,
                    // otherwise remove the current one and add this new one.
                    if (std::get<2>(*find) || !isProtocolEnabled)
                    {
                        // Already registered as enabled or current one is not
                        // enabled, nothing to do
                        BMCWEB_LOG_DEBUG(
                            "protocolName: {}, already true or current one is false: {}",
                            kv.first, isProtocolEnabled);
                        break;
                    }
                    // Remove existing entry and replace with new one (below)
                    socketData.erase(find);
                }

                socketData.emplace_back(socketPath, std::string(kv.first),
                                        isProtocolEnabled);
                // We found service, return from inner loop.
                break;
            }
        }

        callback(ec, socketData);
    },
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "ListUnits");
}

template <typename CallbackFunc>
void getPortNumber(const std::string& socketPath, CallbackFunc&& callback)
{
    sdbusplus::asio::getProperty<
        std::vector<std::tuple<std::string, std::string>>>(
        *crow::connections::systemBus, "org.freedesktop.systemd1", socketPath,
        "org.freedesktop.systemd1.Socket", "Listen",
        [callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const std::vector<std::tuple<std::string, std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("{}", ec);
            callback(ec, 0);
            return;
        }
        if (resp.empty())
        {
            // Network Protocol Listen Response Elements is empty
            boost::system::error_code ec1 =
                boost::system::errc::make_error_code(
                    boost::system::errc::bad_message);
            // return error code
            callback(ec1, 0);
            BMCWEB_LOG_ERROR("{}", ec1);
            return;
        }
        const std::string& listenStream =
            std::get<NET_PROTO_LISTEN_STREAM>(resp[0]);
        const char* pa = &listenStream[listenStream.rfind(':') + 1];
        int port{0};
        if (auto [p, ec2] = std::from_chars(pa, nullptr, port);
            ec2 != std::errc())
        {
            // there is only two possibility invalid_argument and
            // result_out_of_range
            boost::system::error_code ec3 =
                boost::system::errc::make_error_code(
                    boost::system::errc::invalid_argument);
            if (ec2 == std::errc::result_out_of_range)
            {
                ec3 = boost::system::errc::make_error_code(
                    boost::system::errc::result_out_of_range);
            }
            // return error code
            callback(ec3, 0);
            BMCWEB_LOG_ERROR("{}", ec3);
        }
        callback(ec, port);
    });
}

inline void
    handleAccountLocked(const std::string_view username,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const crow::Request& req)
{
    const std::string user(username);
    crow::connections::systemBus->async_method_call(
        [asyncResp, &req,
         user](const boost::system::error_code& ec,
               const std::map<std::string, dbus::utility::DbusVariantType>&
                   userInfo) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("GetUserInfo failed...");
            messages::resourceAtUriUnauthorized(asyncResp->res, req.url(),
                                                "Invalid username or password");
            return;
        }
        bool userLocked = false;
        const bool* userLockedPtr = nullptr;
        auto lockedIter = userInfo.find("UserLockedForFailedAttempt");
        if (lockedIter != userInfo.end())
        {
            userLockedPtr = std::get_if<bool>(&lockedIter->second);
        }
        if (userLockedPtr != nullptr)
        {
            userLocked = *userLockedPtr;
        }

        if (userLocked)
        {
            sdbusplus::asio::getProperty<uint32_t>(
                *crow::connections::systemBus,
                "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
                "xyz.openbmc_project.User.AccountPolicy",
                "AccountUnlockTimeout",
                [asyncResp, &req](const boost::system::error_code ec,
                                  const uint32_t& unlockTimeout) {
                if (ec)
                {
                    messages::internalError(asyncResp->res);
                }
                else
                {
                    BMCWEB_LOG_DEBUG("unlock Timeout: {}", unlockTimeout);
                    std::string arg2 =
                        "Account temporarily locked out for " +
                        std::to_string(unlockTimeout) +
                        " seconds"
                        " due to multiple authentication failures";
                    messages::resourceAtUriUnauthorized(asyncResp->res,
                                                        req.url(), arg2);
                }
            });
        }
        else
        {
            BMCWEB_LOG_DEBUG("User is not locked out");
            messages::resourceAtUriUnauthorized(asyncResp->res, req.url(),
                                                "Invalid username or password");
        }
    },
        "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
        "xyz.openbmc_project.User.Manager", "GetUserInfo", user);
}

/**
 * @brief Fill out firmware version of component
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       objectPath  D-Bus object to query.
 */
inline void
    getComponentFirmwareVersion(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                                const std::string& objectPath)
{
    const std::string serviceObjectMapper = "xyz.openbmc_project.ObjectMapper";

    sdbusplus::asio::getProperty<std::vector<std::string>>(
        *crow::connections::systemBus, serviceObjectMapper,
        objectPath + "/parent_chassis", "xyz.openbmc_project.Association",
        "endpoints",
        [serviceObjectMapper,
         asyncResp](const boost::system::error_code ec,
                    const std::vector<std::string>& objPaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "getComponentFirmwareVersion getProperty parent_chassis DBUS error");
            BMCWEB_LOG_ERROR("error_code = ", ec);
            BMCWEB_LOG_ERROR("error msg = ", ec.message());

            return;
        }

        if (!objPaths.empty())
        {
            const std::string& firstElement = objPaths.front();

            sdbusplus::asio::getProperty<std::vector<std::string>>(
                *crow::connections::systemBus, serviceObjectMapper,
                firstElement + "/activation", "xyz.openbmc_project.Association",
                "endpoints",
                [asyncResp](const boost::system::error_code ec,
                            const std::vector<std::string>& objPaths) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "getComponentFirmwareVersion getProperty activation DBUS error");
                    BMCWEB_LOG_ERROR("error_code = ", ec);
                    BMCWEB_LOG_ERROR("error msg = ", ec.message());

                    return;
                }

                if (!objPaths.empty())
                {
                    const std::string& firstElement = objPaths.front();

                    crow::connections::systemBus->async_method_call(
                        [asyncResp,
                         firstElement](const boost::system::error_code ec,
                                       const GetObjectType& resp) {
                        std::string url;
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "getComponentFirmwareVersion async_method_call GetObject DBUS error");
                            BMCWEB_LOG_ERROR("error_code = ", ec);
                            BMCWEB_LOG_ERROR("error msg = ", ec.message());

                            return;
                        }

                        std::string softwareVersionInterface =
                            "xyz.openbmc_project.Software.Version";
                        std::string serviceObjectSoftware;

                        for (const auto& serObj : resp)
                        {
                            auto interfaces = serObj.second;
                            for (const auto& interface : interfaces)
                            {
                                if (interface == softwareVersionInterface)
                                {
                                    serviceObjectSoftware = serObj.first;
                                    break;
                                }
                            }

                            if (!serviceObjectSoftware.empty())
                            {
                                break;
                            }
                        }

                        if (!serviceObjectSoftware.empty())
                        {
                            sdbusplus::asio::getProperty<std::string>(
                                *crow::connections::systemBus,
                                serviceObjectSoftware, firstElement,
                                softwareVersionInterface, "Version",
                                [asyncResp](const boost::system::error_code ec,
                                            const std::string& property) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "getComponentFirmwareVersion getProperty Version DBUS error");
                                    BMCWEB_LOG_ERROR("error_code = ", ec);
                                    BMCWEB_LOG_ERROR("error msg = ",
                                                     ec.message());

                                    return;
                                }
                                asyncResp->res.jsonValue["FirmwareVersion"] =
                                    property;
                            });
                        }
                    },
                        "xyz.openbmc_project.ObjectMapper",
                        "/xyz/openbmc_project/object_mapper",
                        "xyz.openbmc_project.ObjectMapper", "GetObject",
                        firstElement, std::array<const char*, 0>());

                    return;
                }

                BMCWEB_LOG_ERROR(
                    "Could not find property endpoints in activation element");
            });

            return;
        }

        BMCWEB_LOG_ERROR(
            "Could not find property endpoints in parent_chassis element");
    });
}

} // namespace redfish
