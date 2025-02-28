// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "multipart_parser.hpp"
#include "nvidia_update_service.hpp"
#include "ossl_random.hpp"
#include "persistentstorage_util.hpp"
#include "query.hpp"
#include "redfish_aggregator.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"

#include <sys/mman.h>

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <http_client.hpp>
#include <http_connection.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <update_messages.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/fw_utils.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace redfish
{

// Match signals added on software path
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateMatcher;
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

struct MultiPartUpdateParameters
{
    std::optional<std::string> applyTime;
    std::string uploadData;
    std::vector<std::string> targets;
};

inline void cleanUp()
{
    fwUpdateInProgress = false;
    fwUpdateMatcher = nullptr;
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
                BMCWEB_LOG_DEBUG("error_code = {}", ec);
                BMCWEB_LOG_DEBUG("error msg = {}", ec.message());
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
    for (auto interface : interfacesProperties)
    {
        if (interface.first == "xyz.openbmc_project.Logging.Entry")
        {
            std::string rfMessage;
            std::string resolution;
            std::string messageNamespace;
            std::vector<std::string> rfArgs;
            const std::vector<std::string>* vData = nullptr;
            for (auto& propertyMap : interface.second)
            {
                if (propertyMap.first == "AdditionalData")
                {
                    vData = std::get_if<std::vector<std::string>>(
                        &propertyMap.second);

                    for (const auto& kv : *vData)
                    {
                        std::vector<std::string> fields;
                        bmcweb::split(fields, kv, '=');
                        if (fields[0] == "REDFISH_MESSAGE_ID")
                        {
                            rfMessage = fields[1];
                        }
                        else if (fields[0] == "REDFISH_MESSAGE_ARGS")
                        {
                            bmcweb::split(rfArgs, fields[1], ',');
                        }
                        else if (fields[0] == "namespace")
                        {
                            messageNamespace = fields[1];
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
            if (vData == nullptr || messageNamespace != "FWUpdate")
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
                            BMCWEB_LOG_DEBUG("error_code = {}", ec);
                            BMCWEB_LOG_DEBUG("error msg = {}", ec.message());
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

                    if (asyncResp)
                    {
                        createTask(asyncResp, std::move(payload), objPath);
                    }
                    activateImage(objPath.str, objInfo[0].first);
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

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
inline void monitorForSoftwareAvailable(
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

    BMCWEB_LOG_DEBUG("Enter UpdateService.SimpleUpdate doPost");

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
        BMCWEB_LOG_DEBUG("Missing TransferProtocol or ImageURI parameter");
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

    BMCWEB_LOG_DEBUG("Exit UpdateService.SimpleUpdate doPost");
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

    BMCWEB_LOG_DEBUG("Writing file to {}", filepath.string());
    std::ofstream out(filepath, std::ofstream::out | std::ofstream::binary |
                                    std::ofstream::trunc);
    // set the permission of the file to 640
    std::filesystem::perms permission =
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read;
    std::filesystem::permissions(filepath, permission);

    MultipartParser parser;
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
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");

        size_t index = it->value().find(';');
        if (index == std::string::npos)
        {
            continue;
        }

        for (const auto& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateFile")
            {
                hasUpdateFile = true;
                out << formpart.content;

                if (out.bad())
                {
                    BMCWEB_LOG_ERROR("Error writing to file: {}",
                                     filepath.string());
                    messages::internalError(asyncResp->res);
                    cleanUp();
                }
            }
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR("File with firmware image is missing.");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
    }
}

// Convert the Request Apply Time to the D-Bus value
inline bool convertApplyTime(crow::Response& res, const std::string& applyTime,
                             std::string& applyTimeNewVal)
{
    if (applyTime == "Immediate")
    {
        applyTimeNewVal =
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate";
    }
    else if (applyTime == "OnReset")
    {
        applyTimeNewVal =
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.OnReset";
    }
    else
    {
        BMCWEB_LOG_WARNING(
            "ApplyTime value {} is not in the list of acceptable values",
            applyTime);
        messages::propertyValueNotInList(res, applyTime, "ApplyTime");
        return false;
    }
    return true;
}

inline std::optional<std::string>
    processUrl(boost::system::result<boost::urls::url_view>& url)
{
    if (!url)
    {
        return std::nullopt;
    }
    if (crow::utility::readUrlSegments(*url, "redfish", "v1", "Managers",
                                       BMCWEB_REDFISH_MANAGER_URI_NAME))
    {
        return std::make_optional(std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
    }
    if constexpr (!BMCWEB_REDFISH_UPDATESERVICE_USE_DBUS)
    {
        return std::nullopt;
    }
    std::string firmwareId;
    if (!crow::utility::readUrlSegments(*url, "redfish", "v1", "UpdateService",
                                        "FirmwareInventory",
                                        std::ref(firmwareId)))
    {
        return std::nullopt;
    }

    return std::make_optional(firmwareId);
}

inline void handleStartUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const std::string& objectPath, const boost::system::error_code& ec,
    const sdbusplus::message::object_path& retPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    BMCWEB_LOG_INFO("Call to StartUpdate on {} Success, retPath = {}",
                    objectPath, retPath.str);
    createTask(asyncResp, std::move(payload), retPath);
}

inline void startUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const MemoryFileDescriptor& memfd, const std::string& applyTime,
    const std::string& objectPath, const std::string& serviceName)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, payload = std::move(payload),
         objectPath](const boost::system::error_code& ec1,
                     const sdbusplus::message::object_path& retPath) mutable {
            handleStartUpdate(asyncResp, std::move(payload), objectPath, ec1,
                              retPath);
        },
        serviceName, objectPath, "xyz.openbmc_project.Software.Update",
        "StartUpdate", sdbusplus::message::unix_fd(memfd.fd), applyTime);
}

inline void getSwInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      task::Payload payload, const MemoryFileDescriptor& memfd,
                      const std::string& applyTime, const std::string& target,
                      const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree1)
{
    using SwInfoMap = std::unordered_map<
        std::string, std::pair<sdbusplus::message::object_path, std::string>>;
    SwInfoMap swInfoMap;

    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_DEBUG("Found {} software version paths", subtree1.size());

    for (const auto& entry : subtree1)
    {
        sdbusplus::message::object_path path(entry.first);
        std::string swId = path.filename();
        swInfoMap.emplace(swId, make_pair(path, entry.second[0].first));
    }

    auto swEntry = swInfoMap.find(target);
    if (swEntry == swInfoMap.end())
    {
        BMCWEB_LOG_WARNING("No valid DBus path for Target URI {}", target);
        messages::propertyValueFormatError(asyncResp->res, target, "Targets");
        return;
    }

    BMCWEB_LOG_DEBUG("Found software version path {} serviceName {}",
                     swEntry->second.first.str, swEntry->second.second);

    startUpdate(asyncResp, std::move(payload), memfd, applyTime,
                swEntry->second.first.str, swEntry->second.second);
}

inline void handleBMCUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const MemoryFileDescriptor& memfd, const std::string& applyTime,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& functionalSoftware)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    if (functionalSoftware.size() != 1)
    {
        BMCWEB_LOG_ERROR("Found {} functional software endpoints",
                         functionalSoftware.size());
        messages::internalError(asyncResp->res);
        return;
    }

    startUpdate(asyncResp, std::move(payload), memfd, applyTime,
                functionalSoftware[0], "xyz.openbmc_project.Software.Manager");
}

inline void processUpdateRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    task::Payload&& payload, std::string_view body,
    const std::string& applyTime, [[maybe_unused]] std::vector<std::string>& targets)
{
    MemoryFileDescriptor memfd("update-image");
    if (memfd.fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to create image memfd");
        messages::internalError(asyncResp->res);
        return;
    }
    if (write(memfd.fd, body.data(), body.length()) !=
        static_cast<ssize_t>(body.length()))
    {
        BMCWEB_LOG_ERROR("Failed to write to image memfd");
        messages::internalError(asyncResp->res);
        return;
    }
    if (!memfd.rewind())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    startUpdate(asyncResp, std::move(payload), memfd, applyTime,
                "/xyz/openbmc_project/software/manager",
                "xyz.openbmc_project.Software.Update.Manager");
}

