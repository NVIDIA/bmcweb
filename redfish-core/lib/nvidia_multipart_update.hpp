#pragma once

#include "async_resp.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "multipart_parser.hpp"
#include "multipart_serializer.hpp"
#include "nvidia_messages.hpp"
#include "redfish_aggregator.hpp"
#include "task.hpp"
#include "update_service.hpp"
#include "utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/memfd_utils.hpp"

#include <sys/stat.h>

#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/post.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <format>
#include <span>

// Nvidia code starts here

namespace redfish::nvidia
{

using redfish::task::Payload;

inline boost::system::error_code errorHandler(unsigned int respCode)
{
    BMCWEB_LOG_DEBUG("Response code was: {}", respCode);
    // Non standard handler.  All possible responses are valid as they are
    // forwarded to the user.
    return boost::system::errc::make_error_code(boost::system::errc::success);
};

/**
 * @brief Identifies the uploaded firmware package in NvidiaUpdate messages.
 *
 * name is the "filename" parameter of the UpdateFile part, or the part name
 * when the client did not send one.  declaredSize is the request
 * Content-Length: the size check runs while the upload streams, so the true
 * package size is not known when the limit is crossed.
 */
struct FirmwarePackageInfo
{
    std::string name = "UpdateFile";
    size_t declaredSize = 0;
};

inline std::string formatMiB(size_t bytes)
{
    // One decimal place: truncating to whole MiB renders a package that is
    // barely over the limit as the same figure as the limit itself.
    return std::format("{:.1f} MiB",
                       static_cast<double>(bytes) / (1024.0 * 1024.0));
}

enum class TargetType
{
    Error,
    Local,
    Satellite,
    SatelliteOmitTargets
};

/**
 * @brief Translate a StartUpdate D-Bus error into a Redfish response
 *
 * @param[in] asyncResp - Async response object
 * @param[in] errName - D-Bus error name from the StartUpdate reply
 */
// D-Bus errors that mean the update agent never processed the request: it is
// not running, or it did not answer in time.  Both are transient, so the
// client is told to retry rather than that the service failed internally.
constexpr std::array<std::string_view, 4> updateAgentUnreachableErrors{
    "org.freedesktop.DBus.Error.ServiceUnknown",
    "org.freedesktop.DBus.Error.NoReply", "org.freedesktop.DBus.Error.Timeout",
    "org.freedesktop.DBus.Error.TimedOut"};

inline void handleStartUpdateError(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view errName)
{
    if (std::ranges::find(updateAgentUnreachableErrors, errName) !=
        updateAgentUnreachableErrors.end())
    {
        messages::serviceTemporarilyUnavailable(asyncResp->res, "60");
        return;
    }
    if (errName == "xyz.openbmc_project.Software.Update.Error.InvalidImage")
    {
        messages::missingOrMalformedPart(asyncResp->res);
        return;
    }
    messages::internalError(asyncResp->res);
}

inline void handleStartUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload payload,
    const std::string& target, const std::string& packageName,
    const boost::system::error_code& ec, const sdbusplus::message_t& msg,
    const sdbusplus::object_path& retPath,
    const std::function<void()>& onResponseReady)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        // StartUpdate failed; release the guard (handleCreateTask() clears it
        // on success).
        redfish::fwUpdateInProgress = false;
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError != nullptr)
        {
            BMCWEB_LOG_ERROR("StartUpdate D-Bus error: {} - {}",
                             dbusError->name, dbusError->message);
            handleStartUpdateError(asyncResp, dbusError->name);
        }
        else
        {
            messages::internalError(asyncResp->res);
        }
        onResponseReady();
        return;
    }

    BMCWEB_LOG_INFO("Call to StartUpdate on {} Success, retPath = {}", target,
                    retPath.str);
    // Report which package the task is applying; createTask() moves
    // preTaskMessages into the task's Messages array.
    redfish::preTaskMessages.emplace_back(
        redfish::messages::firmwarePackage(packageName));
    createTask(asyncResp, std::move(payload), retPath);
    onResponseReady();
}

inline void startSoftwareUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload&& payload,
    boost::asio::local::stream_protocol::socket& fileGetSocket,
    const std::string& applyTime, const std::string& serviceName,
    const sdbusplus::object_path& target, const std::string& packageName,
    std::function<void()> onResponseReady, const std::function<void()>& onError)
{
    BMCWEB_LOG_DEBUG("Starting software update for {}", target.str);

    // Claim the guard atomically with the check at the dispatch point: a second
    // local request that passed the startRequest() check while this one was
    // still uploading is rejected here instead of racing into a concurrent
    // StartUpdate.
    if (redfish::fwUpdateInProgress)
    {
        BMCWEB_LOG_ERROR("Update already in progress.");
        redfish::messages::firmwareUpdateInProgress(
            asyncResp->res, "/redfish/v1/TaskService/Tasks");
        onError();
        return;
    }

    sdbusplus::message::unix_fd fd(fileGetSocket.native_handle());

    redfish::fwUpdateInProgress = true;

    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp, payload = std::move(payload), target, packageName,
         onResponseReady = std::move(onResponseReady)](
            const boost::system::error_code& ec1, sdbusplus::message_t& msg,
            const sdbusplus::object_path& retPath) mutable {
            nvidia::handleStartUpdate(asyncResp, std::move(payload), target,
                                      packageName, ec1, msg, retPath,
                                      onResponseReady);
        },
        serviceName, target, updateInterface, "StartUpdate", fd, applyTime);
}

inline std::string getRandomId()
{
    return std::format("bmcweb-update-{}", bmcweb::getRandomIdOfLength(8));
}

// This class Exists because PLDM mmaps the FD instead of streaming or reading
// the FD.  This will be fixed in the future, but for now, do the reading for
// PLDM
struct PLDMUpdateCtx : public std::enable_shared_from_this<PLDMUpdateCtx>
{
    MemoryFileDescriptor memfd;
    size_t bytesWritten = 0;
    std::array<uint8_t, 4096> buffer{};
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;

    boost::asio::local::stream_protocol::socket fileGetSocket;

    std::string applyTime;
    bool forceUpdate;
    std::vector<sdbusplus::object_path> targets;
    FirmwarePackageInfo package;

    redfish::task::Payload payload;
    std::function<void()> onResponseReady;
    std::function<void()> onError;

    PLDMUpdateCtx(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
        Payload&& payloadIn,
        boost::asio::local::stream_protocol::socket&& fileGetSocketIn,
        const std::string& applyTimeIn, bool forceUpdateIn,
        const std::vector<sdbusplus::object_path>& targetsIn,
        const FirmwarePackageInfo& packageIn,
        std::function<void()> onResponseReadyIn,
        std::function<void()> onErrorIn,
        const std::shared_ptr<MemoryFileDescriptor>& memfdIn = nullptr) :
        memfd(memfdIn ? std::move(*memfdIn)
                      : MemoryFileDescriptor(getRandomId())),
        asyncResp(asyncRespIn), fileGetSocket(std::move(fileGetSocketIn)),
        applyTime(applyTimeIn), forceUpdate(forceUpdateIn), targets(targetsIn),
        package(packageIn), payload(std::move(payloadIn)),
        onResponseReady(std::move(onResponseReadyIn)),
        onError(std::move(onErrorIn))
    {}

    void doRead()
    {
        fileGetSocket.async_read_some(
            boost::asio::buffer(buffer),
            [this, self{shared_from_this()}](
                const boost::system::error_code& ec, size_t bytesTransferred) {
                gotBytes(ec, bytesTransferred);
            });
    }

