#pragma once

#include "async_resp.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "multipart_parser.hpp"
#include "multipart_serializer.hpp"
#include "redfish_aggregator.hpp"
#include "task.hpp"
#include "update_service.hpp"
#include "utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/memfd_utils.hpp"

#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include <format>

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

enum class TargetType
{
    Error,
    Local,
    Satellite,
    SatelliteOmitTargets
};

inline void handleStartUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload payload,
    const std::string& target, const boost::system::error_code& ec,
    const sdbusplus::object_path& retPath,
    const std::function<void()>& onResponseReady)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        onResponseReady();
        return;
    }

    BMCWEB_LOG_INFO("Call to StartUpdate on {} Success, retPath = {}", target,
                    retPath.str);
    createTask(asyncResp, std::move(payload), retPath);
    onResponseReady();
}

inline void startSoftwareUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload&& payload,
    boost::asio::local::stream_protocol::socket& fileGetSocket,
    const std::string& applyTime, const std::string& serviceName,
    const sdbusplus::object_path& target,
    std::function<void()> onResponseReady)
{
    BMCWEB_LOG_DEBUG("Starting software update for {}", target.str);

    sdbusplus::message::unix_fd fd(fileGetSocket.native_handle());

    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp, payload = std::move(payload), target,
         onResponseReady = std::move(onResponseReady)](
            const boost::system::error_code& ec1,
            const sdbusplus::object_path& retPath) mutable {
            nvidia::handleStartUpdate(asyncResp, std::move(payload), target,
                                      ec1, retPath, onResponseReady);
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

    redfish::task::Payload payload;
    std::function<void()> onResponseReady;

    PLDMUpdateCtx(const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
                  Payload&& payloadIn,
                  boost::asio::local::stream_protocol::socket&& fileGetSocketIn,
                  const std::string& applyTimeIn, bool forceUpdateIn,
                  const std::vector<sdbusplus::object_path>& targetsIn,
                  std::function<void()> onResponseReadyIn) :
        memfd(getRandomId()), asyncResp(asyncRespIn),
        fileGetSocket(std::move(fileGetSocketIn)), applyTime(applyTimeIn),
        forceUpdate(forceUpdateIn), targets(targetsIn),
        payload(std::move(payloadIn)),
        onResponseReady(std::move(onResponseReadyIn))
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
            messages::internalError(asyncResp->res);
            return;
        }
        doRead();
    }

    void doUpdate()
    {
        BMCWEB_LOG_DEBUG("sending {} bytes to PLDM", bytesWritten);

        const std::string serviceName = "xyz.openbmc_project.PLDM";
        const std::string objectPath = "/xyz/openbmc_project/software/pldm";

        memfd.rewind();
        sdbusplus::message::unix_fd fd(memfd.fd);

        dbus::utility::async_method_call(
            [asyncResp{asyncResp}, payload = std::move(payload),
             fileGetSocket{std::move(fileGetSocket)}, objectPath,
             onResponseReady{onResponseReady}](
                const boost::system::error_code& ec1,
                const sdbusplus::object_path& retPath) mutable {
                nvidia::handleStartUpdate(asyncResp, std::move(payload),
                                          objectPath, ec1, retPath,
                                          onResponseReady);
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
    std::function<void()> onResponseReady)
{
    BMCWEB_LOG_DEBUG("Starting PLDM update for {} targets", targets.size());

    std::shared_ptr<PLDMUpdateCtx> pldmUpdateCtx =
        std::make_shared<PLDMUpdateCtx>(
            asyncResp, std::move(payload), std::move(fileGetSocket), applyTime,
            forceUpdate, targets, std::move(onResponseReady));
    pldmUpdateCtx->doRead();
}

inline void afterGetSubtreePathsSoftware(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload&& payload,
    const std::shared_ptr<boost::asio::local::stream_protocol::socket>&
        fileGetSocket,
    const std::string& updateUriTarget, const std::string& dbusApplyTime,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& swInvPaths,
    std::function<void()> onResponseReady)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get software inventory: {}", ec);
        messages::internalError(asyncResp->res);
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
                            std::move(onResponseReady));
        return;
    }

    messages::resourceNotFound(asyncResp->res,
                               "SoftwareInventory.v1_4_0.SoftwareInventory",
                               updateUriTarget);
}

