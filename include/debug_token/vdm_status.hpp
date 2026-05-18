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

#include "debug_token/base.hpp"
#include "debug_token/endpoint.hpp"
#include "debug_token/vdm_status_utils.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <memory>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace redfish::debug_token
{

namespace vdm_status
{

/**
 * @brief Timeout for the VDM status query operation.
 *
 * This value should be identical to the value used in the wrapper script.
 */
constexpr const int vdmTokenStatusQueryTimeout = 3;
constexpr const size_t vdmTokenStatusQueryOutputSize = 256;

using Output = std::variant<std::monostate, VdmTokenStatus>;
using Eid = int64_t;
using Result = std::tuple<Eid, EndpointState, Output>;
using ResultCallback = std::function<void(const std::vector<Result>&)>;

class Handler : public std::enable_shared_from_this<Handler>
{
  public:
    // Delete copy constructor and assignment operator
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;

    // Delete move constructor and assignment operator
    Handler(Handler&&) = delete;
    Handler& operator=(Handler&&) = delete;

    /**
     * @brief Start the VDM status query operation
     *
     * @param eids List of EIDs to query
     * @param callback Callback function to handle the results
     */
    static void startOperation(const std::vector<Eid>& eids,
                               ResultCallback callback)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(const std::vector<Eid>& eidsParam,
                             ResultCallback callbackParam) :
                Handler(eidsParam, std::move(callbackParam))
            {}
        };
        std::shared_ptr<Handler> t =
            std::make_shared<MakeSharedHelper>(eids, std::move(callback));
        t->run();
    }

  private:
    Handler(const std::vector<Eid>& eidsParam, ResultCallback cb) :
        eids(eidsParam), callback(std::move(cb)),
        subprocessOutput(crow::connections::systemBus->get_io_context())
    {}

    ~Handler()
    {
        if (callback)
        {
            callback(results);
        }
    }

    std::vector<Eid> eids;
    std::vector<Result> results;
    ResultCallback callback;

    std::unique_ptr<boost::asio::steady_timer> operationTimer;
    std::unique_ptr<boost::process::v2::process> subprocess;
    boost::asio::readable_pipe subprocessOutput;

    /**
     * @brief Run the VDM status query operation
     *
     * This function triggers the operation. It creates a timer to monitor for
     * the subprocess call timeout. It also creates the subprocess to execute
     * the VDM status query wrapper script.
     */
    void run()
    {
        createTimer(static_cast<int>(
            eids.size() * (2 * vdmTokenStatusQueryTimeout + 1)));
        std::vector<std::string> args;
        args.reserve(eids.size());
        for (const auto& eid : eids)
        {
            args.emplace_back(std::to_string(eid));
        }
        subprocess = std::make_unique<boost::process::v2::process>(
            crow::connections::systemBus->get_io_context(),
            "/usr/bin/mctp-vdm-util-token-status-query-wrapper.sh", args,
            boost::process::v2::process_stdio{
                .in = nullptr, .out = subprocessOutput, .err = nullptr});
        subprocess->async_wait(
            [this, self(shared_from_this())](
                const boost::system::error_code& ec, int errorCode) {
                subprocessExitHandler(shared_from_this(), errorCode, ec);
            });
        // TODO hook up return code.
    }

    /**
     * @brief Create a timer to monitor for the subprocess call timeout
     *
     * @param timeout Timeout in seconds
     */
    void createTimer(int timeout)
    {
        operationTimer = std::make_unique<boost::asio::steady_timer>(
            crow::connections::systemBus->get_io_context());
        operationTimer->expires_after(std::chrono::seconds(timeout));
        operationTimer->async_wait(
            std::bind_front(&Handler::timerHandler, this, shared_from_this()));
    }

    /**
     * @brief Destroy the timer
     *
     * @param self The shared self pointer to the parent object (unused)
     */
    void destroyTimer(const std::shared_ptr<Handler>& /*unused*/)
    {
        operationTimer.reset();
    }

    /**
     * @brief Timer handler for the subprocess call timeout
     *
     * This function is called when the timer expires or when an error occurs.
     * It resets the timer and cancels the subprocess.
     * @param self The shared self pointer to the parent object
     * @param ec The error code
     */
    void timerHandler(const std::shared_ptr<Handler>& self,
                      const boost::system::error_code& ec)
    {
        subprocess.reset();
        if (!ec)
        {
            BMCWEB_LOG_ERROR("VDM operation timeout");
            return;
        }
        if (ec != boost::asio::error::operation_aborted)
        {
            BMCWEB_LOG_ERROR("async_wait error: {}", ec.message());
        }
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          std::bind_front(&Handler::destroyTimer, this, self));
    }

    /**
     * @brief Subprocess exit handler
     *
     * This function is called when the status query wrapper script exits. It
     * parses the output and calls the callback function with the results.
     * @param self The shared self pointer to the parent object
     * @param exitCode The exit code of the subprocess
     * @param ec The error code
     */
    void subprocessExitHandler(const std::shared_ptr<Handler>& self,
                               int exitCode, const std::error_code& ec)
    {
        destroyTimer(self);
        if (ec)
        {
            BMCWEB_LOG_ERROR("VDM status query subprocess: {}", ec.message());
        }
        if (exitCode != 0)
        {
            BMCWEB_LOG_ERROR("VDM status query subprocess exit code: {}",
                             exitCode);
        }
        boost::asio::streambuf buf;
        boost::system::error_code readEc;
        boost::asio::read(subprocessOutput, buf, boost::asio::transfer_all(),
                          readEc);
        if (readEc && readEc != boost::asio::error::eof)
        {
            BMCWEB_LOG_ERROR("Error reading subprocess output: {}",
                             readEc.message());
            for (const auto& eid : eids)
            {
                results.emplace_back(eid, EndpointState::Error,
                                     std::monostate());
            }
            return;
        }
        std::map<int64_t, VdmTokenStatus> outputMap =
            parseVdmUtilWrapperOutput(buf);
        for (const auto& eid : eids)
        {
            const auto vdmStatus = outputMap.find(eid);
            if (vdmStatus == outputMap.end())
            {
                BMCWEB_LOG_ERROR("No status query data for EID {}", eid);
                results.emplace_back(eid, EndpointState::Error,
                                     std::monostate());
                continue;
            }
            if (vdmStatus->second.responseStatus ==
                    VdmResponseStatus::INVALID_LENGTH ||
                vdmStatus->second.responseStatus ==
                    VdmResponseStatus::PROCESSING_ERROR)
            {
                BMCWEB_LOG_ERROR("Invalid status query data for EID {}", eid);
                results.emplace_back(eid, EndpointState::Error,
                                     std::monostate());
                continue;
            }
            if (vdmStatus->second.responseStatus == VdmResponseStatus::ERROR)
            {
                const auto& errorCode = vdmStatus->second.errorCode;
                if (errorCode.has_value())
                {
                    BMCWEB_LOG_ERROR("Error code {} received for EID {}",
                                     *errorCode, eid);
                }
                results.emplace_back(eid, EndpointState::Error,
                                     vdmStatus->second);
                continue;
            }
            if (vdmStatus->second.tokenStatus ==
                VdmTokenInstallationStatus::INVALID)
            {
                BMCWEB_LOG_ERROR("Invalid token status for EID {}", eid);
                results.emplace_back(eid, EndpointState::Error,
                                     vdmStatus->second);
                continue;
            }
            results.emplace_back(eid, EndpointState::StatusAcquired,
                                 vdmStatus->second);
        }
    }
};

} // namespace vdm_status

} // namespace redfish::debug_token
