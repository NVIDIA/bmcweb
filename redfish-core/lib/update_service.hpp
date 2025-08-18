// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "generated/enums/update_service.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "multipart_parser.hpp"
#include "nvidia_update_service.hpp"
#include "ossl_random.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "str_utility.hpp"
#include "task.hpp"
#include "task_messages.hpp"
#include "utility.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/sw_utils.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/url/format.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>
#include <boost/url/url_view_base.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{

// Match signals added on software path
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateMatcher;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateErrorMatcher;

// Timer for software available
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<boost::asio::steady_timer> fwAvailableTimer;
// match for logging
constexpr auto fwObjectCreationDefaultTimeout = 40;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match::match> loggingMatch = nullptr;

struct MemoryFileDescriptor
{
    int fd = -1;

    explicit MemoryFileDescriptor(const std::string& filename) :
        fd(memfd_create(filename.c_str(), 0))
    {}

    MemoryFileDescriptor(const MemoryFileDescriptor&) = default;
    MemoryFileDescriptor(MemoryFileDescriptor&& other) noexcept : fd(other.fd)
    {
        other.fd = -1;
    }
    MemoryFileDescriptor& operator=(const MemoryFileDescriptor&) = delete;
    MemoryFileDescriptor& operator=(MemoryFileDescriptor&&) = default;

    ~MemoryFileDescriptor()
    {
        if (fd != -1)
        {
            close(fd);
        }
    }

    bool rewind() const
    {
        if (lseek(fd, 0, SEEK_SET) == -1)
        {
            BMCWEB_LOG_ERROR("Failed to seek to beginning of image memfd");
            return false;
        }
        return true;
    }
};

/**
 * @brief A session for asynchronously writing image data to a file.
 *
 * This struct manages the asynchronous writing of image data to a specified
 * file path using Boost.Asio. It handles writing data in chunks and ensures
 * that the file is properly closed upon completion or error.
 */
struct AsyncImageWriteSession :
    public std::enable_shared_from_this<AsyncImageWriteSession>
{
    /**
     * @brief Constructs an AsyncImageWriteSession.
     *
     * @param asyncRespIn A shared pointer to the asynchronous response object.
     * @param streamIn A shared pointer to the Boost.Asio stream descriptor.
     * @param filepathIn The file path where the image data will be written.
     * @param dataRefIn A reference to the string containing the image data.
     * @param sharedReqIn An optional shared pointer to the request object.
     */
    AsyncImageWriteSession(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
        std::shared_ptr<boost::asio::posix::stream_descriptor> streamIn,
        const std::filesystem::path& filepathIn, const std::string& dataRefIn,
        std::shared_ptr<const crow::Request> sharedReqIn = nullptr) :
        asyncResp(asyncRespIn), stream(std::move(streamIn)),
        filepath(filepathIn), dataRef(dataRefIn),
        sharedReq(std::move(sharedReqIn))
    {}

    /**
     * @brief Starts the asynchronous write operation.
     *
     * Initiates the process of writing the image data to the file in chunks.
     */
    void start()
    {
        writeChunk(0);
    }

  private:
    /**
     * @brief Writes a chunk of data to the file.
     *
     * @param offset The current offset in the data to start writing from.
     */
    void writeChunk(std::size_t offset)
    {
        if (offset >= dataRef.size())
        {
            boost::system::error_code ec;
            stream->close(ec);
            BMCWEB_LOG_INFO("Finished writing file to {}", filepath.string());
            return;
        }

        static constexpr std::size_t chunkSize = 8192;
        const std::size_t bytesToWrite =
            std::min(chunkSize, dataRef.size() - offset);

        std::string_view dataRefView{dataRef};
        std::string_view chunk = dataRefView.substr(offset, bytesToWrite);

        auto buffer = boost::asio::buffer(chunk.data(), chunk.size());

        auto self = shared_from_this();
        boost::asio::async_write(
            *stream, buffer,
            [self, offset,
             bytesToWrite](const boost::system::error_code& ec,
                           std::size_t /*bytesTransferred*/) mutable {
                if (!ec)
                {
                    const std::size_t newOffset = offset + bytesToWrite;
                    BMCWEB_LOG_DEBUG("Wrote {} bytes [offset={}] to {}",
                                     bytesToWrite, newOffset,
                                     self->filepath.string());
                    self->writeChunk(newOffset);
                }
                else
                {
                    BMCWEB_LOG_ERROR("Write error on {}: {}",
                                     self->filepath.string(), ec.message());
                    boost::system::error_code closeEc;
                    self->stream->close(closeEc);
                    messages::internalError(self->asyncResp->res);
                }
            });
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    std::shared_ptr<boost::asio::posix::stream_descriptor> stream;
    std::filesystem::path filepath;
    const std::string& dataRef;
    std::shared_ptr<const crow::Request> sharedReq;
};

inline void cleanUp()
{
    fwUpdateInProgress = false;
    fwUpdateMatcher = nullptr;
    fwUpdateErrorMatcher = nullptr;
}

inline void activateImage(const std::string& objPath,
                          const std::string& service)
{
    BMCWEB_LOG_DEBUG("Activate image for {} {}", objPath, service);
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Software.Activation", "RequestedActivation",
        "xyz.openbmc_project.Software.Activation.RequestedActivations.Active",
        [](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                BMCWEB_LOG_ERROR("error msg = {}", ec.message());
            }
        });
}

inline void handleLogMatchCallback(sdbusplus::message_t& m,
                                   nlohmann::json& messages)
{
    std::vector<std::pair<std::string, dbus::utility::DBusPropertiesMap>>
        interfacesProperties;
    sdbusplus::message::object_path objPath;
    m.read(objPath, interfacesProperties);
    const std::unordered_map<std::string, std::string>* additionalData =
        nullptr;
    for (auto interface : interfacesProperties)
    {
        if (interface.first == "xyz.openbmc_project.Logging.Entry")
        {
            std::string rfMessage;
            std::string resolution;
            std::string messageNamespace;
            std::vector<std::string> rfArgs;
            for (auto& propertyMap : interface.second)
            {
                if (propertyMap.first == "AdditionalData")
                {
                    additionalData = std::get_if<
                        std::unordered_map<std::string, std::string>>(
                        &propertyMap.second);

                    if (additionalData != nullptr)
                    {
                        redfish::AdditionalData additional(*additionalData);

                        if (additional.count("REDFISH_MESSAGE_ID") > 0)
                        {
                            rfMessage = additional["REDFISH_MESSAGE_ID"];
                        }
                        if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                        {
                            bmcweb::split(rfArgs,
                                          additional["REDFISH_MESSAGE_ARGS"],
                                          ',');
                        }
                        if (additional.count("namespace") > 0)
                        {
                            messageNamespace = additional["namespace"];
                        }
                    }
                }
                else if (propertyMap.first == "Resolution")
                {
                    const std::string* value =
                        std::get_if<std::string>(&propertyMap.second);
                    if (value != nullptr)
                    {
                        resolution = *value;
                    }
                }
            }
            /* we need to have found the id, data, this image needs to
               correspond to the image we are working with right now and the
               message should be update related */
            if (additionalData == nullptr || messageNamespace != "FWUpdate")
            {
                // something is invalid
                BMCWEB_LOG_DEBUG("Got invalid log message");
            }
            else
            {
                auto message =
                    redfish::messages::getUpdateMessage(rfMessage, rfArgs);
                if (message.find("Message") != message.end())
                {
                    if (!resolution.empty())
                    {
                        message["Resolution"] = resolution;
                    }
                    messages.emplace_back(message);
                }
                else
                {
                    BMCWEB_LOG_ERROR("Unknown message ID: {}", rfMessage);
                }
            }
        }
    }
}

