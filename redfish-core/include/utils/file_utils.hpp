/*!
 * @file    file_utils.cpp
 * @brief   Source code for utility functions of handling file access.
 */

#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

namespace redfish
{

constexpr const char* bmcwebDeviceStatusFSPath = "/tmp/devices";

namespace file_utils
{

constexpr int FLOCK_TIMEOUT = 100; // msec

/**
 * @brief Read file content with timed lock protection
 * @return 0 - succ; otherwise - fail
 */
inline int readFile2Json(const std::string& filePath, nlohmann::json& j)
{
    try
    {
        // Create the file if not exist
        std::ofstream outfile(filePath, std::ios_base::app);
        outfile.close();

        boost::interprocess::file_lock fileLock(filePath.c_str());
        auto start = std::chrono::steady_clock::now();
        auto timeout = boost::posix_time::milliseconds(FLOCK_TIMEOUT);

        while (!fileLock.timed_lock(
            boost::posix_time::microsec_clock::universal_time() + timeout))
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                      start)
                    .count();

            if (elapsed >= FLOCK_TIMEOUT)
            {
                BMCWEB_LOG_ERROR("Get flock of {} timeout!\n", filePath);
                return -1;
            }
        }

        std::ifstream ifs(filePath);

        if (!ifs.is_open())
        {
            BMCWEB_LOG_ERROR("Can't open file {}!\n", filePath);
            return -2;
        }

        std::string jsonString((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());

        ifs.close();
        fileLock.unlock();

        auto json = nlohmann::json::parse(jsonString, nullptr, false);

        if (json.is_discarded())
        {
            BMCWEB_LOG_ERROR("The JSON file could not be parsed.");
            return -3;
        }

        j = json;
    }
    catch (const std::exception& ex)
    {
        BMCWEB_LOG_ERROR("An std::exception error occurred: {}", ex.what());
        return -3;
    }
    catch (const boost::exception& ex)
    {
        BMCWEB_LOG_ERROR("A boost::exception error occurred: {}",
                         boost::diagnostic_information(ex));
        return -4;
    }
    catch (...)
    {
        BMCWEB_LOG_ERROR("Caught an unknown error");
        return -5;
    }
    return 0;
}

/**
 * @brief Read file content
 * @return 0 - succ; otherwise - fail
 */
inline int readJsonFromFile(const std::string& filePath, nlohmann::json& j)
{
    try
    {
        std::ifstream ifs(filePath);

        if (!ifs.is_open())
        {
            BMCWEB_LOG_ERROR("Can't open file {}!\n", filePath);
            return -1;
        }

        std::string jsonString((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());

        ifs.close();

        auto json = nlohmann::json::parse(jsonString, nullptr, false);

        if (json.is_discarded())
        {
            BMCWEB_LOG_ERROR("The JSON file could not be parsed.");
            return -2;
        }

        j = json;
    }
    catch (const std::exception& ex)
    {
        BMCWEB_LOG_ERROR("An std::exception error occurred: {}", ex.what());
        return -3;
    }
    catch (...)
    {
        BMCWEB_LOG_ERROR("Caught an unknown error");
        return -4;
    }
    return 0;
}

} // namespace file_utils
} // namespace redfish