inline void afterGetSubtreePaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Payload&& payload,
    const std::shared_ptr<boost::asio::local::stream_protocol::socket>&
        fileGetSocket,
    const std::string& dbusApplyTime, bool forceUpdate,
    const std::vector<std::string>& uriTargets,
    const boost::system::error_code& ec,
    const std::vector<std::string>& swInvPaths,
    std::function<void()> onResponseReady)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get software inventory: {}", ec);
        messages::internalError(asyncResp->res);
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

    if (areTargetsInvalidOrUnupdatable(uriTargets, updateableFw, swInvPaths,
                                       validTargets))
    {
        BMCWEB_LOG_ERROR("Invalid targets provided");
        messages::invalidObject(asyncResp->res,
                                boost::urls::url_view("Targets"));
        return;
    }

    startPLDMUpdate(asyncResp, std::move(payload), std::move(*fileGetSocket),
                    dbusApplyTime, forceUpdate, validTargets,
                    std::move(onResponseReady));
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
    }

    using SelfPtr = std::shared_ptr<UpdateCtx>;
    enum class State
    {
        WAITING_FOR_UPDATE_PARAMETERS_HEADERS,
        WAITING_FOR_UPDATE_PARAMETERS_DATA,
        WAITING_FOR_UPDATE_FILE_HEADERS,
        WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE,
        WAITING_FOR_UPDATE_FILE_DATA,
        WAITING_FOR_HTTP_CLIENT_DATA_SEND,
        UPDATE_COMPLETE,
        UPDATE_COMPLETE_ERROR
    };

    State state = State::WAITING_FOR_UPDATE_PARAMETERS_HEADERS;
    // TODO, replace this with bmcweb sax json parser
    std::string updateParametersString;
    std::optional<MultiPartUpdate::UpdateParameters> updateParameters;

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
    MultiPartUpdate multiRet;

    std::string pendingFileDataBuffer;
    bool fileSectionComplete = false;
    bool parseComplete = false;

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

    // Ending a streamInput response before the inbound body is fully consumed
    // desyncs HTTP framing (leftover body is parsed as the next request).
    void endClientResponseIfReady()
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
        asyncResp->res.end();
    }

    std::function<void()> responseReadyCallback()
    {
        return [self(shared_from_this())]() {
            self->responseReady = true;
            self->endClientResponseIfReady();
        };
    }

    void failClientResponse()
    {
        state = State::UPDATE_COMPLETE_ERROR;
        responseReady = true;
        endClientResponseIfReady();
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
            // The downstream socket is gone; discard the rest of the body and
            // keep reading so parseComplete can fire and the response ends.
            state = State::UPDATE_COMPLETE_ERROR;
            if (resumeReadCb)
            {
                resumeReadCb();
            }
            endClientResponseIfReady();
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
                return;
            }
            if (targetType == TargetType::Local)
            {
                if (!satelliteTargetsOut.empty())
                {
                    redfish::messages::actionParameterValueConflict(
                        asyncResp->res, "Targets", uri);
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

        localUpdate(localTargetsOut);
    }

    void onHeadersComplete(const SelfPtr& /*self*/,
                           const boost::beast::http::fields& fields,
                           size_t remaingingBodyLength)
    {
        if (state == State::WAITING_FOR_UPDATE_PARAMETERS_HEADERS)
        {
            BMCWEB_LOG_DEBUG("Update Parameters headers complete");

            if (!parseContentDisposition(fields, "UpdateParameters"))
            {
                BMCWEB_LOG_ERROR(
                    "UpdateParameters part has invalid Content-Disposition");
                messages::unrecognizedRequestBody(asyncResp->res);
                state = State::UPDATE_COMPLETE_ERROR;
                return;
            }
            if (!parseContentType(fields))
            {
                BMCWEB_LOG_ERROR(
                    "UpdateParameters part missing or invalid Content-Type");
                messages::headerMissing(asyncResp->res, "Content-Type");
                state = State::UPDATE_COMPLETE_ERROR;
                return;
            }
            state = State::WAITING_FOR_UPDATE_PARAMETERS_DATA;
            return;
        }
        if (state == State::WAITING_FOR_UPDATE_FILE_HEADERS)
        {
            BMCWEB_LOG_DEBUG("Update File headers complete");
            if (!parseContentDisposition(fields, "UpdateFile"))
            {
                BMCWEB_LOG_ERROR("Failed to parse Content-Disposition");
                messages::unrecognizedRequestBody(asyncResp->res);
                state = State::UPDATE_COMPLETE_ERROR;
                return;
            }

            // startRequest() is responsible for the next state transition:
            // the satellite path moves to WAITING_FOR_SAT_CONTROLLER_INFO_
            // COMPLETE, the local path moves straight to
            // WAITING_FOR_UPDATE_FILE_DATA via beginLocalFileStreaming().
            startRequest(remaingingBodyLength);
            return;
        }

        BMCWEB_LOG_CRITICAL("Unexpected state: {}", static_cast<int>(state));
    }

    void onDataAvailable(const SelfPtr& /*self*/, std::string_view data)
    {
        if (state == State::WAITING_FOR_UPDATE_PARAMETERS_DATA)
        {
            if (updateParametersString.size() + data.size() > 8192U)
            {
                BMCWEB_LOG_ERROR(
                    "Update parameters data exceeds content length, stopping parse");
                state = State::UPDATE_COMPLETE;
                return;
            }

            updateParametersString += data;
            return;
        }
        if (state == State::WAITING_FOR_UPDATE_FILE_DATA)
        {
            // BMCWEB_LOG_DEBUG("Update file data available: {}", data);
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
                return;
            }

            multiRet.params = std::move(*params);
            onUpdateParametersComplete(multiRet);
            if (pauseReadCb)
            {
                pauseReadCb();
            }
            state = State::WAITING_FOR_UPDATE_FILE_HEADERS;
            return;
        }
        if (state == State::WAITING_FOR_UPDATE_FILE_DATA)
        {
            BMCWEB_LOG_DEBUG("Update file complete");
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
        closeSendSocketIfReady();
        endClientResponseIfReady();
    }

    void onHttpClientDataSendComplete(
        const std::shared_ptr<UpdateCtx>& /*self*/, const std::string& prefix,
        bool /*keepAlive*/, int32_t /*connId*/, crow::Response& res)
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

        redfish::RedfishAggregator::processResponse(prefix, asyncResp, res);
        responseReady = true;
        endClientResponseIfReady();
    }

    void onUpdateParametersComplete(MultiPartUpdate& multipart)
    {
        std::string applyTime = "OnReset";
        if (multipart.params.applyTime)
        {
            applyTime = *multipart.params.applyTime;
        }

        std::string dbusApplyTime;
        if (!convertApplyTime(asyncResp->res, applyTime, dbusApplyTime))
        {
            return;
        }
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
            updateParametersJson["ApplyTime"] = *multiRet.params.applyTime;
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
            headers.set(field::content_disposition,
                        "form-data; name=\"UpdateFile\"");
            headers.set(field::content_type, "application/octet-stream");
            multipartSerializer.beginPart(headers);
            BMCWEB_LOG_DEBUG("Putting update file headers");
        }
    }

    void satControllerGetComplete(
        const SelfPtr& /*self*/,
        const std::vector<std::string>& localTargetsOut,
        size_t remainingBodyLength,
        const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
    {
        BMCWEB_LOG_DEBUG("Satellite controller get complete");
        if (satelliteInfo.empty())
        {
            BMCWEB_LOG_ERROR("No satellite BMC configs found.");
            messages::internalError(asyncResp->res);
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

        conn.callback =
            std::bind_front(&UpdateCtx::onHttpClientDataSendComplete, this,
                            shared_from_this(), satelliteInfo.begin()->first);

        conn.req.target("/redfish/v1/UpdateService/update-multipart");
        BMCWEB_LOG_DEBUG(
            "Starting request to satellite: {}/redfish/v1/UpdateService/update-multipart",
            host.buffer());

        conn.req.set(boost::beast::http::field::host,
                     host.encoded_host_address());

        conn.req.method(boost::beast::http::verb::post);
        boost::system::error_code ec2;
        conn.req.body().setFd(fileGetSocket.release(), ec2);
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
        closeSendSocketIfReady();
    }

    bool handleSoftwareUpdate(
        const std::string& dbusApplyTime,
        const std::vector<std::string>& uriTargets,
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
             onResponseReady{responseReadyCallback()}](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse&
                    swInvPaths) mutable {
                afterGetSubtreePathsSoftware(
                    asyncResp, std::move(payload), fileGetSocketPtr, softwareId,
                    dbusApplyTime, ec, swInvPaths, std::move(onResponseReady));
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
            return;
        }
        bool forceUpdate = multiRet.params.forceUpdate.value_or(false);

        if (uriTargets.empty())
        {
            std::vector<sdbusplus::object_path> emptyTargets{};
            nvidia::startPLDMUpdate(asyncResp, std::move(payload),
                                    std::move(fileGetSocket), dbusApplyTime,
                                    forceUpdate, emptyTargets,
                                    responseReadyCallback());
            beginLocalFileStreaming();
            return;
        }
        std::shared_ptr<boost::asio::local::stream_protocol::socket>
            fileGetSocketPtr =
                std::make_shared<boost::asio::local::stream_protocol::socket>(
                    std::move(fileGetSocket));

        // TODO Need to clean up the IST dbus paths so we can use the normal
        // call
        if (handleSoftwareUpdate(dbusApplyTime, uriTargets, fileGetSocketPtr))
        {
            beginLocalFileStreaming();
            return;
        }

        BMCWEB_LOG_DEBUG("Getting firmware inventory for {} targets",
                         uriTargets.size());
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/software", 0,
            std::array<std::string_view, 2>{
                "xyz.openbmc_project.Software.Version"},
            [asyncResp{asyncResp}, payload = std::move(payload),
             fileGetSocketPtr, dbusApplyTime, forceUpdate, uriTargets,
             onResponseReady{responseReadyCallback()}](
                const boost::system::error_code& ec,
                const std::vector<std::string>& swInvPaths) mutable {
                afterGetSubtreePaths(asyncResp, std::move(payload),
                                     fileGetSocketPtr, dbusApplyTime,
                                     forceUpdate, uriTargets, ec, swInvPaths,
                                     std::move(onResponseReady));
            });
        beginLocalFileStreaming();
    }
};

inline void handleUpdateServiceMultipartUpdatePostHeaders(
    crow::Request& req, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Configuring multipart parser callbacks");
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
