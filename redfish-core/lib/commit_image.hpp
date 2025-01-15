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

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

using Priority = int;
using MctpMedium = std::string;
using MctpBinding = std::string;
using UUID = std::string;
using EID = uint8_t;
using URI = std::string;

static std::unordered_map<MctpMedium, Priority> mediumPriority = {
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.PCIe", 0},
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.USB", 1},
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.SPI", 2},
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.KCS", 3},
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.Serial", 4},
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.SMBus", 5}};

static std::unordered_map<MctpBinding, Priority> bindingPriority = {
    {"xyz.openbmc_project.MCTP.Binding.BindingTypes.PCIe", 0},
    {"xyz.openbmc_project.MCTP.Binding.BindingTypes.USB", 1},
    {"xyz.openbmc_project.MCTP.Binding.BindingTypes.SPI", 2},
    {"xyz.openbmc_project.MCTP.Binding.BindingTypes.KCS", 3},
    {"xyz.openbmc_project.MCTP.Binding.BindingTypes.Serial", 4},
    {"xyz.openbmc_project.MCTP.Binding.BindingTypes.SMBus", 5}};

constexpr const char* mctpObjectPath = "/xyz/openbmc_project/mctp";
constexpr const char* mctpStateServiceReadyIntf =
    "xyz.openbmc_project.State.ServiceReady";
constexpr const char* mctpCommonUUIDIntf = "xyz.openbmc_project.Common.UUID";
constexpr const char* mctpMCTPEndpointIntf =
    "xyz.openbmc_project.MCTP.Endpoint";
constexpr const char* mctpMCTPBindingIntf = "xyz.openbmc_project.MCTP.Binding";

using UuidToEidMap =
    std::unordered_map<UUID, std::tuple<EID, Priority, Priority>>;
using UuidToUriMap = std::unordered_map<UUID, URI>;
using ResultCallback = std::function<void(UUID, EID, URI)>;
using ErrorCallback = std::function<void(const std::string, const std::string)>;

struct CommitImageValueEntry
{
  public:
    URI inventoryUri;
    UUID uuid;

    friend bool operator==(const CommitImageValueEntry& c1,
                           const std::string& c2)
    {
        return c1.inventoryUri == c2;
    }
};

inline std::vector<CommitImageValueEntry> getAllowableValues()
{
    static std::vector<CommitImageValueEntry> allowableValues;

    if (allowableValues.empty() == false)
    {
        return allowableValues;
    }

    std::string configPath(BMCWEB_FW_MCTP_MAPPING_JSON);

    if (!fs::exists(configPath))
    {
        BMCWEB_LOG_ERROR("The file doesn't exist: {}", configPath);
        return allowableValues;
    }

    std::ifstream jsonFile(configPath);
    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        BMCWEB_LOG_ERROR("Unable to parse json data {}", configPath);
        return allowableValues;
    }

    const Json emptyJson{};

    auto entries = data.value("FwUuidMap", emptyJson);
    for (const auto& element : entries.items())
    {
        try
        {
            CommitImageValueEntry allowableVal;

            allowableVal.inventoryUri = element.key();
            allowableVal.uuid = static_cast<std::string>(element.value());

            allowableValues.push_back(allowableVal);
        }
        catch ([[maybe_unused]] const std::exception& e)
        {
            BMCWEB_LOG_ERROR("FW UUID map format error.");
        }
    }
    return allowableValues;
}

/**
 * @brief Updates the UUID to EID mapping based on priority and binding rules.
 *
 * This function updates the provided `uuidToEidMap` by comparing the current
 * UUID's EID and MediumType priority with any previously stored values.
 * It ensures that the EID with the highest priority (lower value) is retained
 * in the map, based on both MediumType and binding priorities.
 *
 * @param[in] uuid The UUID being processed.
 * @param[in] eid The EID corresponding to the UUID.
 * @param[in] mediumType The MediumType property associated with the UUID.
 * @param[in] bindingType The BindingType property associated with the UUID.
 * @param[in,out] uuidToEidMap A map that stores the UUID-to-EID mappings along
 * with their priority.
 */