inline void loggingMatchCallback(const std::shared_ptr<task::TaskData>& task,
                                 sdbusplus::message_t& m)
{
    if (task == nullptr)
    {
        return;
    }
    handleLogMatchCallback(m, task->messages);
}
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static nlohmann::json preTaskMessages = {};
inline void preTaskLoggingHandler(sdbusplus::message_t& m)
{
    handleLogMatchCallback(m, preTaskMessages);
}

inline bool handleCreateTask(const boost::system::error_code& ec2,
                             sdbusplus::message_t& msg,
                             const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec2)
    {
        return task::completed;
    }

    std::string iface;
    dbus::utility::DBusPropertiesMap values;

    std::string index = std::to_string(taskData->index);
    msg.read(iface, values);

    if (iface == "xyz.openbmc_project.Software.Activation")
    {
        const std::string* state = nullptr;
        for (const auto& property : values)
        {
            if (property.first == "Activation")
            {
                state = std::get_if<std::string>(&property.second);
                if (state == nullptr)
                {
                    taskData->messages.emplace_back(messages::internalError());
                    fwUpdateInProgress = false;
                    return task::completed;
                }
            }
        }

        if (state == nullptr)
        {
            return !task::completed;
        }

        if (state->ends_with("Invalid") || state->ends_with("Failed"))
        {
            taskData->state = "Exception";
            taskData->status = "Warning";
            taskData->messages.emplace_back(messages::taskAborted(index));
            fwUpdateInProgress = false;
            return task::completed;
        }

        if (state->ends_with("Activating"))
        {
            // set firmware inventory inprogress
            // flag to true during activation.
            // this will ensure no furthur
            // updates allowed during this time
            // from redfish
            fwUpdateInProgress = true;
            return !task::completed;
        }

        if (state->ends_with("Staged"))
        {
            taskData->state = "Stopping";
            taskData->messages.emplace_back(messages::taskPaused(index));

            // its staged, set a long timer to
            // allow them time to complete the
            // update (probably cycle the
            // system) if this expires then
            // task will be canceled
            taskData->extendTimer(std::chrono::hours(5));
            return !task::completed;
        }

        if (state->ends_with("Active"))
        {
            taskData->messages.emplace_back(messages::taskCompletedOK(index));
            taskData->state = "Completed";
            fwUpdateInProgress = false;
            return task::completed;
        }
    }
    else if (iface == "xyz.openbmc_project.Software.ActivationProgress")
    {
        const uint8_t* progress = nullptr;
        for (const auto& property : values)
        {
            if (property.first == "Progress")
            {
                progress = std::get_if<uint8_t>(&property.second);
                if (progress == nullptr)
                {
                    taskData->messages.emplace_back(messages::internalError());
                    return task::completed;
                }
            }
        }

        if (progress == nullptr)
        {
            return !task::completed;
        }
        taskData->percentComplete = *progress;

        taskData->messages.emplace_back(
            messages::taskProgressChanged(index, *progress));

        // if we're getting status updates it's
        // still alive, update timer
        taskData->extendTimer(
            std::chrono::minutes(BMCWEB_UPDATE_SERVICE_TASK_TIMEOUT));
    }

    // as firmware update often results in a
    // reboot, the task  may never "complete"
    // unless it is an error

    return !task::completed;
}

/**
 * @brief Retrieve the task message in JSON format for a given task state and
 * index.
 *
 * This function overrides the base function to handle firmware update state
 * management. It is designed to manage the "Aborted" state and reset the global
 * fwUpdateInProgress flag to false.
 *
 * @param state A string representing the task state
 * @param index The index to identify the specific task message
 *
 * @return nlohmann::json The task message corresponding to the given state and
 * index
 */
inline nlohmann::json getTaskMessage(const std::string_view state, size_t index)
{
    if (state == "Aborted")
    {
        fwUpdateInProgress = false;
    }

    return redfish::task::getMessage(state, index);
}

/**
 * @brief Check the initial activation state of a software update
 *
 * This function checks if a software activation has already failed before
 * the property change monitoring begins. This handles the race condition
 * where PLDM or other update services might have already marked the
 * activation as failed immediately after creating the software object.
 *
 * If the activation state is already "Failed" or "Invalid", the task is
 * immediately marked as failed with appropriate status and messages.
 *
 * @param[in] task    The task object to update if activation has failed
 * @param[in] objPath The D-Bus object path of the software activation
 */
inline void checkInitialActivationState(
    const std::shared_ptr<task::TaskData>& task,
    const sdbusplus::message::object_path& objPath)
{
    dbus::utility::getDbusObject(
        objPath.str,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Software.Activation"},
        [task, objPath](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& mapperResponse) {
            if (ec || mapperResponse.empty())
            {
                return;
            }

            dbus::utility::getProperty<std::string>(
                mapperResponse.begin()->first, objPath.str,
                "xyz.openbmc_project.Software.Activation", "Activation",
                [task](const boost::system::error_code& ec2,
                       const std::string& activation) {
                    if (!ec2 && (activation.ends_with("Invalid") ||
                                 activation.ends_with("Failed")))
                    {
                        std::string index = std::to_string(task->index);
                        task->state = "Exception";
                        task->status = "Warning";
                        task->messages.emplace_back(
                            messages::taskAborted(index));
                        task->timer.cancel();
                        task->finishTask();
                        fwUpdateInProgress = false;
                    }
                });
        });
}

