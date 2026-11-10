// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace redfish::trusted_component_utils
{
/**
 * @brief Extract a component ID from an association endpoint.
 *
 * @param[in] endpoint D-Bus object path whose filename identifies the
 * component.
 * @param[in] prefix Prefix to remove from the filename when present.
 * @return The filename with the optional prefix removed.
 */
inline std::string componentId(const std::string& endpoint,
                               std::string_view prefix)
{
    std::string id = std::filesystem::path(endpoint).filename().string();
    if (id.starts_with(prefix))
    {
        id.erase(0, prefix.size());
    }
    return id;
}

/**
 * @brief Select the first endpoint matching a component ID.
 *
 * An empty component ID is used only when checking collection existence and
 * selects the first endpoint.
 *
 * @param[in] endpoints Association endpoints to search in order.
 * @param[in] id Component ID to match, or empty for a collection existence
 * check.
 * @param[in] prefix Prefix to remove before comparing endpoint component IDs.
 * @return The first matching endpoint, or an empty string if none matches.
 */
inline std::string selectEndpoint(const std::vector<std::string>& endpoints,
                                  const std::string& id,
                                  std::string_view prefix)
{
    for (const std::string& endpoint : endpoints)
    {
        if (id.empty() || componentId(endpoint, prefix) == id)
        {
            return endpoint;
        }
    }
    return {};
}

/**
 * @brief Filter associated endpoints to responders that are still available.
 *
 * Endpoints are matched by filename. Associations may outlive a responder
 * during device removal. Collections omit those stale entries while individual
 * resource requests still return 404.
 *
 * @param[in] associated Association endpoints to filter.
 * @param[in] available Object paths of available responders.
 * @return Matching associated endpoints in their original order.
 */
inline std::vector<std::string> matchingEndpoints(
    const std::vector<std::string>& associated,
    const std::vector<std::string>& available)
{
    std::vector<std::string> result;
    for (const std::string& endpoint : associated)
    {
        const auto name = std::filesystem::path(endpoint).filename();
        if (!name.empty() &&
            std::ranges::any_of(available, [&name](const std::string& path) {
                return std::filesystem::path(path).filename() == name;
            }))
        {
            result.push_back(endpoint);
        }
    }
    return result;
}

/**
 * @brief Merge legacy chassis responders with every drive's responders.
 *
 * Invoke the completion callback once after all drive lookups, including when a
 * drive has no responder association. Remove duplicates from the combined
 * result without preserving endpoint order.
 *
 * @tparam Lookup Callable accepting a drive path and a responder callback.
 * @param[in] endpoints Legacy chassis responder endpoints.
 * @param[in] drivePaths Drive object paths whose responders should be
 * collected.
 * @param[in] lookup Callable that must invoke its callback once per drive with
 * that drive's responder endpoints, or an empty vector if none are associated.
 * @param[in] callback Completion callback receiving the merged endpoints.
 */
template <typename Lookup>
inline void collectEndpoints(
    const std::vector<std::string>& endpoints,
    const std::vector<std::string>& drivePaths, Lookup&& lookup,
    const std::function<void(const std::vector<std::string>&)>& callback)
{
    auto collected = std::make_shared<std::unordered_set<std::string>>(
        endpoints.begin(), endpoints.end());
    if (drivePaths.empty())
    {
        callback(
            std::vector<std::string>(collected->begin(), collected->end()));
        return;
    }
    auto remaining = std::make_shared<size_t>(drivePaths.size());
    for (const std::string& drive : drivePaths)
    {
        lookup(drive, [collected, remaining,
                       callback](const std::vector<std::string>& responders) {
            collected->insert(responders.begin(), responders.end());
            if (--*remaining == 0)
            {
                callback(std::vector<std::string>(collected->begin(),
                                                  collected->end()));
            }
        });
    }
}
} // namespace redfish::trusted_component_utils