inline void updateUuidToEidMapping(
    const UUID& uuid, EID eid, const std::string& mediumType,
    const std::string& bindingType, UuidToEidMap& uuidToEidMap)
{
    auto mediumPriorityIter = mediumPriority.find(mediumType);
    Priority currentMediumPriority =
        (mediumPriorityIter != mediumPriority.end())
            ? mediumPriorityIter->second
            : std::numeric_limits<int>::max();

    auto bindingPriorityIter = bindingPriority.find(bindingType);
    Priority currentBindingPriority =
        (bindingPriorityIter != bindingPriority.end())
            ? bindingPriorityIter->second
            : std::numeric_limits<int>::max();

    auto eidIter = uuidToEidMap.find(uuid);
    if (eidIter != uuidToEidMap.end())
    {
        EID prevEid;
        Priority prevMediumPriority;
        Priority prevBindingPriority;
        std::tie(prevEid, prevMediumPriority, prevBindingPriority) =
            eidIter->second;
        if (currentMediumPriority < prevMediumPriority)
        {
            uuidToEidMap[uuid] = std::make_tuple(eid, currentMediumPriority,
                                                 currentBindingPriority);
        }
        else if (currentMediumPriority == prevMediumPriority)
        {
            if (currentBindingPriority < prevBindingPriority)
            {
                uuidToEidMap[uuid] = std::make_tuple(eid, currentMediumPriority,
                                                     currentBindingPriority);
            }
        }
    }
    else
    {
        uuidToEidMap[uuid] =
            std::make_tuple(eid, currentMediumPriority, currentBindingPriority);
    }
}

/**
 * @brief Retrieves the EID for a UUID from MCTP services.
 *
 * This function retrieves the mapping between UUIDs and EIDs from MCTP services
 * and invokes a callback function with the corresponding UUID, EID, and URI for
 * each mapped service. The function first fetches all services that provide the
 * MCTP service properties, then for each service, retrieves and analyzes the
 * relevant MCTP UUID, EID, and MediumType properties. The callback function is
 * executed for each UUID that has a corresponding URI in the uuidToUriMap.
 *
 * @param[in] serviceNames A vector of service names to be processed for
 * retrieving MCTP service properties.
 * @param[in] uuidToUriMap A map containing UUID-to-URI mappings for MCTP
 * services.
 * @param[in] resultCallback A callback function that is called with the UUID,
 * EID, and URI after processing each service.
 * @param[in] errorCallback A callback function that is invoked when an error
 * occurs.
 * @param[in] uuidToEidMap (Optional) A map storing the current UUID-to-EID
 * mappings along with priority, updated as services are analyzed.
 *
 * @return
 */