inline void createTask(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       task::Payload&& payload,
                       const sdbusplus::message::object_path& objPath)
{
    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        std::bind_front(handleCreateTask),
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',path='" +
            objPath.str + "'",
        std::bind_front(getTaskMessage));

    task->startTimer(std::chrono::minutes(BMCWEB_UPDATE_SERVICE_TASK_TIMEOUT));
    task->populateResp(asyncResp->res);
    task->payload.emplace(std::move(payload));
    loggingMatch = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',"
        "path='/xyz/openbmc_project/logging'",
        [task](sdbusplus::message_t& msgLog) {
            loggingMatchCallback(task, msgLog);
        });
    if (!preTaskMessages.empty())
    {
        task->messages.insert(task->messages.end(), preTaskMessages.begin(),
                              preTaskMessages.end());
    }
    preTaskMessages = {};

    checkInitialActivationState(task, objPath);
}

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
inline void softwareInterfaceAdded(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    sdbusplus::message_t& m, task::Payload&& payload)
{
    dbus::utility::DBusInterfacesMap interfacesProperties;

    sdbusplus::message::object_path objPath;

    m.read(objPath, interfacesProperties);

    BMCWEB_LOG_DEBUG("obj path = {}", objPath.str);
    for (const auto& interface : interfacesProperties)
    {
        BMCWEB_LOG_DEBUG("interface = {}", interface.first);

        if (interface.first == "xyz.openbmc_project.Software.Activation")
        {
            // Retrieve service and activate
            constexpr std::array<std::string_view, 1> interfaces = {
                "xyz.openbmc_project.Software.Activation"};
            dbus::utility::getDbusObject(
                objPath.str, interfaces,
                [objPath, asyncResp, payload(std::move(payload))](
                    const boost::system::error_code& ec,
                    const std::vector<
                        std::pair<std::string, std::vector<std::string>>>&
                        objInfo) mutable {
                    if (ec)
                    {
                        if (asyncResp)
                        {
                            BMCWEB_LOG_ERROR("error_code = {}", ec);
                            BMCWEB_LOG_ERROR("error msg = {}", ec.message());
                            if (asyncResp)
                            {
                                messages::internalError(asyncResp->res);
                            }
                            cleanUp();
                            return;
                        }
                        // Ensure we only got one service back
                        if (objInfo.size() != 1)
                        {
                            BMCWEB_LOG_ERROR("Invalid Object Size {}",
                                             objInfo.size());
                            if (asyncResp)
                            {
                                messages::internalError(asyncResp->res);
                            }
                            cleanUp();
                            return;
                        }
                        cleanUp();
                        return;
                    }
                    // cancel timer only when
                    // xyz.openbmc_project.Software.Activation interface
                    // is added
                    fwAvailableTimer = nullptr;
                    sdbusplus::message::object_path objectPath(objPath.str);
                    std::string swID = objectPath.filename();
                    if (swID.empty())
                    {
                        BMCWEB_LOG_ERROR("Software Id is empty");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    activateImage(objPath.str, objInfo[0].first);
                    if (asyncResp)
                    {
                        createTask(asyncResp, std::move(payload), objPath);
                    }
                    fwUpdateInProgress = false;
                });
            break;
        }
    }
}

inline void afterAvailbleTimerAsyncWait(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& imagePath, const boost::system::error_code& ec)
{
    fwUpdateMatcher = nullptr;
    if (ec == boost::asio::error::operation_aborted)
    {
        // expected, we were canceled before the timer completed.
        return;
    }
    fwUpdateInProgress = false;
    BMCWEB_LOG_ERROR("Timed out waiting for firmware object being created");
    BMCWEB_LOG_ERROR("FW image may has already been uploaded to server");
    if (ec)
    {
        BMCWEB_LOG_ERROR("Async_wait failed{}", ec);
        return;
    }
    if (asyncResp)
    {
        redfish::messages::internalError(asyncResp->res);
    }
    // remove update package to allow next update
    if (!imagePath.empty())
    {
        std::filesystem::remove(imagePath);
    }
}

inline void handleUpdateErrorType(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& url,
    const std::string& type)
{
    // NOLINTBEGIN(bugprone-branch-clone)
    if (type == "xyz.openbmc_project.Software.Image.Error.UnTarFailure")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type ==
             "xyz.openbmc_project.Software.Image.Error.ManifestFileFailure")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type == "xyz.openbmc_project.Software.Image.Error.ImageFailure")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type == "xyz.openbmc_project.Software.Version.Error.AlreadyExists")
    {
        messages::resourceAlreadyExists(asyncResp->res, "UpdateService",
                                        "Version", "uploaded version");
    }
    else if (type == "xyz.openbmc_project.Software.Image.Error.BusyFailure")
    {
        messages::serviceTemporarilyUnavailable(asyncResp->res, url);
    }
    else if (type == "xyz.openbmc_project.Software.Version.Error.Incompatible")
    {
        messages::internalError(asyncResp->res);
    }
    else if (type ==
             "xyz.openbmc_project.Software.Version.Error.ExpiredAccessKey")
    {
        messages::internalError(asyncResp->res);
    }
    else if (type ==
             "xyz.openbmc_project.Software.Version.Error.InvalidSignature")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type ==
                 "xyz.openbmc_project.Software.Image.Error.InternalFailure" ||
             type == "xyz.openbmc_project.Software.Version.Error.HostFile")
    {
        BMCWEB_LOG_ERROR("Software Image Error type={}", type);
        messages::internalError(asyncResp->res);
    }
    else
    {
        // Unrelated error types. Ignored
        BMCWEB_LOG_INFO("Non-Software-related Error type={}. Ignored", type);
        return;
    }
    // NOLINTEND(bugprone-branch-clone)
    // Clear the timer
    fwAvailableTimer = nullptr;
}

inline void afterUpdateErrorMatcher(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& url,
    sdbusplus::message_t& m)
{
    dbus::utility::DBusInterfacesMap interfacesProperties;
    sdbusplus::message::object_path objPath;
    m.read(objPath, interfacesProperties);
    BMCWEB_LOG_ERROR("obj path = {}", objPath.str);
    for (const std::pair<std::string, dbus::utility::DBusPropertiesMap>&
             interface : interfacesProperties)
    {
        if (interface.first == "xyz.openbmc_project.Logging.Entry")
        {
            for (const std::pair<std::string, dbus::utility::DbusVariantType>&
                     value : interface.second)
            {
                if (value.first != "Message")
                {
                    continue;
                }
                const std::string* type =
                    std::get_if<std::string>(&value.second);
                if (type == nullptr)
                {
                    // if this was our message, timeout will cover it
                    return;
                }
                handleUpdateErrorType(asyncResp, url, *type);
            }
        }
    }
}

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
static void monitorForSoftwareAvailable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req,
    int timeoutTimeSeconds = fwObjectCreationDefaultTimeout,
    const std::string& imagePath = {})
{
    if (req.ioService == nullptr)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    fwAvailableTimer =
        std::make_unique<boost::asio::steady_timer>(*req.ioService);

    fwAvailableTimer->expires_after(std::chrono::seconds(timeoutTimeSeconds));

    fwAvailableTimer->async_wait(
        std::bind_front(afterAvailbleTimerAsyncWait, asyncResp, imagePath));

    task::Payload payload(req);
    auto callback = [asyncResp, payload](sdbusplus::message_t& m) mutable {
        BMCWEB_LOG_DEBUG("Match fired");
        softwareInterfaceAdded(asyncResp, m, std::move(payload));
    };

    fwUpdateInProgress = true;

    fwUpdateMatcher = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',path='/'",
        callback);

    loggingMatch = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',"
        "path='/xyz/openbmc_project/logging'",
        preTaskLoggingHandler);
}

inline std::optional<boost::urls::url> parseSimpleUpdateUrl(
    std::string imageURI, std::optional<std::string> transferProtocol,
    crow::Response& res)
{
    if (imageURI.find("://") == std::string::npos)
    {
        if (imageURI.starts_with("/"))
        {
            messages::actionParameterValueTypeError(
                res, imageURI, "ImageURI", "UpdateService.SimpleUpdate");
            return std::nullopt;
        }
        if (!transferProtocol)
        {
            messages::actionParameterValueTypeError(
                res, imageURI, "ImageURI", "UpdateService.SimpleUpdate");
            return std::nullopt;
        }
        // OpenBMC currently only supports HTTPS
        if (*transferProtocol == "HTTPS")
        {
            imageURI = "https://" + imageURI;
        }
        else
        {
            messages::actionParameterNotSupported(res, "TransferProtocol",
                                                  *transferProtocol);
            BMCWEB_LOG_ERROR("Request incorrect protocol parameter: {}",
                             *transferProtocol);
            return std::nullopt;
        }
    }

    boost::system::result<boost::urls::url> url =
        boost::urls::parse_absolute_uri(imageURI);
    if (!url)
    {
        messages::actionParameterValueTypeError(res, imageURI, "ImageURI",
                                                "UpdateService.SimpleUpdate");

        return std::nullopt;
    }
    url->normalize();

    if (url->scheme() == "tftp")
    {
        if (url->encoded_path().size() < 2)
        {
            messages::actionParameterNotSupported(res, "ImageURI",
                                                  url->buffer());
            return std::nullopt;
        }
    }
    else if (url->scheme() == "https")
    {
        // Empty paths default to "/"
        if (url->encoded_path().empty())
        {
            url->set_encoded_path("/");
        }
    }
    else
    {
        messages::actionParameterNotSupported(res, "ImageURI", imageURI);
        return std::nullopt;
    }

    if (url->encoded_path().empty())
    {
        messages::actionParameterValueTypeError(res, imageURI, "ImageURI",
                                                "UpdateService.SimpleUpdate");
        return std::nullopt;
    }

    return *url;
}

inline void doHttpsUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const boost::urls::url_view_base& url)
{
    messages::actionParameterNotSupported(asyncResp->res, "ImageURI",
                                          url.buffer());
}

inline void handleUpdateServiceSimpleUpdateAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<std::string> transferProtocol;
    std::string imageURI;

    BMCWEB_LOG_ERROR("Enter UpdateService.SimpleUpdate doPost");

    // User can pass in both TransferProtocol and ImageURI parameters or
    // they can pass in just the ImageURI with the transfer protocol
    // embedded within it.
    // 1) TransferProtocol:TFTP ImageURI:1.1.1.1/myfile.bin
    // 2) ImageURI:tftp://1.1.1.1/myfile.bin

    if (!json_util::readJsonAction(              //
            req, asyncResp->res,                 //
            "ImageURI", imageURI,                //
            "TransferProtocol", transferProtocol //
            ))
    {
        BMCWEB_LOG_ERROR("Missing TransferProtocol or ImageURI parameter");
        return;
    }

    std::optional<boost::urls::url> url =
        parseSimpleUpdateUrl(imageURI, transferProtocol, asyncResp->res);
    if (!url)
    {
        return;
    }
    if (url->scheme() == "https")
    {
        doHttpsUpdate(asyncResp, *url);
    }
    else
    {
        messages::actionParameterNotSupported(asyncResp->res, "ImageURI",
                                              url->buffer());
        return;
    }

    BMCWEB_LOG_ERROR("Exit UpdateService.SimpleUpdate doPost");
}

/**
 * @brief Upload firmware image
 *
 * @param[in] req  HTTP request.
 * @param[in] asyncResp Pointer to object holding response data
 *
 * @return None
 */
inline void uploadImageFile(const std::shared_ptr<const crow::Request>& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::filesystem::path filepath(
        std::string(BMCWEB_UPDATE_SERVICE_IMAGE_LOCATION) +
        bmcweb::getRandomUUID());

    monitorForSoftwareAvailable(asyncResp, *req, fwObjectCreationDefaultTimeout,
                                filepath);

    BMCWEB_LOG_INFO("Writing file to {}", filepath.string());

    MultipartParser parser(filepath);
    ParserError ec = parser.parse(*req);
    if (ec != ParserError::PARSER_SUCCESS)
    {
        // handle error
        BMCWEB_LOG_ERROR("MIME parse failed, ec : {}", static_cast<int>(ec));
        messages::internalError(asyncResp->res);
        return;
    }

    bool hasUpdateFile = false;

    for (const FormPart& formpart : parser.mime_fields)
    {
        if (formpart.isUpdateFile)
        {
            hasUpdateFile = true;
            break;
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR("File with firmware image is missing.");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
    }
}

/**
 * @brief Check whether an update can be processed.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 *
 * @return Returns true when the firmware can be applied.
 */
inline bool preCheckMultipartUpdateServiceReq(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    bool enableFWInProgCheck)
{
    if (req.body().size() > (firmwareImageLimitBytes))
    {
        if (asyncResp)
        {
            BMCWEB_LOG_ERROR("Large image size: {}", req.body().size());
            // std::string resolution =
            //     "Firmware package size is greater than allowed "
            //     "size. Make sure package size is less than "
            //     "UpdateService.MaxImageSizeBytes property and "
            //     "retry the firmware update operation.";
            messages::payloadTooLarge(asyncResp->res);
        }
        return false;
    }

    // Only allow one FW update at a time
    if (enableFWInProgCheck && fwUpdateInProgress)
    {
        if (asyncResp)
        {
            // don't copy the image, update already in progress.
            std::string resolution =
                "Another update is in progress. Retry"
                " the update operation once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR("Update already in progress.");
        }
        return false;
    }

    return true;
}

/**
 * @brief Sets the ForceUpdate flag in the update policy.
 *
 * This function asynchronously updates the ForceUpdate flag in the software
 * update policy.
 *
 * @param[in] asyncResp - Pointer to the object holding the response data.
 * @param[in] objpath - D-Bus object path for the UpdatePolicy.
 * @param[in] forceUpdate - The boolean value to set for the ForceUpdate flag.
 * @param[in] callback - A callback function to be called after the ForceUpdate
 * update policy is changed. This is an optional parameter with a default value
 * of an empty function.
 *
 * @return None
 */
inline void setForceUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& objpath, const bool forceUpdate,
                           const std::function<void()>& callback = {})
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, forceUpdate, objpath, callback](
            const boost::system::error_code errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            // Check if only one service implements
            // xyz.openbmc_project.Software.UpdatePolicy
            if (objInfo.size() != 1)
            {
                BMCWEB_LOG_ERROR(
                    "Expected exactly one service implementing xyz.openbmc_project.Software.UpdatePolicy, but found {} services.",
                    objInfo.size());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 callback](const boost::system::error_code errCodePolicy) {
                    if (errCodePolicy)
                    {
                        BMCWEB_LOG_ERROR("error_code = {}", errCodePolicy);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    if (callback)
                    {
                        callback();
                    }
                },
                objInfo[0].first, objpath, "org.freedesktop.DBus.Properties",
                "Set", "xyz.openbmc_project.Software.UpdatePolicy",
                "ForceUpdate", dbus::utility::DbusVariantType(forceUpdate));
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software",
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

/**
 * @brief Multipart update data structure
 */
struct MultiPartUpdate
{
    struct UpdateParameters
    {
        std::optional<std::string> applyTime;
        std::optional<std::vector<std::string>> targets;
        std::optional<bool> forceUpdate;
    } params;
};

/**
 * @brief Parse form part name from Content-Disposition header
 */
inline std::optional<std::string> parseFormPartName(
    const boost::beast::http::fields::const_iterator& contentDisposition)
{
    size_t semicolonPos = contentDisposition->value().find(';');
    if (semicolonPos == std::string::npos)
    {
        return std::nullopt;
    }

    for (const auto& param : boost::beast::http::param_list{
             contentDisposition->value().substr(semicolonPos)})
    {
        if (param.first == "name" && !param.second.empty())
        {
            return std::string(param.second);
        }
    }
    return std::nullopt;
}

/**
 * @brief Process UpdateParameters JSON content
 */
inline std::optional<MultiPartUpdate::UpdateParameters> processUpdateParameters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view content)
{
    MultiPartUpdate::UpdateParameters params;
    nlohmann::json jsonContent = nlohmann::json::parse(content, nullptr, false);
    if (jsonContent.is_discarded())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return std::nullopt;
    }

    nlohmann::json::object_t* obj =
        jsonContent.get_ptr<nlohmann::json::object_t*>();
    if (obj == nullptr)
    {
        messages::propertyValueTypeError(asyncResp->res, content,
                                         "UpdateParameters");
        return std::nullopt;
    }

    if (!json_util::readJsonObject(
            *obj, asyncResp->res, "@Redfish.OperationApplyTime",
            params.applyTime, "Targets", params.targets, "ForceUpdate",
            params.forceUpdate))
    {
        return std::nullopt;
    }

    return params;
}

/**
 * @brief Extract multipart update parameters (metadata only, no file content)
 *
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] parser MultipartParser with parsed metadata (skipFileContent=true)
 *
 * @return Optional MultiPartUpdate struct with parameters only
 */
inline std::optional<MultiPartUpdate> extractMultipartUpdateParameters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const MultipartParser& parser)
{
    MultiPartUpdate multipart;
    bool hasUpdateFile = false;

    // Parse metadata only (UpdateFile content was skipped)
    for (const FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            BMCWEB_LOG_ERROR("Couldn't find Content-Disposition");
            continue;
        }

        auto formFieldNameOpt = parseFormPartName(it);
        if (!formFieldNameOpt.has_value())
        {
            continue;
        }

        const std::string& formFieldName = formFieldNameOpt.value();

        if (formFieldName == "UpdateParameters")
        {
            std::optional<MultiPartUpdate::UpdateParameters> params =
                processUpdateParameters(asyncResp, formpart.content);
            if (!params)
            {
                return std::nullopt;
            }
            if (params->applyTime && !multipart.params.applyTime)
            {
                multipart.params.applyTime = std::move(params->applyTime);
            }
            if (params->targets && !multipart.params.targets)
            {
                multipart.params.targets = std::move(params->targets);
            }
            if (params->forceUpdate && !multipart.params.forceUpdate)
            {
                multipart.params.forceUpdate = params->forceUpdate;
            }
        }
        else if (formFieldName == "UpdateFile")
        {
            hasUpdateFile = true;
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR("UpdateFile form part is missing");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
        return std::nullopt;
    }

    return multipart;
}

