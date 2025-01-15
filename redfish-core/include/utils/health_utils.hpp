/*!
 * @file    health_utils.cpp
 * @brief   Source code for utility functions of handling Health/HealthRollup.
 */

#pragma once

#include "utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace redfish
{

namespace health_utils
{

/** NOTES: This is a temporary solution to avoid performance issues may impact
 *  other Redfish services. Please call for architecture decisions from all
 *  NvBMC teams if want to use it in other places.
 */

/**
 * @brief Get Device Health/HealthRollup from file
 *
 */
inline void getDeviceHealthInfo(crow::Response& resp,
                                const std::string& chassisId)
{
    std::string deviceId = chassisId;
    if (!BMCWEB_PLATFORM_DEVICE_PREFIX.empty())
    {
        if (chassisId.starts_with(BMCWEB_PLATFORM_DEVICE_PREFIX))
        {
            deviceId = chassisId.substr(BMCWEB_PLATFORM_DEVICE_PREFIX.size());
        }
    }

    if (deviceId.empty())
    {
        BMCWEB_LOG_ERROR("No device {} health info!", deviceId);
        return;
    }

    static const std::string deviceStatusFSPath = bmcwebDeviceStatusFSPath;

    std::string deviceStatusPath = deviceStatusFSPath + "/" + deviceId;

    nlohmann::json jStatus{};

    // "OK" by default if get can't get info
    std::string health = "OK";
    std::string rollup = "OK";

    int rc = file_utils::readFile2Json(deviceStatusPath, jStatus);
    if (rc != 0)
    {
        BMCWEB_LOG_WARNING("Health: read {} status file failed!", deviceId);
        // No need to report error since no status file means device is OK.
        resp.jsonValue["Status"]["Health"] = health;
        resp.jsonValue["Status"]["HealthRollup"] = rollup;
        return;
    }

    auto j = jStatus.find("Status");
    if (j == jStatus.end())
    {
        BMCWEB_LOG_ERROR("Health: No Status in status file of {}!", deviceId);
        messages::internalError(resp);
        return;
    }

    auto h = j->find("Health");
    if (h != j->end())
    {
        std::string value = *h;
        if (value.length() == 0)
        {
            BMCWEB_LOG_WARNING("Get {} Health failed!", deviceId);
        }
        else
        {
            BMCWEB_LOG_DEBUG("Get {} Health {}!", deviceId, value);
            health = value;
        }
    }

    auto r = j->find("HealthRollup");
    if (r != j->end())
    {
        std::string value = *r;
        if (value.length() == 0)
        {
            BMCWEB_LOG_WARNING("Get {} HealthRollup failed!", deviceId);
        }
        else
        {
            BMCWEB_LOG_DEBUG("Get {} HealthRollup {}!", deviceId, value);
            rollup = value;
        }
    }

    resp.jsonValue["Status"]["Health"] = health;
    resp.jsonValue["Status"]["HealthRollup"] = rollup;
}

} // namespace health_utils
} // namespace redfish