/**
 * @brief Check multipart update form UpdateParameters
 *
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] hasUpdateParameters true when 'UpdateParameters' is added
 * to HTTPRequest
 * @param[in] applyTime Operation Apply Time
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return It returns true when the form section 'UpdateParameters' contains the
 * required parameters.
 */
inline bool validateUpdateParametersFormData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    bool hasUpdateParameters, std::optional<std::string>& applyTime,
    std::optional<std::string>& oemUpdateOption)
{
    if (!hasUpdateParameters)
    {
        BMCWEB_LOG_INFO("UpdateParameters parameter is missing");

        messages::actionParameterMissing(asyncResp->res, "update-multipart",
                                         "UpdateParameters");

        return false;
    }

    if (applyTime)
    {
        std::string allowedApplyTime = "Immediate";
        if (allowedApplyTime != *applyTime)
        {
            BMCWEB_LOG_INFO(
                "ApplyTime value is not in the list of acceptable values");

            messages::propertyValueNotInList(asyncResp->res, *applyTime,
                                             "@Redfish.OperationApplyTime");

            return false;
        }
    }
    if (oemUpdateOption)
    {
        if (oemUpdateOption != "StageOnly" and
            oemUpdateOption != "StageAndActivate")
        {
            BMCWEB_LOG_ERROR(
                "Update option value {} is not in the list of acceptable values",
                *oemUpdateOption);
            messages::propertyValueNotInList(asyncResp->res, *oemUpdateOption,
                                             "UpdateOption");
            return false;
        }
    }

    return true;
}