/**
 * @brief Convert ApplyTime to D-Bus format
 */
inline bool convertApplyTime(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& applyTime, std::string& dbusApplyTime)
{
    if (applyTime == "Immediate")
    {
        dbusApplyTime =
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate";
    }
    else if (applyTime == "OnReset")
    {
        dbusApplyTime =
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.OnReset";
    }
    else
    {
        BMCWEB_LOG_ERROR("Invalid ApplyTime value: {}", applyTime);
        messages::propertyValueNotInList(asyncResp->res, applyTime,
                                         "@Redfish.OperationApplyTime");
        return false;
    }
    return true;
}

/**
 * @brief Check if the list of targets contains invalid and unupdateable
 * targets. The function returns a list of valid targets in the parameter
 * 'validTargets'
 *
 * @param[in] uriTargets  List of components delivered in HTTPRequest
 * @param[in] updateables List of all unupdateable components in the system
 * @param[in] swInvPaths  List of software inventory paths
 * @param[out] validTargets  List of valid components delivered in HTTPRequest
 *
 * @return It returns true when a list of delivered components contains invalid
 * or unupdateable components
 */
inline bool areTargetsInvalidOrUnupdatable(
    const std::vector<std::string>& uriTargets,
    const std::vector<std::string>& updateables,
    const std::vector<std::string>& swInvPaths,
    std::vector<sdbusplus::message::object_path>& validTargets)
{
    bool hasAnyInvalidOrUnupdateableTarget = false;
    for (const std::string& target : uriTargets)
    {
        std::string componentName = std::filesystem::path(target).filename();
        bool validTarget = false;
        std::string softwarePath =
            "/xyz/openbmc_project/software/" + componentName;

        if (std::any_of(swInvPaths.begin(), swInvPaths.end(),
                        [&](const std::string& path) {
                            return path.find(softwarePath) != std::string::npos;
                        }))
        {
            validTarget = true;

            if (std::find(updateables.begin(), updateables.end(),
                          componentName) != updateables.end())
            {
                validTargets.emplace_back(softwarePath);
            }
            else
            {
                hasAnyInvalidOrUnupdateableTarget = true;
                BMCWEB_LOG_ERROR("Unupdatable Target: {}", target);
            }
        }

        if (!validTarget)
        {
            hasAnyInvalidOrUnupdateableTarget = true;
            BMCWEB_LOG_ERROR("Invalid Target: {}", target);
        }
    }

    return hasAnyInvalidOrUnupdateableTarget;
}

/**
 * @brief Sets the OEM Firmware UpdateOption in the UpdatePolicy.
 *
 * @param asyncResp Shared pointer to the response object.
 * @param oemUpdateOption The update option ("StageOnly" or "StageAndActivate").
 * @param callback Optional callback after setting the property.
 */
inline void setOemUpdateOption(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& oemUpdateOption,
    const std::function<void()>& callback = {})
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, oemUpdateOption, callback](
            const boost::system::error_code errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            // Check if only one service implements
            // xyz.openbmc_project.Software.UpdatePolicy
            if (objInfo.size() != 1)
            {
                BMCWEB_LOG_ERROR(
                    "Expected exactly one service implementing UpdatePolicy interface, but found {} services.",
                    objInfo.size());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            std::string oemUpdateOptionNewVal;
            if (oemUpdateOption == "StageOnly")
            {
                oemUpdateOptionNewVal =
                    "xyz.openbmc_project.Software.UpdatePolicy.UpdateOptionSupport.StageOnly";
            }
            else if (oemUpdateOption == "StageAndActivate")
            {
                oemUpdateOptionNewVal =
                    "xyz.openbmc_project.Software.UpdatePolicy.UpdateOptionSupport.StageAndActivate";
            }
            sdbusplus::asio::setProperty(
                *crow::connections::systemBus, objInfo[0].first,
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Software.UpdatePolicy", "UpdateOption",
                oemUpdateOptionNewVal,
                [asyncResp, oemUpdateOption,
                 callback](const boost::system::error_code& ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("error_code = {}", ec);
                        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
                        messages::internalError(asyncResp->res);
                    }
                    if (callback)
                    {
                        callback();
                    }
                });
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software",
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

/**
 * @brief Handle update policy
 *
 * @param[in] errorCode Error code
 * @param[in] objInfo Service object
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] targets  List of valid components delivered in HTTPRequest
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return None
 */
inline void validateUpdatePolicyCallback(
    const boost::system::error_code errorCode,
    const dbus::utility::MapperServiceMap& objInfo,
    const std::shared_ptr<const crow::Request>& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<sdbusplus::message::object_path>& targets,
    const std::optional<std::string>& oemUpdateOption)
{
    if (errorCode)
    {
        BMCWEB_LOG_ERROR("validateUpdatePolicyCallback:error_code = {}",
                         errorCode);
        BMCWEB_LOG_ERROR("validateUpdatePolicyCallback:error msg = {}",
                         errorCode.message());
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }
    // Ensure we only got one service back
    if (objInfo.size() != 1)
    {
        BMCWEB_LOG_ERROR(
            "More than one service support xyz.openbmc_project.Software.UpdatePolicy. Object Size {}",
            objInfo.size());
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    crow::connections::systemBus->async_method_call(
        [req, asyncResp, objInfo,
         oemUpdateOption](const boost::system::error_code ec) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                messages::internalError(asyncResp->res);
            }
            setOemUpdateOption(
                asyncResp, oemUpdateOption.value_or("StageAndActivate"),
                [req, asyncResp]() { uploadImageFile(req, asyncResp); });
        },
        objInfo[0].first, "/xyz/openbmc_project/software",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Software.UpdatePolicy", "Targets",
        dbus::utility::DbusVariantType(targets));
}

/**
 * @brief Handle check updateable devices
 *
 * @param[in] ec Error code
 * @param[in] objPaths Object paths
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] uriTargets List of valid components delivered in HTTPRequest
 * @param[in] swInvPaths List of software inventory paths
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return None
 */