    void gotBytes(const boost::system::error_code& ec, size_t bytesTransferred)
    {
        if (ec == boost::asio::error::eof)
        {
            doUpdate();
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to read from file get socket: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            onError();
            return;
        }
        if (bytesWritten > redfish::firmwareImageLimitBytes ||
            bytesTransferred > redfish::firmwareImageLimitBytes - bytesWritten)
        {
            BMCWEB_LOG_ERROR("UpdateFile exceeds image limit of {} bytes",
                             redfish::firmwareImageLimitBytes);
            messages::firmwarePackageSizeExceeded(
                asyncResp->res, package.name, formatMiB(package.declaredSize),
                formatMiB(redfish::firmwareImageLimitBytes));
            onError();
            return;
        }
        BMCWEB_LOG_DEBUG("Putting {} bytes to buffer", bytesTransferred);
        bytesWritten += bytesTransferred;

        // TODO(Ed) the third argument on this really shouldn't be required.
        // It's not clear why every write rewinds
        if (::write(memfd.fd, buffer.data(), bytesTransferred) !=
            static_cast<ssize_t>(bytesTransferred))
        {
            BMCWEB_LOG_ERROR("Failed to write to memfd");
            messages::firmwarePackageStagingError(asyncResp->res, package.name,
                                                  formatMiB(bytesWritten));
            onError();
            return;
        }
        doRead();
    }

    void doUpdate()
    {
        if (asyncResp->res.result() != boost::beast::http::status::ok)
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Sending update to PLDM");

        const std::string serviceName = "xyz.openbmc_project.PLDM";
        const std::string objectPath = "/xyz/openbmc_project/software/pldm";

        memfd.rewind();
        sdbusplus::message::unix_fd fd(memfd.fd);

        // Claim the guard atomically with the check at the dispatch point: a
        // second local request that passed the startRequest() check while this
        // one was still uploading is rejected here instead of racing into a
        // concurrent StartUpdate.
        if (redfish::fwUpdateInProgress)
        {
            BMCWEB_LOG_ERROR("Update already in progress.");
            redfish::messages::firmwareUpdateInProgress(
                asyncResp->res, "/redfish/v1/TaskService/Tasks");
            onError();
            return;
        }
        redfish::fwUpdateInProgress = true;

        dbus::utility::async_method_call(
            [asyncResp{asyncResp}, payload = std::move(payload),
             fileGetSocket{std::move(fileGetSocket)}, objectPath,
             packageName{package.name}, onResponseReady{onResponseReady}](
                const boost::system::error_code& ec1, sdbusplus::message_t& msg,
                const sdbusplus::object_path& retPath) mutable {
                nvidia::handleStartUpdate(asyncResp, std::move(payload),
                                          objectPath, packageName, ec1, msg,
                                          retPath, onResponseReady);
            },
            serviceName, objectPath, updateInterface, "StartUpdate", fd,
            applyTime, forceUpdate, targets);
    }
};

inline void startPLDMUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload&& payload,
    boost::asio::local::stream_protocol::socket&& fileGetSocket,
    const std::string& applyTime, bool forceUpdate,
    const std::vector<sdbusplus::object_path>& targets,
    const FirmwarePackageInfo& package, std::function<void()> onResponseReady,
    std::function<void()> onError,
    const std::shared_ptr<MemoryFileDescriptor>& memfd = nullptr)
{
    BMCWEB_LOG_DEBUG("Starting PLDM update for {} targets", targets.size());

    bool fileAlreadyLoaded = memfd != nullptr;
    std::shared_ptr<PLDMUpdateCtx> pldmUpdateCtx =
        std::make_shared<PLDMUpdateCtx>(
            asyncResp, std::move(payload), std::move(fileGetSocket), applyTime,
            forceUpdate, targets, package, std::move(onResponseReady),
            std::move(onError), memfd);
    if (fileAlreadyLoaded)
    {
        boost::asio::post(getIoContext(),
                          [pldmUpdateCtx]() { pldmUpdateCtx->doUpdate(); });
        return;
    }
    pldmUpdateCtx->doRead();
}

inline void afterGetSubtreePathsSoftware(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload&& payload,
    const std::shared_ptr<boost::asio::local::stream_protocol::socket>&
        fileGetSocket,
    const std::string& updateUriTarget, const std::string& dbusApplyTime,
    const std::string& packageName, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& swInvPaths,
    std::function<void()> onResponseReady, const std::function<void()>& onError)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get software inventory: {}", ec);
        // The inventory lookup is unavailable (typically a restart); the
        // client can retry.
        messages::serviceTemporarilyUnavailable(asyncResp->res, "60");
        onError();
        return;
    }
    BMCWEB_LOG_DEBUG("Found {} software inventory paths", swInvPaths.size());

    for (const auto& path : swInvPaths)
    {
        sdbusplus::object_path softwarePath(path.first);
        std::string filename = softwarePath.filename();
        BMCWEB_LOG_DEBUG("Comparing filename {} to updateUriTarget {}",
                         filename, updateUriTarget);
        if (filename != updateUriTarget)
        {
            continue;
        }
        if (path.second.size() != 1)
        {
            BMCWEB_LOG_WARNING(
                "Found {} service versions for path {}  Canceling",
                path.second.size(), softwarePath.str);
            continue;
        }

        BMCWEB_LOG_DEBUG("Starting software update for {} on path {}",
                         path.second[0].first, softwarePath.str);
        startSoftwareUpdate(asyncResp, std::move(payload), *fileGetSocket,
                            dbusApplyTime, path.second[0].first, softwarePath,
                            packageName, std::move(onResponseReady), onError);
        return;
    }

    messages::resourceNotFound(asyncResp->res,
                               "SoftwareInventory.v1_4_0.SoftwareInventory",
                               updateUriTarget);
    onError();
}

inline void afterGetSubtreePaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload&& payload,
    const std::shared_ptr<boost::asio::local::stream_protocol::socket>&
        fileGetSocket,
    const std::string& dbusApplyTime, bool forceUpdate,
    const std::vector<std::string>& uriTargets,
    const FirmwarePackageInfo& package, const boost::system::error_code& ec,
    const std::vector<std::string>& swInvPaths,
    std::function<void()> onResponseReady, std::function<void()> onError,
    const std::shared_ptr<MemoryFileDescriptor>& memfd = nullptr)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get software inventory: {}", ec);
        // The inventory lookup is unavailable (typically a restart); the
        // client can retry.
        messages::serviceTemporarilyUnavailable(asyncResp->res, "60");
        onError();
        return;
    }

    std::vector<sdbusplus::object_path> validTargets;
    std::vector<std::string> updateableFw;
    updateableFw.reserve(swInvPaths.size());
    for (const auto& path : swInvPaths)
    {
        std::string fwId = std::filesystem::path(path).filename();
        updateableFw.push_back(fwId);
    }

    std::string firstInvalidTarget;
    if (areTargetsInvalidOrUnupdatable(uriTargets, updateableFw, swInvPaths,
                                       validTargets, firstInvalidTarget))
    {
        BMCWEB_LOG_ERROR("Invalid targets provided");
        messages::firmwareUpdateTargetInvalid(asyncResp->res,
                                              firstInvalidTarget);
        onError();
        return;
    }

    startPLDMUpdate(asyncResp, std::move(payload), std::move(*fileGetSocket),
                    dbusApplyTime, forceUpdate, validTargets, package,
                    std::move(onResponseReady), std::move(onError), memfd);
}

