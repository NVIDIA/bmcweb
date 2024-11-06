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

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <openbmc_dbus_rest.hpp>
#include <utils/dbus_utils.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace redfish
{

namespace mctp_utils
{

constexpr const std::string_view mctpBusPrefix = "xyz.openbmc_project.MCTP";
constexpr const std::string_view mctpObjectPrefix =
    "/xyz/openbmc_project/mctp/";
constexpr const uint8_t mctpMessageTypeVdm = 127;

constexpr const char* spdmResponderIntf = "xyz.openbmc_project.SPDM.Responder";

using AssociationCallback = std::function<void(
    bool /* success */,
    const std::string& /* MCTP object name OR error message */)>;
using GetObjectType =
    std::vector<std::pair<std::string, std::vector<std::string>>>;
class MctpEndpoint
{
  public:
    MctpEndpoint(const std::string& spdmObject,
                 const AssociationCallback& callback) :
        spdmObj(spdmObject)
    {
        BMCWEB_LOG_DEBUG("Finding associations for {}", spdmObject);
        dbus::utility::findAssociations(
            spdmObject + "/transport_object",
            [this, spdmObject,
             callback](const boost::system::error_code ec,
                       std::variant<std::vector<std::string>>& association) {
            BMCWEB_LOG_DEBUG("findAssociations callback for {}", spdmObject);
            if (ec)
            {
                BMCWEB_LOG_ERROR("{} : {}", spdmObject, ec.message());
                callback(false, ec.message());
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&association);
            if (data == nullptr || data->empty())
            {
                callback(false, spdmObj + ": no SPDM / MCTP association found");
                return;
            }
            mctpObj = data->front();
            if (mctpObj.rfind(mctpObjectPrefix, 0) == 0)
            {
                std::vector<std::string> v;
                boost::split(v, mctpObj, boost::is_any_of("/"));
                if (v.size() == 0)
                {
                    callback(false, "invalid MCTP object path: " + mctpObj);
                    return;
                }
                try
                {
                    mctpEid = std::stoi(v.back());
                    getDbusMctpProperties(callback);
                }
                catch (const std::invalid_argument&)
                {
                    callback(false, "invalid MCTP object path: " + mctpObj);
                }
                return;
            }
            callback(false, "invalid MCTP object path: " + mctpObj);
        });
    }

    int getMctpEid() const
    {
        return mctpEid;
    }

    const std::string& getMctpObject() const
    {
        return mctpObj;
    }

    std::vector<uint8_t> getMctpMessageTypes() const
    {
        return mctpMessageTypes.has_value() ? *mctpMessageTypes
                                            : std::vector<uint8_t>();
    }

    const std::string& getSpdmObject() const
    {
        return spdmObj;
    }

    bool isEnabled() const
    {
        return enabled.has_value() ? *enabled : false;
    }

  protected:
    void getDbusMctpProperties(const AssociationCallback& callback)
    {
        crow::connections::systemBus->async_method_call(
            [this, callback](const boost::system::error_code ec,
                             const GetObjectType& response) {
            if (ec || response.empty())
            {
                callback(false, "GetObject failure for " + mctpObj);
                return;
            }
            for (const auto& elem : response)
            {
                const std::string& service = elem.first;
                if (service.rfind(mctpBusPrefix, 0) != 0)
                {
                    continue;
                }
                crow::connections::systemBus->async_method_call(
                    [this,
                     callback](const boost::system::error_code ec,
                               const boost::container::flat_map<
                                   std::string, dbus::utility::DbusVariantType>&
                                   properties) {
                    if (ec)
                    {
                        callback(false,
                                 "Failed to get properties for " + mctpObj);
                        return;
                    }
                    for (const auto& [key, val] : properties)
                    {
                        if (key == "Enabled")
                        {
                            if (const bool* value = std::get_if<bool>(&val))
                            {
                                enabled = *value;
                            }
                            else
                            {
                                callback(false,
                                         "Enabled property failure for " +
                                             mctpObj);
                                return;
                            }
                        }
                        else if (key == "SupportedMessageTypes")
                        {
                            if (const std::vector<uint8_t>* value =
                                    std::get_if<std::vector<uint8_t>>(&val))
                            {
                                mctpMessageTypes = *value;
                            }
                            else
                            {
                                callback(
                                    false,
                                    "SupportedMessageTypes property failure for " +
                                        mctpObj);
                                return;
                            }
                        }
                        if (enabled.has_value() && mctpMessageTypes.has_value())
                        {
                            callback(true, mctpObj);
                            return;
                        }
                    }
                    callback(false, "GetAll properties failure for " + mctpObj);
                },
                    service, mctpObj, "org.freedesktop.DBus.Properties",
                    "GetAll", "");
                return;
            }
            callback(false, "GetObject failure for: " + mctpObj);
            return;
        },
            dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
            dbus_utils::mapperIntf, "GetObject", mctpObj,
            std::array<const char*, 0>());
    }

    std::string mctpObj;
    std::string spdmObj;
    int mctpEid{-1};
    std::optional<bool> enabled;
    std::optional<std::vector<uint8_t>> mctpMessageTypes;
};

using Endpoints = std::vector<MctpEndpoint>;
using EndpointCallback = std::function<void(const std::shared_ptr<Endpoints>&)>;
using ErrorCallback = std::function<void(
    bool /* is critical (end of operation) */,
    const std::string& /* resource / procedure associated with the error */,
    const std::string& /* error message*/)>;
inline void enumerateMctpEndpoints(EndpointCallback&& endpointCallback,
                                   ErrorCallback&& errorCallback,
                                   const std::string& spdmObjectFilter = "",
                                   uint64_t timeoutUs = 0)
{
    crow::connections::systemBus->async_method_call_timed(
        [endpointCallback{std::forward<EndpointCallback>(endpointCallback)},
         errorCallback{std::forward<ErrorCallback>(errorCallback)},
         spdmObjectFilter](
            const boost::system::error_code ec,
            const crow::openbmc_mapper::GetSubTreeType& subtree) {
        const std::string desc = "SPDM / MCTP endpoint enumeration";
        BMCWEB_LOG_DEBUG("{}", desc);
        if (ec)
        {
            BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
            errorCallback(true, desc, ec.message());
            return;
        }
        if (subtree.size() == 0)
        {
            errorCallback(true, desc, "no SPDM objects found");
            return;
        }
        if (!spdmObjectFilter.empty())
        {
            auto obj = std::find_if(subtree.begin(), subtree.end(),
                                    [spdmObjectFilter](const auto& object) {
                const auto& name =
                    sdbusplus::message::object_path(object.first).filename();
                return spdmObjectFilter.compare(name) == 0;
            });
            if (obj != subtree.end())
            {
                auto endpoints = std::make_shared<Endpoints>();
                endpoints->reserve(1);
                endpoints->emplace_back(
                    obj->first,
                    [desc, endpoints, endpointCallback,
                     errorCallback](bool success, const std::string& msg) {
                    if (!success)
                    {
                        errorCallback(true, desc, msg);
                    }
                    else
                    {
                        endpointCallback(endpoints);
                    }
                });
                return;
            }
            errorCallback(true, desc,
                          "no SPDM objects matching " + spdmObjectFilter +
                              " found");
            return;
        }

        auto endpoints = std::make_shared<Endpoints>();
        endpoints->reserve(subtree.size());
        std::shared_ptr<size_t> enumeratedEndpoints =
            std::make_shared<size_t>(0);
        for (const auto& object : subtree)
        {
            endpoints->emplace_back(
                object.first,
                [desc, endpoints, enumeratedEndpoints, endpointCallback,
                 errorCallback](bool success, const std::string& msg) {
                if (!success)
                {
                    errorCallback(false, desc, msg);
                }
                *enumeratedEndpoints += 1;
                if (*enumeratedEndpoints == endpoints->capacity())
                {
                    std::sort(endpoints->begin(), endpoints->end(),
                              [](const MctpEndpoint& a, const MctpEndpoint& b) {
                        return a.getMctpEid() < b.getMctpEid();
                    });
                    endpointCallback(endpoints);
                }
            });
        }
    },
        dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
        dbus_utils::mapperIntf, "GetSubTree", timeoutUs,
        "/xyz/openbmc_project/SPDM", 0, std::array{spdmResponderIntf});
}

} // namespace mctp_utils

} // namespace redfish
