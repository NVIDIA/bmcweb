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

#include "app.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utils/nvidia_hex_utils.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

namespace redfish
{
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::shared_ptr<task::TaskData> mfgTestTask;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::shared_ptr<boost::process::v2::process> mfgTestProc;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::shared_ptr<boost::asio::readable_pipe> mfgTestProcOutput;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<std::string> scriptExecOutputFiles;

/** @brief Populates taskResponse for a completed manufacturing test task. */
inline void setMfgTestAttachmentTaskResponse(
    const std::shared_ptr<task::TaskData>& taskData, std::string attachmentUri)
{
    taskData->taskResponse.emplace<task::TaskResponseCallback>(
        [attachmentUri = std::move(attachmentUri)](
            const std::shared_ptr<bmcweb::AsyncResp>& aResp) {
            aResp->res.addHeader(boost::beast::http::field::location,
                                 attachmentUri);
            aResp->res.jsonValue["@odata.id"] = attachmentUri;
        });
}

/**
 * @brief Copy script output file to the predefined location.
 *
 * @param[in]  postCodeID     Post Code ID
 * @param[out] currentValue   Current value
 * @param[out] index          Index value
 *
 * @return int -1 if an error occurred, filename index in
 * scriptExecOutputFiles vector otherwise.
 */
inline int copyMfgTestOutputFile(std::string& path)
{
    static const std::string redfishLogDir = "/var/log/";
    static const std::string mfgTestPrefix = "mfgtest-";
    std::error_code ec;

    bool fileExists = std::filesystem::exists(path, ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR("File access error: {}", ec.message());
    }
    else if (!fileExists)
    {
        BMCWEB_LOG_ERROR("{} does not exist", path);
    }
    else
    {
        std::string filename =
            mfgTestPrefix + std::to_string(scriptExecOutputFiles.size());
        std::string targetPath = redfishLogDir + filename;
        BMCWEB_LOG_DEBUG("Copying output to {}", targetPath);
        std::filesystem::copy(path, targetPath, ec);
        if (ec)
        {
            BMCWEB_LOG_ERROR("File copy error: {}", ec.message());
        }
        else
        {
            scriptExecOutputFiles.push_back(targetPath);
            return static_cast<int>(scriptExecOutputFiles.size()) - 1;
        }
    }

    return -1;
}

/**
 * @brief On-exit callback for the manufacturing script subprocess
 *
 * @param[in]  exitCode     Exit code of the script subprocess
 * @param[in]  ec           Optional system error code
 *
 */
inline void mfgTestProcExitHandler(const std::error_code& ec, int exitCode)
{
    auto& t = mfgTestTask;
    if (ec)
    {
        BMCWEB_LOG_ERROR("Error executing script: {}", ec.message());
        t->state = "Aborted";
        t->messages.emplace_back(messages::internalError());
    }
    else
    {
        BMCWEB_LOG_DEBUG("Script exit code: {}", exitCode);
        if (exitCode == 0)
        {
            std::string output;
            boost::asio::dynamic_string_buffer<std::string::value_type,
                                               std::string::traits_type,
                                               std::string::allocator_type>
                buf(output);

            boost::system::error_code readEc;
            boost::asio::read(*mfgTestProcOutput, buf,
                              boost::asio::transfer_all(), readEc);
            if (readEc && readEc != boost::asio::error::eof)
            {
                BMCWEB_LOG_ERROR("Error reading subprocess output: {}",
                                 readEc.message());
                t->state = "Exception";
                t->messages.emplace_back(
                    messages::taskAborted(std::to_string(t->index)));
                return;
            }

            int id = copyMfgTestOutputFile(output);
            if (id != -1)
            {
                std::string path =
                    std::string("/redfish/v1/Systems/")
                        .append(BMCWEB_REDFISH_SYSTEM_URI_NAME)
                        .append("/LogServices/EventLog/DiagnosticData/")
                        .append(std::to_string(id));
                std::string attachmentUri = path + "/attachment";
                std::string location = "Location: " + attachmentUri;
                if (t)
                {
                    if (auto* payload = t->payload.operator->())
                    {
                        payload->httpHeaders.emplace_back(std::move(location));
                        setMfgTestAttachmentTaskResponse(t, attachmentUri);
                        t->state = "Completed";
                        t->percentComplete = 100;
                        t->messages.emplace_back(messages::taskCompletedOK(
                            std::to_string(t->index)));
                    }
                }
            }
            else
            {
                t->state = "Exception";
                BMCWEB_LOG_ERROR(
                    "CopyMfgTestOutputFile failed with Output file error");
                t->messages.emplace_back(
                    messages::taskAborted(std::to_string(t->index)));
            }
        }
        else
        {
            t->state = "Exception";
            BMCWEB_LOG_ERROR("Mfg Script failed with exit code: {}", exitCode);
            t->messages.emplace_back(
                messages::taskAborted(std::to_string(t->index)));
        }
    }
    mfgTestProc = nullptr;
    mfgTestTask = nullptr;
    mfgTestProcOutput = nullptr;
};

inline void requestRoutesEventLogDiagnosticDataCollect(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/EventLog/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::string diagnosticDataType;
                std::string oemDiagnosticDataType;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "DiagnosticDataType",
                        diagnosticDataType, "OEMDiagnosticDataType",
                        oemDiagnosticDataType))
                {
                    return;
                }

                if (diagnosticDataType != "OEM")
                {
                    BMCWEB_LOG_ERROR(
                        "Only OEM DiagnosticDataType supported for EventLog");
                    messages::actionParameterValueFormatError(
                        asyncResp->res, diagnosticDataType,
                        "DiagnosticDataType", "CollectDiagnosticData");
                    return;
                }

                if (oemDiagnosticDataType == "Manufacturing")
                {
                    if (mfgTestTask == nullptr)
                    {
                        mfgTestTask = task::TaskData::createTask(
                            [](boost::system::error_code,
                               sdbusplus::message::message&,
                               const std::shared_ptr<task::TaskData>&
                                   taskData) {
                                mfgTestProc = nullptr;
                                mfgTestTask = nullptr;
                                if (taskData->percentComplete != 100)
                                {
                                    taskData->state = "Exception";
                                    taskData->messages.emplace_back(
                                        messages::taskAborted(
                                            std::to_string(taskData->index)));
                                }
                                return task::completed;
                            },
                            "0");
                        task::Payload payload(req);
                        mfgTestTask->payload.emplace(std::move(payload));
                        mfgTestTask->startTimer(std::chrono::seconds(
                            BMCWEB_MANUFACTURING_TEST_TIMEOUT));
                        try
                        {
                            mfgTestProcOutput =
                                std::make_shared<boost::asio::readable_pipe>(
                                    crow::connections::systemBus
                                        ->get_io_context());
                            boost::process::v2::process p(
                                crow::connections::systemBus->get_io_context(),
                                "/usr/bin/mfg-script-exec.sh",
                                {"/usr/share/mfg-script-exec/config.yml"},
                                boost::process::v2::process_stdio{
                                    .in = nullptr,
                                    .out = *mfgTestProcOutput,
                                    .err = nullptr});

                            mfgTestProc =
                                std::make_shared<boost::process::v2::process>(
                                    std::move(p));
                            mfgTestProc->async_wait(mfgTestProcExitHandler);
                        }
                        catch (const std::runtime_error& e)
                        {
                            mfgTestTask->state = "Exception";
                            BMCWEB_LOG_ERROR(
                                "Manufacturing script failed with error: {}",
                                e.what());
                            mfgTestTask->messages.emplace_back(
                                messages::taskAborted(
                                    std::to_string(mfgTestTask->index)));
                            mfgTestProc = nullptr;
                        }
                        mfgTestTask->populateResp(asyncResp->res);
                        if (mfgTestProc == nullptr)
                        {
                            mfgTestTask = nullptr;
                        }
                    }
                    else
                    {
                        mfgTestTask->populateResp(asyncResp->res);
                    }
                }
                else
                {
                    BMCWEB_LOG_ERROR("Unsupported OEMDiagnosticDataType: {}",
                                     oemDiagnosticDataType);
                    messages::actionParameterValueFormatError(
                        asyncResp->res, oemDiagnosticDataType,
                        "OEMDiagnosticDataType", "CollectDiagnosticData");
                    return;
                }
            });
}

inline void requestRoutesEventLogDiagnosticDataEntry(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/EventLog/DiagnosticData/<str>/attachment")
        .privileges(redfish::privileges::getLogEntry)
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        [[maybe_unused]] const std::string& systemName,
                        const std::string& idParam) {
            uint32_t id = 0;
            std::string_view paramSV(idParam);
            auto it = std::from_chars(paramSV.begin(), paramSV.end(), id);
            if (it.ec != std::errc())
            {
                messages::internalError(asyncResp->res);
                return;
            }

            auto files = scriptExecOutputFiles.size();
            if (files == 0 || id > files - 1)
            {
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/LogServices/EventLog/DiagnosticData/{}/attachment",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
                return;
            }
            std::ifstream file(scriptExecOutputFiles[id]);
            if (!file.good())
            {
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/LogServices/EventLog/DiagnosticData/{}/attachment",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
                return;
            }
            std::stringstream ss;
            ss << file.rdbuf();
            auto output = ss.str();

            asyncResp->res.addHeader("Content-Type",
                                     "application/octet-stream");
            asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
            asyncResp->res.write(std::move(output));
        });
}
} // namespace redfish