// Redfish resource type of a target that parseRfaUri() classifies as a
// satellite target.
inline std::string_view satelliteTargetType(std::string_view uri)
{
    if (uri.starts_with("/redfish/v1/Chassis/"))
    {
        return "Chassis";
    }
    if (uri.starts_with("/redfish/v1/Managers/"))
    {
        return "Manager";
    }
    if (uri.starts_with("/redfish/v1/UpdateService/SoftwareInventory/"))
    {
        return "SoftwareInventory";
    }
    return "FirmwareInventory";
}

inline TargetType parseRfaUri(std::string_view uri)
{
    if (uri.empty())
    {
        return TargetType::Error;
    }

    boost::system::result<boost::urls::url> parsed =
        boost::urls::parse_relative_ref(uri);
    if (!parsed)
    {
        BMCWEB_LOG_ERROR("Couldn't parse URI from resource {}", uri);
        return TargetType::Error;
    }

    std::string chassisId;
    if (crow::utility::readUrlSegments(*parsed, "redfish", "v1", "Chassis",
                                       std::ref(chassisId)))
    {
        if (chassisId == BMCWEB_RFA_HMC_UPDATE_TARGET)
        {
            // If the target is the manager, don't send it at all so all of HMC
            // updates
            // TODO(Ed) This is technically a Redfish implementation spec
            // violation.
            BMCWEB_LOG_DEBUG(
                "Update target was HMC itself.  Removing Targets from request.");
            return TargetType::SatelliteOmitTargets;
        }

        std::string prefix =
            std::format("{}_", BMCWEB_REDFISH_AGGREGATION_PREFIX);
        if (!chassisId.starts_with(prefix))
        {
            return TargetType::Local;
        }

        BMCWEB_LOG_DEBUG(
            "Update target was normal satellite.  Returning Satellite.");
        return TargetType::Satellite;
    }
    std::string managerId;
    if (crow::utility::readUrlSegments(*parsed, "redfish", "v1", "Managers",
                                       std::ref(managerId)))
    {
        if (managerId == BMCWEB_RFA_HMC_UPDATE_TARGET)
        {
            return TargetType::SatelliteOmitTargets;
        }
    }

    std::string softwareId;
    if (crow::utility::readUrlSegments(*parsed, "redfish", "v1",
                                       "UpdateService", "SoftwareInventory",
                                       std::ref(softwareId)))
    {
        std::string prefix =
            std::format("{}_", BMCWEB_REDFISH_AGGREGATION_PREFIX);
        if (!softwareId.starts_with(prefix))
        {
            return TargetType::Local;
        }

        BMCWEB_LOG_DEBUG(
            "Update target was satellite SoftwareInventory.  Returning Satellite.");
        return TargetType::Satellite;
    }

    std::string firmwareId;
    if (crow::utility::readUrlSegments(*parsed, "redfish", "v1",
                                       "UpdateService", "FirmwareInventory",
                                       std::ref(firmwareId)))
    {
        std::string prefix =
            std::format("{}_", BMCWEB_REDFISH_AGGREGATION_PREFIX);
        if (!firmwareId.starts_with(prefix))
        {
            return TargetType::Local;
        }

        BMCWEB_LOG_DEBUG(
            "Update target was satellite FirmwareInventory.  Returning Satellite.");
        return TargetType::Satellite;
    }

    return TargetType::Local;
}

struct UpdateCtx : public std::enable_shared_from_this<UpdateCtx>
{
    UpdateCtx(size_t incomingContentLengthIn, Payload&& payloadIn) :
        fileSendSocket(getIoContext()), fileGetSocket(getIoContext()),
        multipartSerializer(
            std::bind_front(&UpdateCtx::putBytesToHttpClient, this)),
        incomingContentLength(incomingContentLengthIn),
        payload(std::move(payloadIn))
    {
        boost::system::error_code ec2;
        boost::asio::local::connect_pair(fileGetSocket, fileSendSocket, ec2);
        if (ec2)
        {
            BMCWEB_LOG_ERROR("Failed to connect pair: {}", ec2.message());
            return;
        }
        fileGetSocket.native_non_blocking(true, ec2);
        if (ec2)
        {
            BMCWEB_LOG_ERROR("Failed to set non-blocking: {}", ec2.message());
            return;
        }
        fileSendSocket.native_non_blocking(true, ec2);
        if (ec2)
        {
            BMCWEB_LOG_ERROR("Failed to set non-blocking: {}", ec2.message());
            return;
        }
        package.declaredSize = incomingContentLength;
    }

    using SelfPtr = std::shared_ptr<UpdateCtx>;
    enum class State
    {
        WAITING_FOR_PART_HEADERS,
        WAITING_FOR_UPDATE_PARAMETERS_DATA,
        WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS,
        WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE,
        WAITING_FOR_UPDATE_FILE_DATA,
        WAITING_FOR_HTTP_CLIENT_DATA_SEND,
        UPDATE_COMPLETE,
        UPDATE_COMPLETE_ERROR
    };

    State state = State::WAITING_FOR_PART_HEADERS;
    // TODO, replace this with bmcweb sax json parser
    std::string updateParametersString;
    bool updateFileHeadersSeen = false;
    size_t updateFileRemainingBodyLength = 0;
    bool updateStarted = false;

    // Socket for sending data to the http client
    boost::asio::local::stream_protocol::socket fileSendSocket;

    // Socket for receiving data from serializer.  Invalid after http
    // request has started
    boost::asio::local::stream_protocol::socket fileGetSocket;

    MultipartSerializer multipartSerializer;

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;

    std::function<void()> pauseReadCb;
    std::function<void()> resumeReadCb;
    std::string currentWriteBuffer;
    std::string pendingWriteBuffer;
    bool socketInUse = false;
    size_t incomingContentLength;
    FirmwarePackageInfo package;
    MultiPartUpdate multiRet;

    std::string pendingFileDataBuffer;
    bool fileSectionComplete = false;
    bool parseComplete = false;

    std::shared_ptr<MemoryFileDescriptor> stagedUpdateFile;
    bool updateParametersReceived = false;

    std::optional<size_t> getStagedUpdateFileSize() const
    {
        if (!stagedUpdateFile)
        {
            return std::nullopt;
        }
        struct stat fileStat{};
        if (fstat(stagedUpdateFile->fd, &fileStat) != 0 || fileStat.st_size < 0)
        {
            return std::nullopt;
        }
        return static_cast<size_t>(fileStat.st_size);
    }

    bool appendStagedUpdateFile(std::string_view data) const
    {
        std::span<const char> remaining(data);
        while (!remaining.empty())
        {
            ssize_t written = ::write(stagedUpdateFile->fd, remaining.data(),
                                      remaining.size());
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            if (written <= 0)
            {
                return false;
            }
            remaining = remaining.subspan(static_cast<size_t>(written));
        }
        return true;
    }

    void startStagedFileReplay()
    {
        if (!stagedUpdateFile || !stagedUpdateFile->rewind())
        {
            BMCWEB_LOG_ERROR("Failed to rewind staged update memfd");
            messages::internalError(asyncResp->res);
            failClientResponse();
            return;
        }
        replayStagedFileChunk();
    }