inline void areTargetsUpdateableCallback(
    const boost::system::error_code& ec,
    const std::vector<std::string>& objPaths,
    const std::shared_ptr<const crow::Request>& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::string>& uriTargets,
    const std::vector<std::string>& swInvPaths,
    const std::optional<std::string>& oemUpdateOption)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("areTargetsUpdateableCallback:error_code = {}", ec);
        BMCWEB_LOG_ERROR("areTargetsUpdateableCallback:error msg =  {}",
                         ec.message());

        BMCWEB_LOG_ERROR("Targeted devices not updateable");

        boost::urls::url_view targetURL("Target");
        messages::invalidObject(asyncResp->res, targetURL);
        return;
    }

    std::vector<std::string> updateableFw;
    for (const auto& reqFwObjPath : swInvPaths)
    {
        if (std::find(objPaths.begin(), objPaths.end(), reqFwObjPath) !=
            objPaths.end())
        {
            std::string compName =
                std::filesystem::path(reqFwObjPath).filename();
            updateableFw.push_back(compName);
        }
    }

    std::vector<sdbusplus::message::object_path> targets = {};
    // validate TargetUris if entries are present
    if (!uriTargets.empty())
    {
        if (areTargetsInvalidOrUnupdatable(uriTargets, updateableFw, swInvPaths,
                                           targets))
        {
            boost::urls::url_view targetURL("Target");
            messages::invalidObject(asyncResp->res, targetURL);
            return;
        }

        // else all targets are valid
    }

    crow::connections::systemBus->async_method_call(
        [req, asyncResp, targets, oemUpdateOption](
            const boost::system::error_code errorCode,
            const dbus::utility::MapperServiceMap& objInfo) mutable {
            validateUpdatePolicyCallback(errorCode, objInfo, req, asyncResp,
                                         targets, oemUpdateOption);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software",
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

/**
 * @brief Perfom a check to determine if the targets are updateable
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] uriTargets  List of valid components delivered in HTTPRequest
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return None
 */
inline void areTargetsUpdateable(
    const std::shared_ptr<const crow::Request>& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::string>& uriTargets,
    const std::optional<std::string>& oemUpdateOption)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, uriTargets,
         oemUpdateOption](const boost::system::error_code ec,
                          const std::vector<std::string>& swInvPaths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus responses error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            sdbusplus::asio::getProperty<std::vector<std::string>>(
                *crow::connections::systemBus,
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/software/updateable",
                "xyz.openbmc_project.Association", "endpoints",
                [req, asyncResp, uriTargets, swInvPaths,
                 oemUpdateOption](const boost::system::error_code ec1,
                                  const std::vector<std::string>& objPaths) {
                    areTargetsUpdateableCallback(ec1, objPaths, req, asyncResp,
                                                 uriTargets, swInvPaths,
                                                 oemUpdateOption);
                });
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/software/", static_cast<int32_t>(0),
        std::array<std::string, 1>{"xyz.openbmc_project.Software.Version"});
}

/**
 * @brief Handle StartUpdate D-Bus method response
 *
 * This function processes the response from the StartUpdate D-Bus method call
 * and creates a task to track the firmware update progress if successful.
 *
 * @param asyncResp Shared pointer to the async response object
 * @param payload Task payload containing update information
 * @param objectPath D-Bus object path where StartUpdate was called
 * @param ec Error code from the D-Bus method call
 * @param retPath Object path returned by StartUpdate method, used for tracking
 */
inline void handleStartUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const std::string& objectPath, const boost::system::error_code& ec,
    const sdbusplus::message::object_path& retPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("StartUpdate failed: error_code = {}", ec);
        messages::internalError(asyncResp->res);
        fwUpdateInProgress = false;
        return;
    }

    BMCWEB_LOG_INFO("StartUpdate on {} Success, retPath = {}", objectPath,
                    retPath.str);
    createTask(asyncResp, std::move(payload), retPath);
}

/**
 * @brief Call StartUpdate D-Bus method
 */
inline void startUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const std::shared_ptr<MemoryFileDescriptor>& memfd,
    const std::string& applyTime, bool forceUpdate,
    const std::vector<sdbusplus::message::object_path>& targets)
{
    // PLDM UA is the only service implementing StartUpdate
    const std::string serviceName = "xyz.openbmc_project.PLDM";
    const std::string objectPath = "/xyz/openbmc_project/software/pldm";

    crow::connections::systemBus->async_method_call(
        [asyncResp, payload = std::move(payload), memfd,
         objectPath](const boost::system::error_code& ec1,
                     const sdbusplus::message::object_path& retPath) mutable {
            handleStartUpdate(asyncResp, std::move(payload), objectPath, ec1,
                              retPath);
        },
        serviceName, objectPath, "xyz.openbmc_project.Software.Update",
        "StartUpdate", sdbusplus::message::unix_fd(memfd->fd), applyTime,
        forceUpdate, targets);
}

/**
 * @brief Process update request - write firmware to memfd and call StartUpdate
 */
inline void processUpdateRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    task::Payload&& payload, const crow::Request& req,
    const std::string& dbusApplyTime, bool forceUpdate,
    const std::vector<std::string>& uriTargets)
{
    auto memfd = std::make_shared<MemoryFileDescriptor>("update-image");
    if (memfd->fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to create image memfd");
        messages::internalError(asyncResp->res);
        fwUpdateInProgress = false;
        return;
    }

    MultipartParser parser(memfd->fd);
    ParserError parseResult = parser.parse(req);
    if (parseResult != ParserError::PARSER_SUCCESS)
    {
        BMCWEB_LOG_ERROR(
            "Failed to parse multipart with direct memfd write: {}",
            static_cast<int>(parseResult));
        messages::internalError(asyncResp->res);
        fwUpdateInProgress = false;
        return;
    }

    if (!memfd->rewind())
    {
        messages::internalError(asyncResp->res);
        fwUpdateInProgress = false;
        return;
    }

    if (!uriTargets.empty())
    {
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/software", 0,
            std::array<std::string_view, 1>{
                "xyz.openbmc_project.Software.Version"},
            [asyncResp, payload = std::move(payload), memfd, dbusApplyTime,
             forceUpdate,
             uriTargets](const boost::system::error_code& ec,
                         const std::vector<std::string>& swInvPaths) mutable {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Failed to get software inventory: {}",
                                     ec);
                    messages::internalError(asyncResp->res);
                    fwUpdateInProgress = false;
                    return;
                }

                std::vector<sdbusplus::message::object_path> validTargets;
                std::vector<std::string> updateableFw;
                updateableFw.reserve(swInvPaths.size());
                for (const auto& path : swInvPaths)
                {
                    std::string fwId = std::filesystem::path(path).filename();
                    updateableFw.push_back(fwId);
                }

                if (areTargetsInvalidOrUnupdatable(uriTargets, updateableFw,
                                                   swInvPaths, validTargets))
                {
                    BMCWEB_LOG_ERROR("Invalid targets provided");
                    messages::invalidObject(asyncResp->res,
                                            boost::urls::url_view("Targets"));
                    fwUpdateInProgress = false;
                    return;
                }

                startUpdate(asyncResp, std::move(payload), memfd, dbusApplyTime,
                            forceUpdate, validTargets);
            });
    }
    else
    {
        std::vector<sdbusplus::message::object_path> emptyTargets{};
        startUpdate(asyncResp, std::move(payload), memfd, dbusApplyTime,
                    forceUpdate, emptyTargets);
    }
}

/**
 * @brief Process multipart form data
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] parser  MultipartParser
 *
 * @return None
 */
inline void updateMultipartContext(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const MultipartParser& parser)
{
    std::optional<MultiPartUpdate> multipart =
        extractMultipartUpdateParameters(asyncResp, parser);
    if (!multipart)
    {
        return;
    }

    if (!multipart->params.applyTime)
    {
        multipart->params.applyTime = "Immediate";
    }

    std::string dbusApplyTime;
    if (!convertApplyTime(asyncResp, *multipart->params.applyTime,
                          dbusApplyTime))
    {
        return;
    }

    std::vector<std::string> uriTargets;
    if (multipart->params.targets.has_value())
    {
        uriTargets = *multipart->params.targets;
    }

    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        bool updateAll = false;
        uint8_t count = 0;
        std::string rfaPrefix = std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX);
        if (!uriTargets.empty())
        {
            for (const auto& uri : uriTargets)
            {
                std::string file = std::filesystem::path(uri).filename();
                std::string prefix = rfaPrefix + "_";
                if (file.starts_with(prefix))
                {
                    count++;
                }

                auto parsed = boost::urls::parse_relative_ref(uri);
                if (!parsed)
                {
                    BMCWEB_LOG_ERROR("Couldn't parse URI from resource {}",
                                     uri);
                    return;
                }

                const boost::urls::url_view& thisUrl = *parsed;

                // this is the Chassis resource from satellite BMC for all
                // component firmware update.
                if (crow::utility::readUrlSegments(
                        thisUrl, "redfish", "v1", "Chassis",
                        std::string(BMCWEB_RFA_HMC_UPDATE_TARGET)))
                {
                    updateAll = true;
                }
            }
            // There is one URI at least for satellite BMC.
            if (count > 0)
            {
                // further check if there is mixed targets and some are not
                // for satellite BMC.
                if (count != uriTargets.size())
                {
                    boost::urls::url_view targetURL("Target");
                    messages::invalidObject(asyncResp->res, targetURL);
                }
                else
                {
                    // All URIs in Target has the prepended prefix
                    BMCWEB_LOG_ERROR("forward image {}", uriTargets[0]);
                    auto sharedReq = std::make_shared<crow::Request>(req);
                    RedfishAggregator::getSatelliteConfigs(std::bind_front(
                        forwardImage, sharedReq, updateAll, asyncResp));
                }
                return;
            }
        }
        // the update request is for BMC so only allow one FW update at a time
        if (fwUpdateInProgress)
        {
            if (asyncResp)
            {
                // don't copy the image, update already in progress.
                std::string resolution =
                    "Another update is in progress. Retry"
                    " the update operation once it is complete.";
                redfish::messages::updateInProgressMsg(asyncResp->res,
                                                       resolution);
                BMCWEB_LOG_ERROR("Update already in progress.");
            }
            return;
        }
    }

    if (fwUpdateInProgress)
    {
        BMCWEB_LOG_ERROR("Update already in progress");
        messages::serviceTemporarilyUnavailable(asyncResp->res, "30");
        return;
    }

    fwUpdateInProgress = true;

    task::Payload payload(req);
    processUpdateRequest(asyncResp, std::move(payload), req, dbusApplyTime,
                         multipart->params.forceUpdate.value_or(false),
                         uriTargets);
}

