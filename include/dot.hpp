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

#include "nvidia_error_messages.hpp"
#include "nvidia_messages.hpp"
#include "utils/mctp_utils.hpp"

#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/asio.hpp>
#include <boost/interprocess/streams/bufferstream.hpp>
//#include <boost/process/child.hpp>
//#include <boost/process/pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/asio/connect_pipe.hpp>
#include <memory>
#include <numeric>
#include <span>
#include <vector>

namespace redfish
{

namespace dot
{

constexpr const size_t mctpVdmUtilOutputSize = 256;
constexpr const size_t mctpVdmUtilResponseSize = 9;
constexpr const size_t mctpVdmUtilDotDataSize = 1;
constexpr const size_t mctpVdmUtilCakTestDataSize = 3;
// defined in libmctp project in vdm/nvidia/libmctp-vdm-cmds.h
constexpr const size_t dotKeySize = 96;
// related to mctp_vendor_cmd_cak_install structure size in libmctp
constexpr const size_t dotCakInstallDataSize = dotKeySize + 98;
constexpr const size_t dotTokenSize = 256;

enum DotMctpVdmUtilCommand
{
    CAKInstall,
    CAKLock,
    CAKTest,
    DOTDisable,
    DOTTokenInstall,
};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::map<const DotMctpVdmUtilCommand, const std::string> commandsMap{
    {DotMctpVdmUtilCommand::CAKInstall, "cak_install"},
    {DotMctpVdmUtilCommand::CAKLock, "cak_lock"},
    {DotMctpVdmUtilCommand::CAKTest, "cak_test"},
    {DotMctpVdmUtilCommand::DOTDisable, "dot_disable"},
    {DotMctpVdmUtilCommand::DOTTokenInstall, "dot_token_install"}};

using ResultCallback = std::function<void(
    const std::string& /* data received from the MCTP endpoint */)>;
using ErrorCallback = std::function<void(
    const std::string& /* resource / procedure associated with the error */,
    const std::string& /* error message */)>;
class DotCommandHandler
{
  public:
    DotCommandHandler(const std::string& erot,
                      const DotMctpVdmUtilCommand command,
                      const std::vector<uint8_t>& data,
                      ResultCallback&& resultCallbackIn,
                      ErrorCallback&& errorCallbackIn, int timeoutSec = 3) :
        externalResultCallback(std::move(resultCallbackIn)),
        externalErrorCallback(std::move(errorCallbackIn)),
        subprocessTimeout(timeoutSec)
    {
        mctp_utils::enumerateMctpEndpoints(
            [this, command,
             data](const std::shared_ptr<std::vector<mctp_utils::MctpEndpoint>>&
                       endpoints) {
                if (endpoints && !endpoints->empty())
                {
                    runCommand(endpoints->begin()->getMctpEid(), command, data);
                }
                else
                {
                    errorCallback("Endpoint enumeration", "no endpoints found");
                }
            },
            [this](bool critical, const std::string& desc,
                   const std::string& msg) {
                if (critical)
                {
                    errorCallback(desc, msg);
                }
            },
            erot);
    }

    bool finished() const
    {
        bool isFinished = !subprocess && !subprocessTimer &&
                          !externalResultCallback && !externalErrorCallback;
        return isFinished;
    }

  private:
    ResultCallback externalResultCallback;
    ErrorCallback externalErrorCallback;

    std::optional<boost::process::v2::process> subprocess;
    std::unique_ptr<boost::asio::readable_pipe> outRead;
    std::vector<char> subprocessOutput;
    std::array<char, 4096> outBuf{};
    bool readDone{false};
    int lastExitCode{0};

    int subprocessTimeout;
    std::unique_ptr<boost::asio::steady_timer> subprocessTimer;

    void resultCallback(const std::string& data)
    {
        if (externalResultCallback)
        {
            externalResultCallback(data);
        }
        if (!finished())
        {
            cleanup();
        }
    }

    void errorCallback(const std::string& arg1, const std::string& arg2)
    {
        if (externalErrorCallback)
        {
            externalErrorCallback(arg1, arg2);
        }
        if (!finished())
        {
            cleanup();
        }
    }