/**
 * @brief Check multipart update form UpdateFile
 *
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] hasFile true when 'UpdateFile' is added to HTTPRequest
 *
 * @return It returns true when the form section 'UpdateFile' contains the
 * required parameters.
 */
inline bool validateUpdateFileFormData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const bool hasFile)
{
    if (!hasFile)
    {
        BMCWEB_LOG_ERROR("Upload data is NULL");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
        return false;
    }

    return true;
}

/**
 * @brief Forward firmware image to the satellite BMC
 *
 * @param[in] req  HTTP request.
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] ec the error code returned by Dbus call.
 * @param[in] satelliteInfo the map containing the satellite controllers
 *
 * @return None
 */
inline void forwardImage(
    crow::Request& req, const MultipartParser& parser, const bool updateAll,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    // Something went wrong while querying dbus
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat =
        satelliteInfo.find(std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX));
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(handleSatBMCResponse, asyncResp);

    bool hasUpdateFile = false;
    std::string data;
    std::string_view boundary(parser.boundary);
    for (const FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");

        size_t index = it->value().find(';');
        if (index == std::string::npos)
        {
            continue;
        }
        // skip \r\n and get the boundary
        data += boundary.substr(2);
        data += "\r\n";
        data += "Content-Disposition:";
        data += formpart.fields.at("Content-Disposition");
        data += "\r\n";

        for (const auto& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateFile")
            {
                data += "Content-Type: application/octet-stream\r\n\r\n";
                data += formpart.content;
                data += "\r\n";
                hasUpdateFile = true;
            }
            else if (param.second == "UpdateParameters")
            {
                data += "Content-Type: application/json\r\n\r\n";
                nlohmann::json content =
                    nlohmann::json::parse(formpart.content, nullptr, false);
                if (content.is_discarded())
                {
                    BMCWEB_LOG_INFO("UpdateParameters parse error:{}",
                                    formpart.content);
                    continue;
                }
                std::optional<std::vector<std::string>> targets;
                std::optional<bool> forceUpdate;

                json_util::readJson(content, asyncResp->res, "Targets", targets,
                                    "ForceUpdate", forceUpdate);

                nlohmann::json paramJson = nlohmann::json::object();

                const std::string urlPrefix =
                    std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX);
                // individual components update
                if (targets && !updateAll)
                {
                    paramJson["Targets"] = nlohmann::json::array();

                    for (auto& uri : *targets)
                    {
                        // the special handling for Gb200Nvl System.
                        // we don't remove the prefix if the resource's
                        // prefix from FirmwareInventory is the same with
                        // RFA prefix.
                        if constexpr (BMCWEB_REDFISH_AGGREGATION_PREFIX_REMOVAL)
                        {
                            // remove prefix before the update request is
                            // forwarded.
                            size_t pos = uri.find(urlPrefix + "_");
                            if (pos != std::string::npos)
                            {
                                uri.erase(pos, urlPrefix.size() + 1);
                            }
                        }
                        BMCWEB_LOG_DEBUG("uri in Targets: {}", uri);
                        paramJson["Targets"].push_back(uri);
                    }
                }
                if (forceUpdate)
                {
                    paramJson["ForceUpdate"] = *forceUpdate;
                }
                data += paramJson.dump();
                data += "\r\n";
                BMCWEB_LOG_DEBUG("form data: {}", data);
            }
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR("File with firmware image is missing.");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
    }
    else
    {
        data += boundary.substr(2);
        data += "--\r\n";

        boost::urls::url url(sat->second);
        url.set_path(req.url().path());
        // Remove headers not handled for RFA firmware upgrade flow
        if (!req.getHeaderValue("Expect").empty())
        {
            BMCWEB_LOG_INFO("Removed Expect header from the request");
            req.clearHeader(boost::beast::http::field::expect);
        }
        BMCWEB_LOG_INFO("Expect header value {}", req.getHeaderValue("Expect"));
        client.sendDataWithCallback(
            std::move(data), url, ensuressl::VerifyCertificate::Verify,
            req.fields(), boost::beast::http::verb::post, cb);
    }
}