/**
 * @brief POST handler for Multipart Update Service
 *
 * @param[in] app App
 * @param[in] req  HTTP request
 * @param[in] asyncResp  Pointer to object holding response data
 *
 * @return None
 */
inline void handleUpdateServiceMultipartUpdatePost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG(
        "Execute HTTP POST method '/redfish/v1/UpdateService/update-multipart/'");

    bool enableFWInProgCheck = true;
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        // This is the flag to check BMC firmware update.
        // Parse the multipart payload and then learn satBMC or BMC firmware
        // update So UpdateInProgress will be checking at the later stage.
        enableFWInProgCheck = false;
    }

    if (!preCheckMultipartUpdateServiceReq(req, asyncResp, enableFWInProgCheck))
    {
        return;
    }

    std::string_view contentType = req.getHeaderValue("Content-Type");
    if (!contentType.starts_with("multipart/form-data"))
    {
        BMCWEB_LOG_DEBUG("Bad content type specified:{}", contentType);
        asyncResp->res.result(boost::beast::http::status::bad_request);
        return;
    }

    MultipartParser parser(true);
    ParserError ec = parser.parse(req);
    if (ec != ParserError::PARSER_SUCCESS)
    {
        BMCWEB_LOG_ERROR("MIME parse failed, ec : {}", static_cast<int>(ec));
        messages::internalError(asyncResp->res);
        return;
    }

    updateMultipartContext(req, asyncResp, parser);
}

inline void handleUpdateServiceGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#UpdateService.v1_11_1.UpdateService";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/UpdateService";
    asyncResp->res.jsonValue["Id"] = "UpdateService";
    asyncResp->res.jsonValue["Description"] = "Service for Software Update";
    asyncResp->res.jsonValue["Name"] = "Update Service";

    asyncResp->res.jsonValue["HttpPushUri"] =
        "/redfish/v1/UpdateService/update";
    asyncResp->res.jsonValue["MultipartHttpPushUri"] =
        "/redfish/v1/UpdateService/update-multipart";

    // UpdateService cannot be disabled
    asyncResp->res.jsonValue["ServiceEnabled"] = true;
    asyncResp->res.jsonValue["FirmwareInventory"]["@odata.id"] =
        "/redfish/v1/UpdateService/FirmwareInventory";
    // Get the MaxImageSizeBytes
    asyncResp->res.jsonValue["MaxImageSizeBytes"] = firmwareImageLimitBytes;

    extendUpdateServiceGet(asyncResp);

    if constexpr (BMCWEB_REDFISH_ALLOW_SIMPLE_UPDATE)
    {
        // Update Actions object.
        nlohmann::json& updateSvcSimpleUpdate =
            asyncResp->res.jsonValue["Actions"]["#UpdateService.SimpleUpdate"];
        updateSvcSimpleUpdate["target"] =
            "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate";

        nlohmann::json::array_t allowed;
        allowed.emplace_back(update_service::TransferProtocolType::HTTPS);
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] =
            std::move(allowed);
    }

    asyncResp->res
        .jsonValue["HttpPushUriOptions"]["HttpPushUriApplyTime"]["ApplyTime"] =
        update_service::ApplyTime::Immediate;
}

inline void handleUpdateServiceFirmwareInventoryCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#SoftwareInventoryCollection.SoftwareInventoryCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/UpdateService/FirmwareInventory";
    asyncResp->res.jsonValue["Name"] = "Software Inventory Collection";
    const std::array<const std::string_view, 1> iface = {
        "xyz.openbmc_project.Software.Version"};

    redfish::collection_util::getCollectionMembers(
        asyncResp,
        boost::urls::url("/redfish/v1/UpdateService/FirmwareInventory"), iface,
        "/xyz/openbmc_project/software");
}

/* Fill related item links (i.e. bmc, bios) in for inventory */
inline static void getRelatedItems(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId, const std::string& purpose)
{
    if (purpose == sw_util::otherPurpose || purpose == sw_util::bmcPurpose)
    {
        getRelatedItemsOthers(asyncResp, swId);
    }
    else if (purpose == sw_util::biosPurpose)
    {
        nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
        nlohmann::json::object_t item;
        item["@odata.id"] = std::format("/redfish/v1/Systems/{}/Bios",
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
        relatedItem.emplace_back(std::move(item));
        asyncResp->res.jsonValue["RelatedItem@odata.count"] =
            relatedItem.size();
    }
    else
    {
        BMCWEB_LOG_ERROR("Unknown software purpose {}", purpose);
    }
}

inline void getSoftwareVersion(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& swId)
{
    dbus::utility::getAllProperties(
        service, path, "xyz.openbmc_project.Software.Version",
        [asyncResp,
         swId](const boost::system::error_code& ec,
               const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* swInvPurpose = nullptr;
            const std::string* version = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList, "Purpose",
                swInvPurpose, "Version", version);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (swInvPurpose == nullptr)
            {
                BMCWEB_LOG_ERROR("Can't find property \"Purpose\"!");
                messages::internalError(asyncResp->res);
                return;
            }

            BMCWEB_LOG_ERROR("swInvPurpose = {}", *swInvPurpose);

            if (version == nullptr)
            {
                BMCWEB_LOG_ERROR("Can't find property \"Version\"!");

                messages::internalError(asyncResp->res);

                return;
            }
            asyncResp->res.jsonValue["Version"] = *version;
            asyncResp->res.jsonValue["Id"] = swId;

            // swInvPurpose is of format:
            // xyz.openbmc_project.Software.Version.VersionPurpose.ABC
            // Translate this to "ABC image"
            size_t endDesc = swInvPurpose->rfind('.');
            if (endDesc == std::string::npos)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            endDesc++;
            if (endDesc >= swInvPurpose->size())
            {
                messages::internalError(asyncResp->res);
                return;
            }

            std::string formatDesc = swInvPurpose->substr(endDesc);
            asyncResp->res.jsonValue["Description"] = formatDesc + " image";
            getRelatedItems(asyncResp, swId, *swInvPurpose);
        });
}