    void replayStagedFileChunk()
    {
        if (state != State::WAITING_FOR_UPDATE_FILE_DATA)
        {
            stagedUpdateFile.reset();
            closeSendSocketIfReady();
            releaseClientResponseIfReady();
            return;
        }
        if (!stagedUpdateFile)
        {
            BMCWEB_LOG_ERROR("Staged update memfd missing during replay");
            messages::internalError(asyncResp->res);
            failClientResponse();
            return;
        }
        std::array<char, 4096> buffer{};
        ssize_t bytesRead = -1;
        do
        {
            bytesRead =
                ::read(stagedUpdateFile->fd, buffer.data(), buffer.size());
        } while (bytesRead < 0 && errno == EINTR);
        if (bytesRead < 0)
        {
            BMCWEB_LOG_ERROR("Failed to read from staged update memfd");
            messages::internalError(asyncResp->res);
            failClientResponse();
            return;
        }
        if (bytesRead == 0)
        {
            BMCWEB_LOG_DEBUG("Staged update replay complete");
            stagedUpdateFile.reset();
            multipartSerializer.finish();
            state = State::UPDATE_COMPLETE;
            closeSendSocketIfReady();
            releaseClientResponseIfReady();
            return;
        }
        std::string_view chunk(buffer.data(), static_cast<size_t>(bytesRead));
        multipartSerializer.put(chunk);
    }

    // End the client response only once this is set AND the inbound body is
    // fully consumed (parseComplete).
    bool responseReady = false;

    // True for a local (PLDM/Software.Update) target.  Local updates forward
    // the raw fwpkg bytes straight to the socket fd handed to the update
    // service; satellite updates re-serialize the body as multipart form-data.
    bool isLocal = false;

    Payload payload;