    void subprocessExitCallback(int exitCode, const std::error_code& ec)
    {
        const std::string desc = "mctp-vdm-util execution exit callback";
        BMCWEB_LOG_DEBUG("{}", desc);
        if (ec)
        {
            errorCallback(desc, ec.message());
        }
        else if (exitCode == 0)
        {
            boost::interprocess::bufferstream outputStream(
                subprocessOutput.data(), subprocessOutput.size());
            std::string line;
            std::string rxLine;
            std::string txLine;
            std::string output;
            while (std::getline(outputStream, line) && output.empty())
            {
                if (line.starts_with("RX: "))
                {
                    rxLine = line.substr(4);
                    BMCWEB_LOG_DEBUG(" RX: {}", rxLine);
                }
                if (line.starts_with("TX: "))
                {
                    txLine = line.substr(4);
                    BMCWEB_LOG_DEBUG(" TX: {}", txLine);
                }
                if (!rxLine.empty() && !txLine.empty())
                {
                    output = rxLine;
                }
            }
            if (output.empty())
            {
                errorCallback(desc, "no RX data found");
            }
            else
            {
                resultCallback(rxLine);
            }
        }
        else
        {
            errorCallback(desc, "Exit code: " + std::to_string(exitCode));
        }
    }

    void runCommand(const int eid, const DotMctpVdmUtilCommand command,
                    const std::vector<uint8_t>& data)
    {
        const std::string desc = "mctp-vdm-util execution";
        BMCWEB_LOG_DEBUG("{}", desc);
        subprocessTimer = std::make_unique<boost::asio::steady_timer>(
            crow::connections::systemBus->get_io_context());
        subprocessTimer->expires_after(std::chrono::seconds(subprocessTimeout));
        subprocessTimer->async_wait(
            [this, desc](const boost::system::error_code& ec) {
                if (ec && ec != boost::asio::error::operation_aborted)
                {
                    subprocess.reset();
                    errorCallback(desc, "Timeout");
                }
            });

        std::vector<std::string> args = {"-c", commandsMap.at(command), "-t",
                                         std::to_string(eid)};
        for (const auto& byte : data)
        {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setw(2)
               << std::setfill('0') << static_cast<int>(byte);
            args.emplace_back(ss.str());
        }
        BMCWEB_LOG_DEBUG("mctp-vdm-util {}", boost::algorithm::join(args, " "));
        try
        {
            namespace bpv2 = boost::process::v2;
            auto& io = crow::connections::systemBus->get_io_context();

            // Prepare output buffer (reserve extra for hex payload)
            size_t hexDataSize = data.size() * 3; // hex representation + space
            subprocessOutput.clear();
            subprocessOutput.reserve(mctpVdmUtilOutputSize + hexDataSize);

            // Pipe for stdout
            boost::asio::readable_pipe rp(io);
            boost::asio::writable_pipe wp(io);
            boost::asio::connect_pipe(rp, wp);
            outRead = std::make_unique<boost::asio::readable_pipe>(std::move(rp));

            // Configure stdio (ignore stderr as before)
            bpv2::process_stdio stdio{
                .in = nullptr,
                .out = std::move(wp),
                .err = nullptr,
            };

            // Launch process
            subprocess.emplace(io, "/usr/bin/mctp-vdm-util", args, stdio);

            // Async read all stdout
            auto self = this;
            std::function<void()> readLoop;
            readLoop = [self, &readLoop]() {
                self->outRead->async_read_some(
                    boost::asio::buffer(self->outBuf),
                    [self, &readLoop](const boost::system::error_code& ec, std::size_t n) {
                        if (!ec)
                        {
                            std::span<const char> chunk(self->outBuf.data(), n);
                            self->subprocessOutput.insert(
                                self->subprocessOutput.end(), chunk.begin(),
                                chunk.end());
                            readLoop();
                            return;
                        }
                        // done or error – mark read complete
                        self->readDone = true;
                        // Try finish if process already exited
                        if (!self->subprocess.has_value())
                        {
                            self->subprocessExitCallback(self->lastExitCode, std::error_code{});
                        }
                    });
            };
            readLoop();

            // Wait for process to exit
            subprocess->async_wait([this](const boost::system::error_code& ec, int code) {
                lastExitCode = code;
                // Clear process to indicate exit happened
                subprocess.reset();
                if (readDone)
                {
                    // both read and exit done
                    subprocessExitCallback(code, std::error_code(ec.value(), std::generic_category()));
                }
            });
        }
        catch (const std::runtime_error& e)
        {
            errorCallback(desc, e.what());
        }
    }

