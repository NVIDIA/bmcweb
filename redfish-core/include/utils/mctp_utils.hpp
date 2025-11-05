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

constexpr const std::string_view mctpBusPrefix = "au.com.codeconstruct.MCTP1";
constexpr const std::string_view mctpObjectPrefix =
    "/au/com/codeconstruct/mctp1/networks/1/endpoints/";
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
                 const AssociationCallback& callback) : spdmObj(spdmObject)
    {
        BMCWEB_LOG_DEBUG("Finding associations for {}", spdmObject);
        dbus::utility::findAssociations(
            spdmObject + "/transport_object",
            [this, spdmObject,
             callback](const boost::system::error_code& ec1,
                       const std::vector<std::string>& association) {
                BMCWEB_LOG_DEBUG("findAssociations callback for {}",
                                 spdmObject);
                if (ec1)
                {
                    BMCWEB_LOG_ERROR("{} : {}", spdmObject, ec1.message());
                    callback(false, ec1.message());
                    return;
                }
                if (association.empty())
                {
                    callback(false,
                             spdmObj + ": no SPDM / MCTP association found");
                    return;
                }
                mctpObj = association.front();
                if (mctpObj.starts_with(mctpObjectPrefix))
                {
                    std::vector<std::string> v;
                    bmcweb::split(v, mctpObj, '/');
                    if (v.empty())
                    {
                        callback(false, "invalid MCTP object path: " + mctpObj);
                        return;
                    }
                    try
                    {
                        mctpEid = std::stoi(v.back());
                        getDbusMctpProperties(callback);
                    }
                    catch (const std::invalid_argument& e)
                    {
                        BMCWEB_LOG_ERROR(
                            "Invalid MCTP object path: {} and error: {}",
                            mctpObj, e.what());
                        callback(false, "Invalid MCTP object path: " + mctpObj);
                        return;
                    }
                    catch (const std::exception& e)
                    {
                        BMCWEB_LOG_ERROR(
                            "Unexpected error parsing MCTP object path: {} and error: {}",
                            mctpObj, e.what());
                        callback(false, "Invalid MCTP object path: " + mctpObj);
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
        return connectivity.has_value() ? (*connectivity == "Available")
                                        : false;
    }

  protected:
    void getDbusMctpProperties(const AssociationCallback& callback)
    {
        dbus::utility::async_method_call(
            [this, callback](const boost::system::error_code& ec,
                             const GetObjectType& response) {
                if (ec || response.empty())
                {
                    callback(false, "GetObject failure for " + mctpObj);
                    return;
                }
                for (const auto& elem : response)
                {
                    const std::string& service = elem.first;
                    if (!service.starts_with(mctpBusPrefix))
                    {
                        continue;
                    }
                    dbus::utility::getAllProperties(
                        service, mctpObj, "",
                        [this, callback](const boost::system::error_code& ec2,
                                         const dbus::utility::DBusPropertiesMap&
                                             properties) {
                            if (ec2)
                            {
                                callback(false,
                                         "Failed to get properties for " +
                                             mctpObj);
                                return;
                            }
                            for (const auto& [key, val] : properties)
                            {
                                if (key == "Connectivity")
                                {
                                    if (const std::string* value =
                                            std::get_if<std::string>(&val))
                                    {
                                        connectivity = *value;
                                    }
                                    else
                                    {
                                        callback(
                                            false,
                                            "Connectivity property failure for " +
                                                mctpObj);
                                        return;
                                    }
                                }
                                else if (key == "SupportedMessageTypes")
                                {
                                    if (const std::vector<uint8_t>* value =
                                            std::get_if<std::vector<uint8_t>>(
                                                &val))
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
                                if (connectivity.has_value() &&
                                    mctpMessageTypes.has_value())
                                {
                                    callback(true, mctpObj);
                                    return;
                                }
                            }
                            callback(false, "GetAll properties failure for " +
                                                mctpObj);
                        });
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
    std::optional<std::string> connectivity;
    std::optional<std::vector<uint8_t>> mctpMessageTypes;
};

using Endpoints = std::vector<MctpEndpoint>;
using EndpointCallback = std::function<void(const std::shared_ptr<Endpoints>&)>;
using ErrorCallback = std::function<void(
    bool /* is critical (end of operation) */,
    const std::string& /* resource / procedure associated with the error */,
    const std::string& /* error message*/)>;

inline void enumerateMctpEndpoints(
    EndpointCallback&& endpointCallback, ErrorCallback&& errorCallback,
    const std::vector<std::string>& spdmObjectFilter =
        std::vector<std::string>(),
    uint64_t timeoutUs = 0)
{
    dbus::utility::async_method_call_timed(
        [endpointCallback{std::move(endpointCallback)},
         errorCallback{std::move(errorCallback)},
         spdmObjectFilter](const boost::system::error_code& ec,
                           const dbus::utility::GetSubTreeType& subtree) {
            const std::string desc = "SPDM / MCTP endpoint enumeration";
            BMCWEB_LOG_DEBUG("{}", desc);
            if (ec)
            {
                BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                errorCallback(true, desc, ec.message());
                return;
            }
            if (subtree.empty())
            {
                errorCallback(true, desc, "no SPDM objects found");
                return;
            }
            auto endpoints = std::make_shared<Endpoints>();
            endpoints->reserve(subtree.size());
            std::shared_ptr<size_t> processedEndpoints =
                std::make_shared<size_t>(0);
            for (const auto& object : subtree)
            {
                if (!spdmObjectFilter.empty())
                {
                    bool match = false;
                    const auto& name =
                        sdbusplus::message::object_path(object.first)
                            .filename();
                    for (const auto& f : spdmObjectFilter)
                    {
                        if (name.find(f) != std::string::npos)
                        {
                            match = true;
                            break;
                        }
                    }
                    if (!match)
                    {
                        *processedEndpoints += 1;
                        continue;
                    }
                }
                endpoints->emplace_back(
                    object.first,
                    [desc, endpoints, processedEndpoints, endpointCallback,
                     errorCallback](bool success, const std::string& msg) {
                        if (!success)
                        {
                            errorCallback(false, desc, msg);
                        }
                        *processedEndpoints += 1;
                        if (*processedEndpoints == endpoints->capacity())
                        {
                            std::sort(endpoints->begin(), endpoints->end(),
                                      [](const MctpEndpoint& a,
                                         const MctpEndpoint& b) {
                                          return a.getMctpEid() <
                                                 b.getMctpEid();
                                      });
                            endpointCallback(endpoints);
                        }
                    });
            }
        },
        dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
        dbus_utils::mapperIntf, "GetSubTree", timeoutUs,
        "/xyz/openbmc_project/SPDM", 0,
        std::array<const char*, 1>{spdmResponderIntf});
}

inline void enumerateMctpEndpoints(
    EndpointCallback&& endpointCallback, ErrorCallback&& errorCallback,
    const std::string& spdmObjectFilter = "", uint64_t timeoutUs = 0)
{
    std::vector<std::string> filterVector;
    if (!spdmObjectFilter.empty())
    {
        filterVector.emplace_back(spdmObjectFilter);
    }
    enumerateMctpEndpoints(std::move(endpointCallback),
                           std::move(errorCallback), filterVector, timeoutUs);
}

} // namespace mctp_utils

} // namespace redfish