    void closeSendSocketIfReady()
    {
        if (!parseComplete)
        {
            return;
        }
        if (socketInUse)
        {
            return;
        }
        if (!pendingWriteBuffer.empty())
        {
            return;
        }
        if (state != State::UPDATE_COMPLETE &&
            state != State::UPDATE_COMPLETE_ERROR)
        {
            return;
        }
        if (!fileSendSocket.is_open())
        {
            return;
        }
        boost::system::error_code ec;
        fileSendSocket.close(ec);
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to close file send socket: {}",
                             ec.message());
        }
    }

    // Keep the response alive until the inbound body is fully consumed to
    // avoid desynchronizing HTTP framing.
    void releaseClientResponseIfReady()
    {
        if (!responseReady)
        {
            return;
        }
        if (!parseComplete)
        {
            if (resumeReadCb)
            {
                resumeReadCb();
            }
            return;
        }
        asyncResp.reset();
    }

    std::function<void()> responseReadyCallback()
    {
        return [self(shared_from_this())]() {
            self->responseReady = true;
            self->releaseClientResponseIfReady();
        };
    }

    std::function<void()> failResponseCallback()
    {
        return [self(shared_from_this())]() { self->failClientResponse(); };
    }

    void failClientResponse()
    {
        state = State::UPDATE_COMPLETE_ERROR;
        stagedUpdateFile.reset();
        responseReady = true;
        releaseClientResponseIfReady();
    }

    void putBytesToHttpClient(std::string_view data)
    {
        // If we got here before we began the http request, buffer, as we do not
        // yet know the content-length.
        if (state != State::WAITING_FOR_UPDATE_FILE_DATA)
        {
            pendingWriteBuffer += data;
            return;
        }
        BMCWEB_LOG_DEBUG("putBytesToHttpClient() called: {}", data.size());
        // BMCWEB_LOG_DEBUG("data: {}", data);
        if (socketInUse)
        {
            BMCWEB_LOG_DEBUG("appending buffer to pendingWriteBuffer");
            pendingWriteBuffer.append(data);
            return;
        }

        socketInUse = true;
        currentWriteBuffer.assign(data);

        // Hold off any further socket reads until this write (and any data
        // queued during it) completes.
        if (pauseReadCb)
        {
            pauseReadCb();
        }

        boost::asio::async_write(
            fileSendSocket, boost::asio::buffer(currentWriteBuffer),
            std::bind_front(&UpdateCtx::afterWritePartialData, this,
                            shared_from_this()));
    }

    void afterWritePartialData(const SelfPtr& /*self*/,
                               const boost::beast::error_code& ec,
                               size_t bytesTransferred)
    {
        socketInUse = false;
        // If we're backpressued, just attempt to write again
        if (ec == boost::system::errc::operation_would_block)
        {
            if (bytesTransferred > 0)
            {
                BMCWEB_LOG_CRITICAL("Unexpected bytes transferred: {}",
                                    bytesTransferred);
            }
            boost::asio::async_write(
                fileSendSocket, boost::asio::buffer(currentWriteBuffer),
                std::bind_front(&UpdateCtx::afterWritePartialData, this,
                                shared_from_this()));
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("afterWritePartialData() failed: {}",
                             ec.message());
            if (!isLocal && !responseReady)
            {
                // The connection to the satellite BMC dropped while the
                // package was streaming; without this the response ends with
                // no message at all. Once the satellite has answered, that
                // answer is the response and may already be released.
                messages::operationFailed(asyncResp->res);
            }
            // The downstream socket is gone; discard the rest of the body and
            // keep reading so parseComplete can fire and the response ends.
            state = State::UPDATE_COMPLETE_ERROR;
            if (resumeReadCb)
            {
                resumeReadCb();
            }
            releaseClientResponseIfReady();
            return;
        }
        BMCWEB_LOG_DEBUG("afterWritePartialData() success: {} bytes sent",
                         bytesTransferred);

        if (!pendingWriteBuffer.empty())
        {
            currentWriteBuffer = std::move(pendingWriteBuffer);
            pendingWriteBuffer.clear();
            socketInUse = true;
            // BMCWEB_LOG_DEBUG("Writing buffer: {}", currentWriteBuffer);
            boost::asio::async_write(
                fileSendSocket, boost::asio::buffer(currentWriteBuffer),
                std::bind_front(&UpdateCtx::afterWritePartialData, this,
                                shared_from_this()));
            return;
        }

        currentWriteBuffer.clear();

        if (resumeReadCb)
        {
            resumeReadCb();
        }

        if (stagedUpdateFile)
        {
            replayStagedFileChunk();
            return;
        }

        closeSendSocketIfReady();
    }

    void startRequest(size_t remainingBodyLength)
    {
        BMCWEB_LOG_DEBUG("Starting update request");

        std::vector<std::string> uriTargets;
        if (multiRet.params.targets.has_value())
        {
            uriTargets = *multiRet.params.targets;
        }
        bool omitSatelliteTargets = false;
        std::vector<std::string> localTargetsOut;
        std::vector<std::string> satelliteTargetsOut;
        for (const auto& uri : uriTargets)
        {
            TargetType targetType = parseRfaUri(uri);
            if (targetType == TargetType::Error)
            {
                redfish::messages::actionParameterValueConflict(asyncResp->res,
                                                                "Targets", uri);
                failClientResponse();
                return;
            }
            if (targetType == TargetType::Local)
            {
                if (!satelliteTargetsOut.empty())
                {
                    redfish::messages::actionParameterValueConflict(
                        asyncResp->res, "Targets", uri);
                    failClientResponse();
                    return;
                }
                localTargetsOut.emplace_back(uri);
            }
            else if (targetType == TargetType::Satellite)
            {
                if (!localTargetsOut.empty())
                {
                    redfish::messages::actionParameterValueConflict(
                        asyncResp->res, "Targets", uri);
                    failClientResponse();
                    return;
                }
                satelliteTargetsOut.emplace_back(uri);
            }
            else if (targetType == TargetType::SatelliteOmitTargets)
            {
                if (!localTargetsOut.empty())
                {
                    redfish::messages::actionParameterValueConflict(
                        asyncResp->res, "Targets", uri);
                    failClientResponse();
                    return;
                }
                satelliteTargetsOut.emplace_back(uri);
                omitSatelliteTargets = true;
            }
        }

        // Workaround for dead store false positive in clang.
        (void)omitSatelliteTargets;

        if constexpr (BMCWEB_REDFISH_AGGREGATION)
        {
            if (!satelliteTargetsOut.empty())
            {
                if (omitSatelliteTargets)
                {
                    satelliteTargetsOut.clear();
                }
                state = State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;
                BMCWEB_LOG_DEBUG("Getting satellite configs");
                RedfishAggregator::getInstance().getSatelliteConfigs(
                    std::bind_front(&UpdateCtx::satControllerGetComplete, this,
                                    shared_from_this(), satelliteTargetsOut,
                                    remainingBodyLength));
                return;
            }
        }
        else
        {
            BMCWEB_LOG_DEBUG(
                "Aggregation is disabled, all targets are local targets");
            // If aggregation is disabled, all targets are local targets, let
            // the errors be dealt with later
            localTargetsOut.insert(localTargetsOut.end(),
                                   satelliteTargetsOut.begin(),
                                   satelliteTargetsOut.end());
        }

        // Only allow one local firmware update at a time.  Satellite-targeted
        // updates are forwarded above without consulting the guard: the
        // satellite BMC serializes its own updates, and a local update in
        // flight must not block it.
        if (redfish::fwUpdateInProgress)
        {
            BMCWEB_LOG_ERROR("Update already in progress.");
            redfish::messages::firmwareUpdateInProgress(
                asyncResp->res, "/redfish/v1/TaskService/Tasks");
            failClientResponse();
            return;
        }

        localUpdate(localTargetsOut);
    }

    void beginUpdateFile(size_t remainingBodyLength)
    {
        // A failed parameter check leaves updateStarted false, so without the
        // terminal-state test onParseComplete() runs this again and reports
        // the same error twice.
        if (updateStarted || state == State::UPDATE_COMPLETE_ERROR)
        {
            return;
        }
        if (!onUpdateParametersComplete(multiRet))
        {
            failClientResponse();
            return;
        }
        if (pauseReadCb)
        {
            pauseReadCb();
        }
        updateStarted = true;
        startRequest(remainingBodyLength);
    }

    void onHeadersComplete(const SelfPtr& /*self*/,
                           const boost::beast::http::fields& fields,
                           size_t remainingBodyLength)
    {
        if (state == State::WAITING_FOR_PART_HEADERS)
        {
            if (parseContentDisposition(fields, "UpdateParameters"))
            {
                if (updateParametersReceived)
                {
                    BMCWEB_LOG_ERROR("Duplicate UpdateParameters part");
                    messages::malformedMultipartRequest(
                        asyncResp->res, "duplicate UpdateParameters part",
                        "Remove the duplicate UpdateParameters part and "
                        "resubmit the request.");
                    failClientResponse();
                    return;
                }
                if (!parseContentType(fields))
                {
                    BMCWEB_LOG_ERROR(
                        "UpdateParameters part missing or invalid Content-Type");
                    messages::malformedMultipartRequest(
                        asyncResp->res,
                        "the UpdateParameters part is missing a JSON "
                        "Content-Type");
                    failClientResponse();
                    return;
                }
                state = State::WAITING_FOR_UPDATE_PARAMETERS_DATA;
                return;
            }

            if (!parseContentDisposition(fields, "UpdateFile"))
            {
                BMCWEB_LOG_ERROR("Unexpected multipart form-data name");
                messages::malformedMultipartRequest(
                    asyncResp->res, "a part carries a malformed or unexpected "
                                    "Content-Disposition");
                failClientResponse();
                return;
            }

            {
                auto ctIt =
                    fields.find(boost::beast::http::field::content_type);
                if (ctIt != fields.end() &&
                    http_helpers::getContentType(ctIt->value()) !=
                        http_helpers::ContentType::OctetStream)
                {
                    BMCWEB_LOG_ERROR("UpdateFile Content-Type is not "
                                     "application/octet-stream: {}",
                                     ctIt->value());
                    messages::malformedMultipartRequest(
                        asyncResp->res,
                        "the UpdateFile part Content-Type is not "
                        "application/octet-stream");
                    failClientResponse();
                    return;
                }
            }

            std::string fileName = parseFormPartFileName(fields);
            if (!fileName.empty())
            {
                package.name = std::move(fileName);
            }
            updateFileHeadersSeen = true;
            updateFileRemainingBodyLength = remainingBodyLength;
            if (stagedUpdateFile)
            {
                BMCWEB_LOG_ERROR("Duplicate UpdateFile part");
                messages::malformedMultipartRequest(
                    asyncResp->res, "duplicate UpdateFile part",
                    "Remove the duplicate UpdateFile part and resubmit the "
                    "request.");
                failClientResponse();
                return;
            }

            if (!updateParametersReceived)
            {
                BMCWEB_LOG_DEBUG(
                    "UpdateFile sent before UpdateParameters; staging");
                stagedUpdateFile =
                    std::make_shared<MemoryFileDescriptor>(getRandomId());
                if (stagedUpdateFile->fd < 0)
                {
                    BMCWEB_LOG_ERROR("Failed to create staged update memfd");
                    messages::firmwarePackageStagingError(
                        asyncResp->res, package.name, formatMiB(0));
                    failClientResponse();
                    return;
                }
                state = State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS;
                return;
            }

            state = State::WAITING_FOR_UPDATE_FILE_DATA;
            beginUpdateFile(remainingBodyLength);
            return;
        }

        if (state == State::UPDATE_COMPLETE_ERROR)
        {
            return;
        }
        if (state == State::UPDATE_COMPLETE)
        {
            BMCWEB_LOG_ERROR("Unexpected multipart part after UpdateFile");
            messages::malformedMultipartRequest(
                asyncResp->res, "a part follows the UpdateFile part");
            failClientResponse();
            return;
        }

        BMCWEB_LOG_ERROR("Unexpected multipart part in state {}",
                         static_cast<int>(state));
        messages::malformedMultipartRequest(asyncResp->res,
                                            "a part arrived out of order");
        failClientResponse();
    }

    void onDataAvailable(const SelfPtr& /*self*/, std::string_view data)
    {
        if (state == State::WAITING_FOR_UPDATE_PARAMETERS_DATA)
        {
            // Fail rather than hang if UpdateParameters exceeds 8 KB.
            if (updateParametersString.size() + data.size() > 8192U)
            {
                BMCWEB_LOG_ERROR("UpdateParameters part exceeds 8192 bytes");
                messages::unrecognizedRequestBody(asyncResp->res);
                failClientResponse();
                return;
            }

            updateParametersString += data;
            return;
        }
        if (state == State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS)
        {
            if (!stagedUpdateFile)
            {
                BMCWEB_LOG_ERROR("Staged update memfd missing");
                messages::firmwarePackageStagingError(
                    asyncResp->res, package.name, formatMiB(0));
                failClientResponse();
                return;
            }
            std::optional<size_t> fileSize = getStagedUpdateFileSize();
            if (!fileSize)
            {
                BMCWEB_LOG_ERROR("Failed to get staged update memfd size");
                messages::firmwarePackageStagingError(
                    asyncResp->res, package.name, formatMiB(0));
                failClientResponse();
                return;
            }
            if (*fileSize > redfish::firmwareImageLimitBytes ||
                data.size() > redfish::firmwareImageLimitBytes - *fileSize)
            {
                BMCWEB_LOG_ERROR(
                    "Staged update exceeds memory limit of {} bytes",
                    redfish::firmwareImageLimitBytes);
                messages::firmwarePackageSizeExceeded(
                    asyncResp->res, package.name,
                    formatMiB(package.declaredSize),
                    formatMiB(redfish::firmwareImageLimitBytes));
                failClientResponse();
                return;
            }
            if (!appendStagedUpdateFile(data))
            {
                BMCWEB_LOG_ERROR("Failed to write to staged update memfd");
                messages::firmwarePackageStagingError(
                    asyncResp->res, package.name,
                    formatMiB(getStagedUpdateFileSize().value_or(0)));
                failClientResponse();
                return;
            }
            return;
        }
        if (state == State::WAITING_FOR_UPDATE_FILE_DATA)
        {
            // BMCWEB_LOG_DEBUG("Update file data available: {}", data);
            if (!updateStarted)
            {
                pendingFileDataBuffer.append(data);
                return;
            }
            if (isLocal)
            {
                putBytesToHttpClient(data);
            }
            else
            {
                multipartSerializer.put(data);
            }
            return;
        }
        if (state == State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE)
        {
            // BMCWEB_LOG_DEBUG(
            //     "Update file data buffered (waiting for sat info): {}",
            //     data);
            pendingFileDataBuffer.append(data);
            return;
        }
        if (state == State::UPDATE_COMPLETE ||
            state == State::UPDATE_COMPLETE_ERROR)
        {
            // Discard trailing body so the connection can finish reading.
            return;
        }

        BMCWEB_LOG_ERROR("Unexpected state on data available: {}",
                         static_cast<int>(state));
    }

    void onSectionComplete(const SelfPtr& /*self*/)
    {
        if (state == State::WAITING_FOR_UPDATE_PARAMETERS_DATA)
        {
            BMCWEB_LOG_DEBUG("Update parameters complete");
            std::optional<MultiPartUpdate::UpdateParameters> params =
                processUpdateParameters(asyncResp, updateParametersString);
            if (!params)
            {
                // processUpdateParameters() already set the error message.
                failClientResponse();
                return;
            }

            mergeUpdateParameters(multiRet.params, *params);
            updateParametersString.clear();
            updateParametersReceived = true;
            state = State::WAITING_FOR_PART_HEADERS;
            return;
        }
        if (state == State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS)
        {
            BMCWEB_LOG_DEBUG("Staged UpdateFile; waiting for UpdateParameters");
            state = State::WAITING_FOR_PART_HEADERS;
            return;
        }
        if (state == State::WAITING_FOR_UPDATE_FILE_DATA)
        {
            BMCWEB_LOG_DEBUG("Update file complete");
            if (!updateStarted)
            {
                fileSectionComplete = true;
                return;
            }
            if (!isLocal)
            {
                // Only the satellite path needs the trailing multipart
                // boundary; the local path forwards the raw fwpkg, so EOF is
                // signalled by closing the socket in closeSendSocketIfReady().
                multipartSerializer.finish();
            }
            // Complete the update file data
            state = State::UPDATE_COMPLETE;
            closeSendSocketIfReady();
            return;
        }
        if (state == State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE)
        {
            BMCWEB_LOG_DEBUG(
                "Update file section complete (deferred, waiting for sat info)");
            // Defer finishing the serializer until the http client is ready
            // and the buffered file data has been flushed in the correct order.
            fileSectionComplete = true;
            return;
        }
        if (state == State::UPDATE_COMPLETE ||
            state == State::UPDATE_COMPLETE_ERROR)
        {
            return;
        }

        BMCWEB_LOG_ERROR("Unexpected state: {}", static_cast<int>(state));
    }

    void onParseComplete(const SelfPtr& /*self*/)
    {
        BMCWEB_LOG_DEBUG("Parse complete");
        parseComplete = true;

        if (state == State::WAITING_FOR_PART_HEADERS &&
            updateParametersReceived && stagedUpdateFile)
        {
            std::optional<size_t> stagedBytes = getStagedUpdateFileSize();
            if (!stagedBytes)
            {
                BMCWEB_LOG_ERROR("Failed to get staged update memfd size");
                messages::firmwarePackageStagingError(
                    asyncResp->res, package.name, formatMiB(0));
                failClientResponse();
                return;
            }
            if (!onUpdateParametersComplete(multiRet))
            {
                failClientResponse();
                return;
            }
            startRequest(*stagedBytes);
            return;
        }

        if (state == State::WAITING_FOR_PART_HEADERS ||
            state == State::WAITING_FOR_UPDATE_PARAMETERS_DATA ||
            state == State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS)
        {
            messages::propertyMissing(
                asyncResp->res, stagedUpdateFile && !updateParametersReceived
                                    ? "UpdateParameters"
                                    : "UpdateFile");
            failClientResponse();
            return;
        }
        if (updateFileHeadersSeen && !updateStarted)
        {
            beginUpdateFile(updateFileRemainingBodyLength);
        }
        closeSendSocketIfReady();
        releaseClientResponseIfReady();
    }

    void onParseError(const SelfPtr& /*self*/, ParserError /*error*/)
    {
        if (state == State::UPDATE_COMPLETE_ERROR)
        {
            return;
        }
        messages::unrecognizedRequestBody(asyncResp->res);
        failClientResponse();
    }

    void onHttpClientDataSendComplete(
        const std::shared_ptr<UpdateCtx>& /*self*/, const std::string& prefix,
        const boost::urls::url& satelliteHost, bool /*keepAlive*/,
        int32_t /*connId*/, crow::Response& res)
    {
        BMCWEB_LOG_DEBUG("Response code: {}", res.resultInt());
        BMCWEB_LOG_DEBUG("Response body: {}", *res.body());
        for (const auto& header : res.fields())
        {
            BMCWEB_LOG_DEBUG("Response header: {}: {}", header.name_string(),
                             header.value());
        }

        if (res.body() != nullptr)
        {
            BMCWEB_LOG_DEBUG("Response body: {}", *res.body());
        }
        else
        {
            BMCWEB_LOG_ERROR("Response body is empty");
        }

        using enum boost::beast::http::field;
        std::string locationValue = res.response[location];
        if (!locationValue.empty())
        {
            // addPrefixToStringItem(locationValue, prefix);
            asyncResp->res.addHeader(location, locationValue);
        }
        std::string_view retryAfter = res.response[retry_after];
        if (!retryAfter.empty())
        {
            asyncResp->res.addHeader(retry_after, retryAfter);
        }

        if (res.result() == boost::beast::http::status::bad_gateway)
        {
            // The request never reached the satellite BMC: unreachable,
            // refused, TLS handshake failure or timeout.  processResponse()
            // relays the 502 without a body, so name the host here.
            messages::addMessageToErrorJson(
                asyncResp->res.jsonValue,
                messages::couldNotEstablishConnection(satelliteHost));
        }
        redfish::RedfishAggregator::processResponse(prefix, asyncResp, res);
        responseReady = true;
        releaseClientResponseIfReady();
    }

    bool onUpdateParametersComplete(MultiPartUpdate& multipart)
    {
        std::string applyTime = "OnReset";
        if (multipart.params.applyTime)
        {
            applyTime = *multipart.params.applyTime;
        }

        std::string dbusApplyTime;
        return convertApplyTime(asyncResp->res, applyTime, dbusApplyTime);
    }

    void setHeaders(const std::vector<std::string>& localTargetsOut)
    {
        nlohmann::json::object_t updateParametersJson;
        BMCWEB_LOG_DEBUG("Got {} targets", localTargetsOut.size());
        if (!localTargetsOut.empty())
        {
            updateParametersJson["Targets"] = localTargetsOut;
        }
        if (multiRet.params.applyTime)
        {
            // The receiver rejects unknown UpdateParameters keys, so use the
            // exact Redfish key.
            updateParametersJson["@Redfish.OperationApplyTime"] =
                *multiRet.params.applyTime;
        }
        if (multiRet.params.forceUpdate)
        {
            updateParametersJson["ForceUpdate"] = *multiRet.params.forceUpdate;
        }
        using field = boost::beast::http::field;
        {
            boost::beast::http::fields headers;
            headers.set(field::content_disposition,
                        "form-data; name=\"UpdateParameters\"");
            headers.set(field::content_type, "application/json");
            multipartSerializer.beginPart(headers);
            std::string updateParametersJsonStr =
                nlohmann::json(updateParametersJson)
                    .dump(-1, ' ', true,
                          nlohmann::json::error_handler_t::replace);
            BMCWEB_LOG_DEBUG("Update parameters JSON: {}",
                             updateParametersJsonStr);
            multipartSerializer.put(updateParametersJsonStr);
            BMCWEB_LOG_DEBUG("Putting update parameters JSON: {}",
                             updateParametersJsonStr);
        }
        {
            boost::beast::http::fields headers;
            // Forward the package name so the satellite renders it too; it is
            // sanitized at parse time, so it is safe in a quoted-string.
            headers.set(
                field::content_disposition,
                std::format(R"(form-data; name="UpdateFile"; filename="{}")",
                            package.name));
            headers.set(field::content_type, "application/octet-stream");
            multipartSerializer.beginPart(headers);
            BMCWEB_LOG_DEBUG("Putting update file headers");
        }
    }

    void satControllerGetComplete(
        const SelfPtr& /*self*/,
        const std::vector<std::string>& localTargetsOut,
        size_t remainingBodyLength, const boost::system::error_code& ec,
        const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
    {
        BMCWEB_LOG_DEBUG("Satellite controller get complete");
        if (state != State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE)
        {
            // The request failed while the satellite config query was in
            // flight; don't open the forwarding connection.
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to get satellite configs: {}",
                             ec.message());
            // Satellite discovery is unavailable; the client can retry.
            messages::serviceTemporarilyUnavailable(asyncResp->res, "60");
            failClientResponse();
            return;
        }
        if (satelliteInfo.empty())
        {
            BMCWEB_LOG_ERROR("No satellite BMC configs found.");
            // The request named satellite components but no Satellite
            // Management Controller is configured.  Name every target the
            // client sent; a target naming the satellite itself is dropped
            // from the forwarded list, so that list cannot be used here.
            if (multiRet.params.targets)
            {
                for (const std::string& uri : *multiRet.params.targets)
                {
                    messages::resourceNotFound(asyncResp->res,
                                               satelliteTargetType(uri), uri);
                }
            }
            failClientResponse();
            return;
        }
        const boost::urls::url& host = satelliteInfo.begin()->second;

        BMCWEB_LOG_DEBUG("Satellite host: {}", host);

        std::shared_ptr<crow::ConnectionPolicy> connPolicy =
            std::make_shared<crow::ConnectionPolicy>();
        connPolicy->maxRetryAttempts = 0;
        connPolicy->invalidResp = errorHandler;

        std::shared_ptr<crow::ConnectionInfo> httpClient =
            std::make_shared<crow::ConnectionInfo>(
                getIoContext(), "NvidiaMultipartUpdate", connPolicy, host,
                ensuressl::VerifyCertificate::NoVerify, 0);
        crow::ConnectionInfo& conn = *httpClient;

        conn.callback = std::bind_front(
            &UpdateCtx::onHttpClientDataSendComplete, this, shared_from_this(),
            satelliteInfo.begin()->first, host);

        conn.req.target("/redfish/v1/UpdateService/update-multipart");
        BMCWEB_LOG_DEBUG(
            "Starting request to satellite: {}/redfish/v1/UpdateService/update-multipart",
            host.buffer());

        conn.req.set(boost::beast::http::field::host,
                     host.encoded_host_address());

        conn.req.method(boost::beast::http::verb::post);
        boost::system::error_code ec2;
        conn.req.body().setFd(DuplicatableFileHandle(fileGetSocket.release()),
                              ec2);
        if (ec2)
        {
            BMCWEB_LOG_ERROR("Failed to set fd: {}", ec2.message());
            messages::internalError(asyncResp->res);
            failClientResponse();
            return;
        }

        state = State::WAITING_FOR_UPDATE_FILE_DATA;
        nlohmann::json::object_t updateParametersJson;
        BMCWEB_LOG_DEBUG("Got {} targets", localTargetsOut.size());
        setHeaders(localTargetsOut);

        BMCWEB_LOG_DEBUG("Remaining body length: {}", remainingBodyLength);
        BMCWEB_LOG_DEBUG("Pending file data buffer size: {}",
                         pendingWriteBuffer.size());

        conn.req.set(boost::beast::http::field::accept, "application/json");

        conn.req.content_length(
            currentWriteBuffer.size() + pendingWriteBuffer.size() +
            remainingBodyLength + multipartSerializer.getBoundary().size() + 8);

        conn.req.set(boost::beast::http::field::content_type,
                     multipartSerializer.getContentType());

        conn.doResolve();

        // Flush any file data that was buffered while we waited for the
        // satellite controller info to arrive.  This must happen after the
        // opening boundaries have been written so the body is in order.
        if (!pendingFileDataBuffer.empty())
        {
            multipartSerializer.put(pendingFileDataBuffer);
            pendingFileDataBuffer.clear();
        }

        // If the parser already finished the file section while we were
        // waiting, close out the serializer now.
        if (fileSectionComplete)
        {
            multipartSerializer.finish();
            state = State::UPDATE_COMPLETE;
        }

        if (resumeReadCb)
        {
            resumeReadCb();
        }

        if (stagedUpdateFile)
        {
            startStagedFileReplay();
            return;
        }
        closeSendSocketIfReady();
    }

    bool handleSoftwareUpdate(
        const std::string& dbusApplyTime,
        const std::vector<std::string>& uriTargets,
        const std::string& packageName,
        const std::shared_ptr<boost::asio::local::stream_protocol::socket>&
            fileGetSocketPtr)
    {
        BMCWEB_LOG_DEBUG("Handling software inventory update for {} targets",
                         uriTargets.size());
        // For now can only update one software at a time.
        if (uriTargets.size() != 1)
        {
            return false;
        }
        std::string softwareId;
        boost::system::result<boost::urls::url> uriTarget =
            boost::urls::parse_relative_ref(uriTargets[0]);
        if (!uriTarget)
        {
            return false;
        }
        if (!crow::utility::readUrlSegments(
                *uriTarget, "redfish", "v1", "UpdateService",
                "SoftwareInventory", std::ref(softwareId)))
        {
            return false;
        }
        BMCWEB_LOG_DEBUG("Getting software inventory for {}", softwareId);
        dbus::utility::getSubTree(
            "/xyz/openbmc_project/inventory_software", 0,
            std::array<std::string_view, 1>{
                "xyz.openbmc_project.Software.Update"},
            [asyncResp{asyncResp}, payload = std::move(payload),
             fileGetSocketPtr, uriTargets, dbusApplyTime, softwareId,
             packageName, onResponseReady{responseReadyCallback()},
             onError{failResponseCallback()}](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse&
                    swInvPaths) mutable {
                afterGetSubtreePathsSoftware(
                    asyncResp, std::move(payload), fileGetSocketPtr, softwareId,
                    dbusApplyTime, packageName, ec, swInvPaths,
                    std::move(onResponseReady), onError);
            });
        return true;
    }

    void beginLocalFileStreaming()
    {
        state = State::WAITING_FOR_UPDATE_FILE_DATA;

        // Flush anything the parser delivered while the update was being set
        // up.
        if (!pendingFileDataBuffer.empty())
        {
            putBytesToHttpClient(pendingFileDataBuffer);
            pendingFileDataBuffer.clear();
        }

        // The parser may have already consumed the whole (small) file.
        if (fileSectionComplete)
        {
            state = State::UPDATE_COMPLETE;
        }

        if (resumeReadCb)
        {
            resumeReadCb();
        }

        closeSendSocketIfReady();
    }

    void localUpdate(const std::vector<std::string>& uriTargets)
    {
        BMCWEB_LOG_DEBUG("Starting local update for {} targets",
                         uriTargets.size());
        isLocal = true;
        std::string dbusApplyTime;
        if (!convertApplyTime(asyncResp->res,
                              multiRet.params.applyTime.value_or("OnReset"),
                              dbusApplyTime))
        {
            BMCWEB_LOG_WARNING("Failed to convert apply time");
            failClientResponse();
            return;
        }
        bool forceUpdate = multiRet.params.forceUpdate.value_or(false);
        bool fileAlreadyStaged = stagedUpdateFile != nullptr;
        if (fileAlreadyStaged)
        {
            state = State::UPDATE_COMPLETE;
        }

        if (uriTargets.empty())
        {
            std::vector<sdbusplus::object_path> emptyTargets{};
            nvidia::startPLDMUpdate(
                asyncResp, std::move(payload), std::move(fileGetSocket),
                dbusApplyTime, forceUpdate, emptyTargets, package,
                responseReadyCallback(), failResponseCallback(),
                stagedUpdateFile);
            if (fileAlreadyStaged)
            {
                stagedUpdateFile.reset();
                closeSendSocketIfReady();
                return;
            }
            beginLocalFileStreaming();
            return;
        }
        std::shared_ptr<boost::asio::local::stream_protocol::socket>
            fileGetSocketPtr =
                std::make_shared<boost::asio::local::stream_protocol::socket>(
                    std::move(fileGetSocket));

        // TODO Need to clean up the IST dbus paths so we can use the normal
        // call
        if (!fileAlreadyStaged &&
            handleSoftwareUpdate(dbusApplyTime, uriTargets, package.name,
                                 fileGetSocketPtr))
        {
            beginLocalFileStreaming();
            return;
        }

        BMCWEB_LOG_DEBUG("Getting firmware inventory for {} targets",
                         uriTargets.size());
        std::shared_ptr<MemoryFileDescriptor> preloadedFile =
            std::move(stagedUpdateFile);
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/software", 0,
            std::array<std::string_view, 2>{
                "xyz.openbmc_project.Software.Version"},
            [asyncResp{asyncResp}, payload = std::move(payload),
             fileGetSocketPtr, dbusApplyTime, forceUpdate, uriTargets,
             package{package}, preloadedFile = std::move(preloadedFile),
             onResponseReady{responseReadyCallback()},
             onError{failResponseCallback()}](
                const boost::system::error_code& ec,
                const std::vector<std::string>& swInvPaths) mutable {
                afterGetSubtreePaths(
                    asyncResp, std::move(payload), fileGetSocketPtr,
                    dbusApplyTime, forceUpdate, uriTargets, package, ec,
                    swInvPaths, std::move(onResponseReady), std::move(onError),
                    preloadedFile);
            });
        if (fileAlreadyStaged)
        {
            closeSendSocketIfReady();
            return;
        }
        beginLocalFileStreaming();
    }
};