    void cleanup()
    {
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          [this] {
                              subprocessOutput.resize(0);
                              outRead.reset(nullptr);
                              subprocess.reset();
                              subprocessTimer.reset();
                              externalErrorCallback = nullptr;
                              externalResultCallback = nullptr;
                          });
    }
};

inline std::string getCommandDescription(DotMctpVdmUtilCommand command)
{
    static const std::map<const DotMctpVdmUtilCommand, const std::string> desc{
        {DotMctpVdmUtilCommand::CAKInstall, "DOT CAK install"},
        {DotMctpVdmUtilCommand::CAKLock, "DOT CAK lock"},
        {DotMctpVdmUtilCommand::CAKTest, "DOT CAK test"},
        {DotMctpVdmUtilCommand::DOTDisable, "DOT disable"},
        {DotMctpVdmUtilCommand::DOTTokenInstall, "DOT token install"}};
    try
    {
        return desc.at(command);
    }
    catch (...)
    {
        return "Invalid command";
    }
}

inline std::string getMctpStatusCodeDescription(int code)
{
    static const std::map<const int, const std::string> status{
        {0x01, "General error"},       {0x02, "Invalid data"},
        {0x03, "Invalid length"},      {0x04, "Not ready"},
        {0x05, "Unsupported command"}, {0x06, "Update not done"},
        {0x21, "No DOT support"},      {0x80, "Message type not supported"},
        {0x81, "Update DB failure"},   {0x82, "Get strap failure"},
        {0x83, "Debug token failure"}, {0x84, "Invalid slot"}};
    try
    {
        return status.at(code);
    }
    catch (...)
    {
        BMCWEB_LOG_ERROR("Unknown DOT MCTP status code: {}", code);
        return "Unknown error";
    }
}

inline std::string getCompletionCodeDescription(int code)
{
    static const std::map<const int, const std::string> cc{
        {1, "DOT disabled"},
        {2, "DOT already enabled"},
        {3, "DOT CAK already installed"},
        {4, "AP_FW signature authentication failed"},
        {5, "DOT CAK locked"},
        {6, "DOT CAK not installed"},
        {7, "DOT CAK lock disabled"},
        {8, "DOT LAK not installed"},
        {9, "DOT invalid rollback revision"},
        {10, "DOT revision revoked already"},
        {11, "SPI error"},
        {12, "Crypto error"},
        {13, "Invalid key"},
        {14, "Invalid token"},
        {15, "Recovery failed"},
        {16, "Verification failed"},
        {17, "All key copies corrupt"},
        {18, "Failed to get key"},
        {19, "Invalid token type"},
        {20, "Unset LAK in FLASH failed"},
        {21, "Unset CAK in FLASH failed"},
        {22, "Seting DOT enabled failed"},
        {23, "Optional meta signature write failed"},
        {24, "Key write failed"},
        {25, "AP FW metadata slot 0 failed"},
        {26, "AP FW metadata slot 1 failed"},
        {27, "AP FW metadata slot 0 and 1 failed"},
        {28, "CAK lock value invalid"},
        {29, "General error"}};
    try
    {
        return cc.at(code);
    }
    catch (...)
    {
        BMCWEB_LOG_ERROR("Unknown DOT completion code: {}", code);
        return "Unknown error";
    }
}

inline bool getBinaryKeyFromPem(const std::string& pem,
                                std::vector<uint8_t>& key)
{
    std::unique_ptr<BIO, decltype(&::BIO_free)> bio{BIO_new(BIO_s_mem()),
                                                    &::BIO_free};
    if (!bio)
    {
        BMCWEB_LOG_ERROR("openssl BIO allocation failed");
        return false;
    }

    size_t written = 0;
    int ret = BIO_write_ex(bio.get(), pem.data(), pem.size(), &written);
    if (ret != 1 || written != pem.size())
    {
        BMCWEB_LOG_ERROR("BIO_write_ex failed");
        return false;
    }

    std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> pubKey{
        PEM_read_bio_PUBKEY(bio.get(), nullptr, lsp::emptyPasswordCallback,
                            nullptr),
        &::EVP_PKEY_free};
    if (!pubKey)
    {
        BMCWEB_LOG_ERROR("PEM_read_bio_PUBKEY failed");
        return false;
    }
    size_t keySize = 0;
    if (EVP_PKEY_get_octet_string_param(pubKey.get(), OSSL_PKEY_PARAM_PUB_KEY,
                                        nullptr, 0, &keySize) != 1)
    {
        BMCWEB_LOG_ERROR("Failed to get public key size");
        return false;
    }
    key.resize(keySize);
    if (EVP_PKEY_get_octet_string_param(pubKey.get(), OSSL_PKEY_PARAM_PUB_KEY,
                                        key.data(), key.size(), &keySize) != 1)
    {
        BMCWEB_LOG_ERROR("Failed to get public key data");
        return false;
    }
    // the first byte contains information about whether the key
    // is compressed as per https://www.rfc-editor.org/rfc/rfc5480#section-2.2
    key.erase(key.begin());
    return true;
}

static inline void executeDotCommand(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, dot::DotMctpVdmUtilCommand command,
    const std::vector<uint8_t>& data)
{
    static std::map<std::string, std::unique_ptr<dot::DotCommandHandler>>
        dotOpMap;
    auto currentOp = dotOpMap.find(chassisID);
    if (currentOp != dotOpMap.end() && !currentOp->second->finished())
    {
        messages::resourceErrorsDetectedFormatError(
            asyncResp->res, dot::getCommandDescription(command),
            "another operation in progress");
        return;
    }
    auto resultHandler = [asyncResp](const std::string& output) {
        std::istringstream iss(output);
        std::vector<std::string> tokens{std::istream_iterator<std::string>{iss},
                                        std::istream_iterator<std::string>{}};
        if (tokens.size() != mctpVdmUtilResponseSize &&
            tokens.size() != mctpVdmUtilResponseSize + mctpVdmUtilDotDataSize)
        {
            BMCWEB_LOG_ERROR("mctp-vdm-util RX data invalid length: {}",
                             output);
            messages::resourceErrorsDetectedFormatError(
                asyncResp->res, "mctp-vdm-util RX data", "invalid length");
            return;
        }
        std::string mctpStatusCodeHexStr;
        std::string dotCompletionCodeHexStr;
        int mctpStatusCode = -1;
        int dotCompletionCode = -1;
        if (tokens.size() == mctpVdmUtilResponseSize + mctpVdmUtilDotDataSize)
        {
            mctpStatusCodeHexStr = tokens[tokens.size() - 2];
            dotCompletionCodeHexStr = tokens[tokens.size() - 1];
        }
        else if (tokens.size() == mctpVdmUtilResponseSize)
        {
            mctpStatusCodeHexStr = tokens[tokens.size() - 1];
        }
        try
        {
            if (!mctpStatusCodeHexStr.empty())
            {
                mctpStatusCode = std::stoi(mctpStatusCodeHexStr, nullptr, 16);
            }
            if (!dotCompletionCodeHexStr.empty())
            {
                dotCompletionCode =
                    std::stoi(dotCompletionCodeHexStr, nullptr, 16);
            }
        }
        catch (...)
        {
            BMCWEB_LOG_ERROR("mctp-vdm-util RX data invalid format: {}",
                             output);
            messages::resourceErrorsDetectedFormatError(
                asyncResp->res, "mctp-vdm-util RX data", "invalid format");
            return;
        }
        if (mctpStatusCode == 0 && dotCompletionCode == 0)
        {
            messages::success(asyncResp->res);
            return;
        }
        if (mctpStatusCode == 1 && dotCompletionCode != -1)
        {
            messages::dotActionResponseError(
                asyncResp->res,
                dot::getCompletionCodeDescription(dotCompletionCode));
            return;
        }
        messages::dotMctpStatusError(
            asyncResp->res, dot::getMctpStatusCodeDescription(mctpStatusCode));
    };
    auto errorHandler =
        [asyncResp](const std::string& desc, const std::string& error) {
            BMCWEB_LOG_ERROR("{}: {}", desc, error);
            messages::resourceErrorsDetectedFormatError(asyncResp->res, desc,
                                                        error);
        };
    auto op = std::make_unique<dot::DotCommandHandler>(
        chassisID, command, data, resultHandler, errorHandler);
    dotOpMap[chassisID] = std::move(op);
}

} // namespace dot

} // namespace redfish