inline std::optional<MultiPartUpdateParameters>
    extractMultipartUpdateParameters(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        MultipartParser parser)
{
    MultiPartUpdateParameters multiRet;
    for (FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            BMCWEB_LOG_ERROR("Couldn't find Content-Disposition");
            return std::nullopt;
        }
        BMCWEB_LOG_INFO("Parsing value {}", it->value());

        // The construction parameters of param_list must start with `;`
        size_t index = it->value().find(';');
        if (index == std::string::npos)
        {
            continue;
        }

        for (const auto& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateParameters")
            {
                std::vector<std::string> tempTargets;
                nlohmann::json content =
                    nlohmann::json::parse(formpart.content, nullptr, false);
                if (content.is_discarded())
                {
                    return std::nullopt;
                }
                nlohmann::json::object_t* obj =
                    content.get_ptr<nlohmann::json::object_t*>();
                if (obj == nullptr)
                {
                    messages::propertyValueTypeError(
                        asyncResp->res, formpart.content, "UpdateParameters");
                    return std::nullopt;
                }

                if (!json_util::readJsonObject(                            //
                        *obj, asyncResp->res,                              //
                        "@Redfish.OperationApplyTime", multiRet.applyTime, //
                        "Targets", tempTargets                             //
                        ))
                {
                    return std::nullopt;
                }

                for (size_t urlIndex = 0; urlIndex < tempTargets.size();
                     urlIndex++)
                {
                    const std::string& target = tempTargets[urlIndex];
                    boost::system::result<boost::urls::url_view> url =
                        boost::urls::parse_origin_form(target);
                    auto res = processUrl(url);
                    if (!res.has_value())
                    {
                        messages::propertyValueFormatError(
                            asyncResp->res, target,
                            std::format("Targets/{}", urlIndex));
                        return std::nullopt;
                    }
                    multiRet.targets.emplace_back(res.value());
                }
                if (multiRet.targets.size() != 1)
                {
                    messages::propertyValueFormatError(
                        asyncResp->res, multiRet.targets, "Targets");
                    return std::nullopt;
                }
            }
            else if (param.second == "UpdateFile")
            {
                multiRet.uploadData = std::move(formpart.content);
            }
        }
    }

    if (multiRet.uploadData.empty())
    {
        BMCWEB_LOG_ERROR("Upload data is NULL");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
        return std::nullopt;
    }
    if (multiRet.targets.empty())
    {
        messages::propertyMissing(asyncResp->res, "Targets");
        return std::nullopt;
    }
    return multiRet;
}