inline void handleUpdateServiceMultipartUpdatePostHeaders(
    crow::Request& req, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Configuring multipart parser callbacks");
    std::string_view contentType =
        req.getHeaderValue(boost::beast::http::field::content_type);
    if (!MultipartParser::hasMultipartBoundary(contentType))
    {
        BMCWEB_LOG_ERROR("The request has unsupported media type");
        asyncResp->res.result(
            boost::beast::http::status::unsupported_media_type);
        messages::addMessageToErrorJson(
            asyncResp->res.jsonValue,
            messages::headerValueInvalid(contentType, "Content-Type",
                                         "multipart/form-data"));
        return;
    }
    std::string_view ct =
        req.getHeaderValue(boost::beast::http::field::content_length);
    if (ct.empty())
    {
        BMCWEB_LOG_ERROR("Content-Length header not found");
        messages::headerMissing(asyncResp->res, "Content-Length");
        return;
    }
    size_t contentLength = 0;
    std::from_chars_result result =
        std::from_chars(ct.begin(), ct.end(), contentLength);
    if (result.ec != std::errc() || result.ptr != ct.end())
    {
        BMCWEB_LOG_ERROR("Failed to parse Content-Length: {}", ct);
        messages::headerInvalid(asyncResp->res, "Content-Length");
        return;
    }
    // The one-update-at-a-time guard is NOT checked here: Targets arrive in
    // the body, and a satellite-targeted update must be forwarded even while
    // a local update is in flight.  startRequest() rejects local updates once
    // targets are known; the dispatch points claim the guard.
    // Register streaming callbacks for the multipart parser
    std::shared_ptr<UpdateCtx> contextPtr =
        std::make_shared<UpdateCtx>(contentLength, Payload(req));
    contextPtr->asyncResp = asyncResp;
    MultipartParserStreamingCallbacks callbacks{
        .onStart =
            [contextPtr](std::function<void()> pause,
                         std::function<void()> resume) {
                contextPtr->pauseReadCb = std::move(pause);
                contextPtr->resumeReadCb = std::move(resume);
            },
        .onHeadersComplete = std::bind_front(&UpdateCtx::onHeadersComplete,
                                             contextPtr.get(), contextPtr),
        .onDataAvailable = std::bind_front(&UpdateCtx::onDataAvailable,
                                           contextPtr.get(), contextPtr),
        .onSectionComplete = std::bind_front(&UpdateCtx::onSectionComplete,
                                             contextPtr.get(), contextPtr),
        .onParseComplete = std::bind_front(&UpdateCtx::onParseComplete,
                                           contextPtr.get(), contextPtr),
        .onParseError = std::bind_front(&UpdateCtx::onParseError,
                                        contextPtr.get(), contextPtr)};
    req.setMultipartParserCallbacks(std::move(callbacks));
}

inline void requestRoutesNvUpdateServiceMultipartUpdate(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/update-multipart/")
        .privileges(redfish::privileges::postUpdateService)
        .streamInput()
        .methods(boost::beast::http::verb::post)(
            handleUpdateServiceMultipartUpdatePostHeaders);
}
} // namespace redfish::nvidia
// Nvidia code ends here