inline void retrieveEidFromMctpServiceProperties(
    const std::vector<std::string>& serviceNames,
    const std::unordered_map<UUID, URI>& uuidToUriMap,
    const ResultCallback& resultCallback, const ErrorCallback& errorCallback,
    UuidToEidMap uuidToEidMap = {})
{
    std::string currentServiceName = serviceNames.front();
    const std::vector<std::string> remainingServices(serviceNames.begin() + 1,
                                                     serviceNames.end());
    sdbusplus::message::object_path path(mctpObjectPath);
    dbus::utility::getManagedObjects(
        currentServiceName, path,
        [remainingServices, uuidToUriMap, resultCallback, errorCallback,
         uuidToEidMap](const boost::system::error_code& ec,
                       const dbus::utility::ManagedObjectType& objects) {
            if (ec)
            {
                const std::string errorDesc =
                    "failed to retrieveEidFromMctpServiceProperties";
                errorCallback(errorDesc, ec.message());

                return;
            }

            std::optional<UUID> uuid;
            std::optional<EID> eid;
            std::optional<std::string> mediumType;
            std::optional<std::string> bindingType;

            UuidToEidMap updatedUuidToEidMap = uuidToEidMap;

            for (const auto& [objectPath, interfaces] : objects)
            {
                for (const auto& [interfaceName, properties] : interfaces)
                {
                    if (interfaceName == mctpCommonUUIDIntf)
                    {
                        for (const auto& propertyMap : properties)
                        {
                            if (propertyMap.first == "UUID")
                            {
                                if (const std::string* uuidPtr =
                                        std::get_if<std::string>(
                                            &propertyMap.second))
                                {
                                    uuid = *uuidPtr;
                                }
                            }
                        }
                    }
                    if (interfaceName == mctpMCTPEndpointIntf)
                    {
                        for (const auto& propertyMap : properties)
                        {
                            if (propertyMap.first == "EID")
                            {
                                if (const uint32_t* eidPtr =
                                        std::get_if<uint32_t>(
                                            &propertyMap.second))
                                {
                                    eid = *eidPtr;
                                }
                            }
                            if (propertyMap.first == "MediumType")
                            {
                                if (const std::string* mediumTypePtr =
                                        std::get_if<std::string>(
                                            &propertyMap.second))
                                {
                                    mediumType = *mediumTypePtr;
                                }
                            }
                        }
                    }
                    if (interfaceName == mctpMCTPBindingIntf)
                    {
                        for (const auto& propertyMap : properties)
                        {
                            if (propertyMap.first == "BindingType")
                            {
                                if (const std::string* bindingTypePtr =
                                        std::get_if<std::string>(
                                            &propertyMap.second))
                                {
                                    bindingType = *bindingTypePtr;
                                }
                            }
                        }
                    }
                }

                if (uuid && eid && mediumType && bindingType)
                {
                    updateUuidToEidMapping(*uuid, *eid, *mediumType,
                                           *bindingType, updatedUuidToEidMap);
                }
            }

            if (!remainingServices.empty())
            {
                retrieveEidFromMctpServiceProperties(
                    remainingServices, uuidToUriMap, resultCallback,
                    errorCallback, updatedUuidToEidMap);
            }
            else
            {
                // When 'remainingServices' is empty, it means that all MCTP
                // services have been analyzed. The 'updatedUuidToEidMap'
                // contains the mapping between EIDs and UUIDs from all MCTP
                // services. The final step is to check if the UUID from
                // 'uuidToUriMap' is present in 'updatedUuidToEidMap', and then
                // call the resultCallback function with the appropriate
                // parameters.

                // The expectation is that the UUID-to-EID mapping is not empty.
                // If the collection is empty, it indicates that it was not
                // possible to find a mapping between UUIDs and EIDs
                if (updatedUuidToEidMap.empty())
                {
                    const std::string errorDesc =
                        "failed to retrieveEidFromMctpServiceProperties";
                    const std::string errorMsg =
                        "couldn't find mapping between UUIDs and EIDs";
                    errorCallback(errorDesc, errorMsg);
                    return;
                }

                for (const auto& [uuid, eidTuple] : updatedUuidToEidMap)
                {
                    EID eid;
                    Priority mediumPriority;
                    Priority bindingPriority;
                    std::tie(eid, mediumPriority, bindingPriority) = eidTuple;
                    auto uriIter = uuidToUriMap.find(uuid);
                    if (uriIter != uuidToUriMap.end())
                    {
                        resultCallback(uuid, eid, uriIter->second);
                    }
                }
            }
        });
}

/**
 * @brief Retrieves EID mapping for all available MCTP services.
 *
 * This function queries the D-Bus subtree for MCTP services and retrieves the
 * mapping between UUIDs and EIDs. It calls the
 * retrieveEidFromMctpServiceProperties function for each service and eventually
 * invokes the callback function for every UUID that is found in the
 * uuidToUriMap with its corresponding EID and URI.
 *
 * @param[in] uuidToUriMap A map containing UUID-to-URI mappings for MCTP
 * services.
 * @param[in] resultCallback A callback function that is called with the UUID,
 * EID, and URI after processing each service.
 * @param[in] errorCallback A callback function that is invoked when an error
 * occurs.
 *
 * @return
 */
inline void retrieveEidFromMctpServices(const UuidToUriMap& uuidToUriMap,
                                        const ResultCallback& resultCallback,
                                        const ErrorCallback& errorCallback)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        mctpStateServiceReadyIntf};
    dbus::utility::getSubTree(
        mctpObjectPath, 0, interfaces,
        [uuidToUriMap, resultCallback, errorCallback](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                const std::string errorDesc =
                    "Failed to retrieveEidFromMctpServices";
                errorCallback(errorDesc, ec.message());

                return;
            }

            std::vector<std::string> serviceNames;
            for (const auto& obj : subtree)
            {
                for (const auto& service : obj.second)
                {
                    serviceNames.push_back(service.first);
                }
            }

            retrieveEidFromMctpServiceProperties(serviceNames, uuidToUriMap,
                                                 resultCallback, errorCallback);
        });
}