inline void
    updateMultipartContext(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const crow::Request& req, MultipartParser&& parser)
{
    std::optional<MultiPartUpdateParameters> multipart =
        extractMultipartUpdateParameters(asyncResp, std::move(parser));
    if (!multipart)
    {
        return;
    }
    if (!multipart->applyTime)
    {
        multipart->applyTime = "OnReset";
    }

    std::string applyTimeNewVal;
    if (!convertApplyTime(asyncResp->res, *multipart->applyTime,
                          applyTimeNewVal))
    {
        return;
    }
    task::Payload payload(req);

    processUpdateRequest(
        asyncResp, std::move(payload), multipart->uploadData,
        "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate",
        multipart->targets);
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

    std::error_code spaceInfoError;
    const std::filesystem::space_info spaceInfo = std::filesystem::space(
        std::string(BMCWEB_UPDATE_SERVICE_IMAGE_LOCATION), spaceInfoError);
    if (!spaceInfoError)
    {
        if (spaceInfo.free < req.body().size())
        {
            BMCWEB_LOG_ERROR(
                "Insufficient storage space. Required: {} Available: {}",
                req.body().size(), spaceInfo.free);
            // std::string resolution =
            //     "Reset the baseboard and retry the operation.";
            messages::insufficientStorage(asyncResp->res);
            return false;
        }
    }

    return true;
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
inline void handleMultipartUpdateServicePost(
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

    MultipartParser parser;
    ParserError ec = parser.parse(req);
    if (ec == ParserError::ERROR_BOUNDARY_FORMAT)
    {
        BMCWEB_LOG_ERROR("The request has unsupported media type");
        messages::unsupportedMediaType(asyncResp->res);

        return;
    }
    if (ec != ParserError::PARSER_SUCCESS)
    {
        // handle error
        BMCWEB_LOG_ERROR("MIME parse failed, ec : {}", static_cast<int>(ec));
        messages::internalError(asyncResp->res);
        return;
    }
    updateMultipartContext(asyncResp, req, std::move(parser));
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
        "#UpdateService.v1_11_0.UpdateService";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/UpdateService";
    asyncResp->res.jsonValue["Id"] = "UpdateService";
    asyncResp->res.jsonValue["Description"] = "Service for Software Update";
    asyncResp->res.jsonValue["Name"] = "Update Service";

    asyncResp->res.jsonValue["HttpPushUri"] =
        "/redfish/v1/UpdateService/update";

    // UpdateService cannot be disabled
    asyncResp->res.jsonValue["ServiceEnabled"] = true;

    asyncResp->res.jsonValue["MultipartHttpPushUri"] =
        "/redfish/v1/UpdateService/update-multipart";

    const nlohmann::json operationApplyTimeSupportedValues = {"Immediate"};

    asyncResp->res
        .jsonValue["MultipartHttpPushUri@Redfish.OperationApplyTimeSupport"] = {
        {"@odata.type", "#Settings.v1_3_3.OperationApplyTimeSupport"},
        {"SupportedValues", operationApplyTimeSupportedValues}};

    asyncResp->res.jsonValue["FirmwareInventory"]["@odata.id"] =
        "/redfish/v1/UpdateService/FirmwareInventory";

    // Get the MaxImageSizeBytes
    asyncResp->res.jsonValue["MaxImageSizeBytes"] = firmwareImageLimitBytes;

    extendUpdateServiceGet(asyncResp);

#if defined(BMCWEB_INSECURE_TFTP_UPDATE) || defined(BMCWEB_SCP_UPDATE) ||      \
    defined(BMCWEB_REDFISH_UPDATESERVICE_HTTP_PULL)
    // Update Actions object.
    nlohmann::json& updateSvcSimpleUpdate =
        asyncResp->res.jsonValue["Actions"]["#UpdateService.SimpleUpdate"];
    updateSvcSimpleUpdate["target"] =
        "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate";
    updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] = {};

    if constexpr (BMCWEB_INSECURE_PUSH_STYLE_NOTIFICATION)
    {
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
            "TFTP";
    }

    if constexpr (BMCWEB_SCP_UPDATE)
    {
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
            "SCP";
    }
    if constexpr (BMCWEB_REDFISH_UPDATESERVICE_HTTP_PULL)
    {
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
            "HTTP";
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
            "HTTPS";
    }
#endif
    asyncResp->res
        .jsonValue["HttpPushUriOptions"]["HttpPushUriApplyTime"]["ApplyTime"] =
        "Immediate";
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

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus response error on GetSubTree {}", ec);
                return;
            }

            if (subtree.empty())
            {
                return;
            }

            std::vector<std::string> pathNames;
            for (const auto& [object, serviceMap] : subtree)
            {
                sdbusplus::message::object_path path(object);
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    continue;
                }
                pathNames.push_back(leaf);
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 pathNames](const boost::system::error_code& ec2,
                            const dbus::utility::MapperGetSubTreeResponse&
                                subtree2) mutable {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "D-Bus response error on GetSubTree {}", ec2);
                        return;
                    }

                    for (const auto& [object, serviceMap] : subtree2)
                    {
                        sdbusplus::message::object_path path(object);
                        std::string leaf = path.filename();
                        if (leaf.empty() or
                            std::ranges::find(pathNames, leaf) !=
                                pathNames.end())
                        {
                            continue;
                        }
                        pathNames.push_back(leaf);
                    }

                    std::ranges::sort(pathNames, AlphanumLess<std::string>());
                    nlohmann::json& members =
                        asyncResp->res.jsonValue["Members"];
                    members = nlohmann::json::array();

                    for (const std::string& leaf : pathNames)
                    {
                        boost::urls::url url = boost::urls::url(
                            "/redfish/v1/UpdateService/FirmwareInventory");
                        crow::utility::appendUrlPieces(url, leaf);
                        nlohmann::json::object_t member;
                        member["@odata.id"] = std::move(url);
                        members.emplace_back(std::move(member));
                    }
                    asyncResp->res.jsonValue["Members@odata.count"] =
                        members.size();
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/software/", int32_t(0),
                std::array<const char*, 2>{
                    "xyz.openbmc_project.State.Decorator.Health",
                    "xyz.openbmc_project.State.Decorator.OperationalStatus"});
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/software/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
}