inline void handleUpdateServiceFirmwareInventoryGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::shared_ptr<std::string> swId = std::make_shared<std::string>(param);

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/UpdateService/FirmwareInventory/{}", *swId);

    constexpr std::array<std::string_view, 4> interfaces = {
        "xyz.openbmc_project.Software.Version",
        "xyz.openbmc_project.Software.Settings",
        "xyz.openbmc_project.State.Decorator.Health",
        "xyz.openbmc_project.State.Decorator.OperationalStatus"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/software/", 0, interfaces,
        [asyncResp, swId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            BMCWEB_LOG_DEBUG("doGet callback...");
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Ensure we find our input swId, otherwise return an
            // error
            bool foundVersionObject = false;
            bool foundStatusObject = false;
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     obj : subtree)
            {
                sdbusplus::message::object_path objPath(obj.first);
                if (!boost::equals(objPath.filename(), *swId))
                {
                    continue;
                }

                if (obj.second.empty())
                {
                    continue;
                }

                foundVersionObject = true;

                std::string settingService{};
                std::string versionService{};
                std::string statusService{};
                for (const auto& [service, interface] : obj.second)
                {
                    if (std::ranges::find(
                            interface,
                            "xyz.openbmc_project.Software.Settings") !=
                        interface.end())
                    {
                        settingService = service;
                    }
                    if (std::ranges::find(
                            interface,
                            "xyz.openbmc_project.Software.Version") !=
                        interface.end())
                    {
                        versionService = service;
                    }
                    if (std::ranges::find(
                            interface,
                            "xyz.openbmc_project.State.Decorator.Health") !=
                            interface.end() and
                        std::ranges::find(
                            interface,
                            "xyz.openbmc_project.State.Decorator.OperationalStatus") !=
                            interface.end())
                    {
                        statusService = service;
                        foundStatusObject = true;
                    }
                }

                if (versionService.empty() and statusService.empty())
                {
                    BMCWEB_LOG_ERROR(
                        "Firmware Inventory: Software.Version interface is missing for swId: {}",
                        *swId);
                    messages::internalError(asyncResp->res);
                    return;
                }

                fw_util::getFwStatus(asyncResp, swId, obj.second[0].first);
                // The settingService is used for populating
                // WriteProtected property. This property is optional
                // and not implemented on all devices.
                if (!settingService.empty())
                {
                    fw_util::getFwWriteProtectedStatus(asyncResp, obj.first,
                                                       settingService);
                }
                asyncResp->res.jsonValue["Id"] = *swId;

                if (!versionService.empty())
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp,
                         swId](const boost::system::error_code errorCode,
                               const boost::container::flat_map<
                                   std::string, dbus::utility::DbusVariantType>&
                                   propertiesList) {
                            if (errorCode)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            boost::container::flat_map<
                                std::string,
                                dbus::utility::DbusVariantType>::const_iterator
                                it = propertiesList.find("Purpose");
                            if (it == propertiesList.end())
                            {
                                BMCWEB_LOG_ERROR(
                                    "Can't find property \"Version\"!");
                                messages::propertyMissing(asyncResp->res,
                                                          "Purpose");
                                return;
                            }
                            const std::string* swInvPurpose =
                                std::get_if<std::string>(&it->second);
                            if (swInvPurpose == nullptr)
                            {
                                BMCWEB_LOG_ERROR(
                                    "wrong types for property\"Purpose\"!");
                                messages::propertyValueTypeError(asyncResp->res,
                                                                 "", "Purpose");
                                return;
                            }

                            BMCWEB_LOG_DEBUG("swInvPurpose = {}",
                                             *swInvPurpose);
                            it = propertiesList.find("Version");
                            if (it == propertiesList.end())
                            {
                                BMCWEB_LOG_ERROR(
                                    "Can't find property \"Version\"!");
                                messages::propertyMissing(asyncResp->res,
                                                          "Version");
                                return;
                            }

                            BMCWEB_LOG_DEBUG("Version found!");

                            const std::string* version =
                                std::get_if<std::string>(&it->second);

                            if (version == nullptr)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Can't find property \"Version\"!");

                                messages::propertyValueTypeError(asyncResp->res,
                                                                 "", "Version");
                                return;
                            }

                            it = propertiesList.find("Manufacturer");
                            if (it != propertiesList.end())
                            {
                                const std::string* manufacturer =
                                    std::get_if<std::string>(&it->second);

                                if (manufacturer == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Can't find property \"Manufacturer\"!");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue["Manufacturer"] =
                                    *manufacturer;
                            }

                            it = propertiesList.find("SoftwareId");
                            if (it != propertiesList.end())
                            {
                                const std::string* softwareId =
                                    std::get_if<std::string>(&it->second);

                                if (softwareId == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Can't find property \"softwareId\"!");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                if (!softwareId->empty())
                                {
                                    asyncResp->res.jsonValue["SoftwareId"] =
                                        *softwareId;
                                }
                            }

                            asyncResp->res.jsonValue["Version"] = *version;

                            // swInvPurpose is of format:
                            // xyz.openbmc_project.Software.Version.VersionPurpose.ABC
                            // Translate this to "ABC image"
                            size_t endDesc = swInvPurpose->rfind('.');
                            if (endDesc == std::string::npos)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            endDesc++;
                            if (endDesc >= swInvPurpose->size())
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            std::string formatDesc =
                                swInvPurpose->substr(endDesc);
                            it = propertiesList.find("Description");
                            asyncResp->res.jsonValue["Description"] =
                                formatDesc + " image";

                            if (it != propertiesList.end())
                            {
                                const std::string* description =
                                    std::get_if<std::string>(&it->second);
                                if (description != nullptr &&
                                    !description->empty())
                                {
                                    asyncResp->res.jsonValue["Description"] =
                                        *description;
                                }
                            }
                            getRelatedItems(asyncResp, *swId, *swInvPurpose);

                            it = propertiesList.find("PrettyName");
                            if (it != propertiesList.end())
                            {
                                const std::string* foundName =
                                    std::get_if<std::string>(&it->second);
                                if (foundName != nullptr && !foundName->empty())
                                {
                                    asyncResp->res.jsonValue["Name"] =
                                        *foundName;
                                }
                            }
                        },
                        versionService, obj.first,
                        "org.freedesktop.DBus.Properties", "GetAll", "");
                }

                asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
                }
                if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                {
                    asyncResp->res.jsonValue["Status"]["Conditions"] =
                        nlohmann::json::array();
                }

                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if constexpr (BMCWEB_NVIDIA_OEM_FW_UPDATE_STAGING)
                    {
                        redfish::fw_util::getFWSlotInformation(asyncResp,
                                                               obj.first);
                    }
                }
                if (!statusService.empty())
                {
                    fw_util::getFwRecoveryStatus(asyncResp, swId,
                                                 statusService);
                }
                else
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                }
            }

            if (!foundVersionObject and !foundStatusObject)
            {
                BMCWEB_LOG_DEBUG("Input swID {} not found!", *swId);
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/UpdateService/FirmwareInventory/{}",
                        *swId));
                return;
            }

            if (foundVersionObject)
            {
                asyncResp->res.jsonValue["Updateable"] = false;
                fw_util::getFwUpdateableStatus(asyncResp, swId);
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#SoftwareInventory.v1_4_0.SoftwareInventory";
            asyncResp->res.jsonValue["Name"] = "Software Inventory";

            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                updateOemActionComputeDigest(asyncResp, *swId);
            }
        });
}

inline void requestRoutesUpdateService(App& app)
{
    if constexpr (BMCWEB_REDFISH_ALLOW_SIMPLE_UPDATE)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate/")
            .privileges(redfish::privileges::postUpdateService)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleUpdateServiceSimpleUpdateAction, std::ref(app)));
    }
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceFirmwareInventoryGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/")
        .privileges(redfish::privileges::getUpdateService)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUpdateServiceGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/update-multipart/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleUpdateServiceMultipartUpdatePost, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceFirmwareInventoryCollectionGet, std::ref(app)));
}

} // namespace redfish