/* Fill related item links (i.e. bmc, bios) in for inventory */
inline static void getRelatedItems(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId, const std::string& purpose)
{
    if (purpose == fw_util::biosPurpose)
    {
        nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
        relatedItem.push_back(
            {{"@odata.id",
              "/redfish/v1/Systems/" +
                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Bios"}});
        asyncResp->res.jsonValue["Members@odata.count"] = relatedItem.size();
    }
    else if (purpose == fw_util::otherPurpose || purpose == fw_util::bmcPurpose)
    {
        getRelatedItemsOthers(asyncResp, swId);
    }
    else
    {
        BMCWEB_LOG_DEBUG("Unknown software purpose {}", purpose);
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
                BMCWEB_LOG_DEBUG("Can't find property \"Purpose\"!");
                messages::internalError(asyncResp->res);
                return;
            }

            BMCWEB_LOG_DEBUG("swInvPurpose = {}", *swInvPurpose);

            if (version == nullptr)
            {
                BMCWEB_LOG_DEBUG("Can't find property \"Version\"!");

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
                if (objPath.filename() != *swId)
                {
                    continue;
                }

                if (obj.second.size() < 1)
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
                    fw_util::getFwWriteProtectedStatus(asyncResp, swId,
                                                       settingService);
                }
                asyncResp->res.jsonValue["Id"] = *swId;

                if (!versionService.empty())
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp,
                         swId](const boost::system::error_code& errorCode,
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
                                BMCWEB_LOG_DEBUG(
                                    "Can't find property \"Version\"!");
                                messages::propertyMissing(asyncResp->res,
                                                          "Purpose");
                                return;
                            }
                            const std::string* swInvPurpose =
                                std::get_if<std::string>(&it->second);
                            if (swInvPurpose == nullptr)
                            {
                                BMCWEB_LOG_DEBUG(
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
                                BMCWEB_LOG_DEBUG(
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
                                BMCWEB_LOG_DEBUG(
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
                            asyncResp->res.jsonValue["Id"] = *swId;

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

                extendSoftwareInventoryGet(asyncResp, obj.first, swId);

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
                BMCWEB_LOG_ERROR("Input swID {} not found!", *swId);
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
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleMultipartUpdateServicePost, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceFirmwareInventoryCollectionGet, std::ref(app)));
}
} // namespace redfish
