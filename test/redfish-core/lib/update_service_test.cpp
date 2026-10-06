// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "nvidia_update_service.hpp"
#include "update_service.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/beast/http/status.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(UpdateService, ParseHTTSPPostitive)
{
    crow::Response res;
    {
        // No protocol, schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", std::nullopt, res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Protocol, no schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
}

TEST(UpdateService, ParseHTTPSPostitive)
{
    crow::Response res;
    {
        // No protocol, schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", std::nullopt, res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Protocol, no schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url with path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url without path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url without path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://[2001:db8::1]", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "[2001:db8::1]");
        EXPECT_EQ(ret->encoded_path(), "/");
        EXPECT_EQ(ret->scheme(), "https");
    }
}

TEST(UpdateService, ParseHTTPSNegative)
{
    crow::Response res;
    // No protocol, no schema
    ASSERT_EQ(parseSimpleUpdateUrl("1.1.1.1/path", std::nullopt, res),
              std::nullopt);
    // No host
    ASSERT_EQ(parseSimpleUpdateUrl("/path", "HTTPS", res), std::nullopt);
}

// A Software.Version object that omits the deprecated Purpose property must
// still succeed: the handler must not fail the resource and must skip the
// Purpose-derived Description.
TEST(UpdateService, MissingPurposeIsToleratedAndSkipsDescription)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap props = {
        {"Version", std::string("anyVersion")}};
    getSoftwareVersionCallback(asyncResp, "swId", {}, props);

    EXPECT_NE(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("Description"));
}

// When Purpose is present, Description is the trailing token of the
// VersionPurpose enum string plus " image"; exercise that transform.
TEST(UpdateService, PurposeIsTranslatedToDescription)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap props = {
        {"Version", std::string("anyVersion")},
        {"Purpose", std::string("xyz.openbmc_project.Software.Version."
                                "VersionPurpose.BMC")}};
    getSoftwareVersionCallback(asyncResp, "swId", {}, props);

    EXPECT_NE(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_EQ(asyncResp->res.jsonValue["Description"], "BMC image");
}

// Version is mandatory; its absence is an internal error.
TEST(UpdateService, MissingVersionIsError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    getSoftwareVersionCallback(asyncResp, "swId", {},
                               dbus::utility::DBusPropertiesMap{});

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

// Nvidia code starts here

std::optional<MultiPartUpdate::UpdateParameters> parseParams(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view content)
{
    return processUpdateParameters(asyncResp, content);
}

// Property type errors are annotated on the offending property
// ("<property>@Message.ExtendedInfo"); unknown-property errors land in the
// top-level error object. Return the first MessageId from either place.
nlohmann::json firstMessageId(crow::Response& res)
{
    for (const auto& [key, value] : res.jsonValue.items())
    {
        if (key.ends_with("@Message.ExtendedInfo"))
        {
            return value[0]["MessageId"];
        }
    }
    return res.jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"];
}

TEST(ProcessUpdateParameters, PreUpdateValidationTrueParses)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp,
        R"({"Targets":[],"ForceUpdate":false,"Oem":{"Nvidia":{"PreUpdateValidation":true}}})");
    ASSERT_TRUE(ret);
    if (!ret)
    {
        return;
    }
    EXPECT_EQ(ret->preUpdateValidation, true);
}

TEST(ProcessUpdateParameters, PreUpdateValidationFalseParses)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp, R"({"Oem":{"Nvidia":{"PreUpdateValidation":false}}})");
    ASSERT_TRUE(ret);
    if (!ret)
    {
        return;
    }
    EXPECT_EQ(ret->preUpdateValidation, false);
}

TEST(ProcessUpdateParameters, PreUpdateValidationAbsentLeavesOptionUnset)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Targets":[],"ForceUpdate":true})");
    ASSERT_TRUE(ret);
    if (!ret)
    {
        return;
    }
    EXPECT_EQ(ret->preUpdateValidation, std::nullopt);
}

TEST(ProcessUpdateParameters, NonBooleanPreUpdateValidationRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp, R"({"Oem":{"Nvidia":{"PreUpdateValidation":"yes"}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, IntegerPreUpdateValidationRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret = parseParams(
        asyncResp, R"({"Oem":{"Nvidia":{"PreUpdateValidation":5}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, NonObjectOemRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":true})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, NonObjectNvidiaRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":{"Nvidia":"invalid"}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res),
              "Base.1.19.PropertyValueTypeError");
}

TEST(ProcessUpdateParameters, UnrelatedOemVendorRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":{"OtherVendor":{"Flag":true}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res), "Base.1.19.PropertyUnknown");
}

TEST(ProcessUpdateParameters, UnknownNvidiaOemKeyRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::optional<MultiPartUpdate::UpdateParameters> ret =
        parseParams(asyncResp, R"({"Oem":{"Nvidia":{"Flag":true}}})");
    EXPECT_EQ(ret, std::nullopt);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(firstMessageId(asyncResp->res), "Base.1.19.PropertyUnknown");
}

TEST(MergeUpdateParameters, PreUpdateValidationCarriedIntoMultipartParams)
{
    MultiPartUpdate::UpdateParameters dest;
    MultiPartUpdate::UpdateParameters src;
    src.preUpdateValidation = true;
    mergeUpdateParameters(dest, src);
    EXPECT_EQ(dest.preUpdateValidation, true);

    // An UpdateParameters part that omits the option must not clear it.
    mergeUpdateParameters(dest, MultiPartUpdate::UpdateParameters{});
    EXPECT_EQ(dest.preUpdateValidation, true);
}

// A staging directory for one test, removed with whatever the test left in
// it.  path is empty if it could not be created.
class StagingDir
{
  public:
    StagingDir() :
        path(std::format("{}/bmcweb-fw-staging-XXXXXX",
                         std::filesystem::temp_directory_path().string()))
    {
        // mkdtemp() is POSIX, declared by <stdlib.h>; <cstdlib> provides it.
        // NOLINTNEXTLINE(misc-include-cleaner)
        if (mkdtemp(path.data()) == nullptr)
        {
            path.clear();
        }
    }
    ~StagingDir()
    {
        if (!path.empty())
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    }
    StagingDir(const StagingDir&) = delete;
    StagingDir(StagingDir&&) = delete;
    StagingDir& operator=(const StagingDir&) = delete;
    StagingDir& operator=(StagingDir&&) = delete;

    std::string path;
};

bool isMemfdNamed(int fd, std::string_view name)
{
    std::array<char, 256> target{};
    const std::string fdPath = std::format("/proc/self/fd/{}", fd);
    ssize_t size = readlink(fdPath.c_str(), target.data(), target.size());
    if (size <= 0)
    {
        return false;
    }
    return std::string_view(target.data(), static_cast<size_t>(size))
        .starts_with(std::format("/memfd:{}", name));
}

TEST(MemoryFileDescriptor, StagesWrittenData)
{
    MemoryFileDescriptor file("test-image");
    ASSERT_GE(file.fd, 0);
    constexpr std::string_view payload = "firmware-bytes";
    ASSERT_EQ(write(file.fd, payload.data(), payload.size()),
              static_cast<ssize_t>(payload.size()));

    EXPECT_TRUE(finalizeFirmwareStagingFd(file.fd, file.fileBacked));
    ASSERT_TRUE(file.rewind());
    std::string readBack(payload.size(), '\0');
    EXPECT_EQ(read(file.fd, readBack.data(), readBack.size()),
              static_cast<ssize_t>(payload.size()));
    EXPECT_EQ(readBack, payload);
}

TEST(MemoryFileDescriptor, IsMoveOnly)
{
    static_assert(!std::is_copy_constructible_v<MemoryFileDescriptor>);
    static_assert(!std::is_copy_assignable_v<MemoryFileDescriptor>);
    static_assert(std::is_nothrow_move_constructible_v<MemoryFileDescriptor>);
    static_assert(std::is_move_assignable_v<MemoryFileDescriptor>);

    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    MemoryFileDescriptor src(openFirmwareStagingFdIn(dir.path, "test-image"));
    ASSERT_GE(src.fd, 0);
    ASSERT_TRUE(src.fileBacked);
    const int fd = src.fd;
    MemoryFileDescriptor dst(std::move(src));
    EXPECT_EQ(dst.fd, fd);
    EXPECT_TRUE(dst.fileBacked);
    EXPECT_EQ(src.fd, -1); // NOLINT(bugprone-use-after-move)
}

TEST(MemoryFileDescriptor, MoveAssignmentClosesPrevious)
{
    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    MemoryFileDescriptor a("a");
    MemoryFileDescriptor b(openFirmwareStagingFdIn(dir.path, "b"));
    ASSERT_GE(a.fd, 0);
    ASSERT_GE(b.fd, 0);
    ASSERT_TRUE(b.fileBacked);
    int previousFdOfA = a.fd;
    int newFd = b.fd;

    a = std::move(b);
    EXPECT_EQ(a.fd, newFd);
    EXPECT_TRUE(a.fileBacked);
    EXPECT_EQ(b.fd, -1); // NOLINT(bugprone-use-after-move)

    // The descriptor a held before must be closed, so writing to it fails
    // with EBADF.  Nothing in this single-threaded test opens a descriptor
    // between the assignment and the write, so the number cannot have been
    // reused.
    errno = 0;
    char zero = 0;
    const ssize_t rc = ::write(previousFdOfA, &zero, 1);
    const int err = errno;
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(err, EBADF);
}

TEST(MemoryFileDescriptor, IsAMemfdWithoutTheStagingDirectory)
{
    std::error_code ec;
    if (std::filesystem::exists(firmwareStagingDirectory, ec))
    {
        GTEST_SKIP() << firmwareStagingDirectory << " exists on this host";
    }
    MemoryFileDescriptor file("probe");
    ASSERT_GE(file.fd, 0);
    EXPECT_FALSE(file.fileBacked);
    EXPECT_TRUE(isMemfdNamed(file.fd, "probe"));
}

TEST(FirmwareStaging, StagingDirectoryGivesAnAnonymousFile)
{
    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    FirmwareStagingFd staged = openFirmwareStagingFdIn(dir.path, "probe");
    ASSERT_GE(staged.fd, 0);
    EXPECT_TRUE(staged.fileBacked);
    struct stat fileStat{};
    ASSERT_EQ(fstat(staged.fd, &fileStat), 0);
    EXPECT_TRUE(S_ISREG(fileStat.st_mode));
    EXPECT_EQ(fileStat.st_nlink, 0U);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    EXPECT_NE(fcntl(staged.fd, F_GETFD) & FD_CLOEXEC, 0);
    // Nothing in the directory is reachable by name.
    EXPECT_TRUE(std::filesystem::is_empty(dir.path));
    close(staged.fd);
}

TEST(FirmwareStaging, LinkedDirectoryStagesInItsTarget)
{
    // The platform can provide the directory as a link to its storage.
    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    const std::string target = dir.path + "/target";
    const std::string link = dir.path + "/link";
    ASSERT_EQ(mkdir(target.c_str(), 0700), 0);
    ASSERT_EQ(symlink(target.c_str(), link.c_str()), 0);

    FirmwareStagingFd staged = openFirmwareStagingFdIn(link, "probe");
    ASSERT_GE(staged.fd, 0);
    EXPECT_TRUE(staged.fileBacked);
    EXPECT_FALSE(isMemfdNamed(staged.fd, "probe"));
    close(staged.fd);
}

TEST(FirmwareStaging, SpaceCheckAppliesOnlyToFileBackedStaging)
{
    constexpr uint64_t tooMuch = std::numeric_limits<uint64_t>::max();
    MemoryFileDescriptor memfd("test-image");
    ASSERT_GE(memfd.fd, 0);
    EXPECT_TRUE(firmwareStagingHasSpaceFor(memfd.fd, false, tooMuch));

    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    MemoryFileDescriptor file(openFirmwareStagingFdIn(dir.path, "test-image"));
    ASSERT_GE(file.fd, 0);
    ASSERT_TRUE(file.fileBacked);
    EXPECT_TRUE(firmwareStagingHasSpaceFor(file.fd, true, 0U));
    EXPECT_FALSE(firmwareStagingHasSpaceFor(file.fd, true, tooMuch));
}

TEST(FirmwareStaging, FlushFailsOnlyForFileBackedStaging)
{
    // fdatasync() fails on a character device.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    ASSERT_GE(fd, 0);
    EXPECT_FALSE(finalizeFirmwareStagingFd(fd, true));
    EXPECT_TRUE(finalizeFirmwareStagingFd(fd, false));
    close(fd);

    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    MemoryFileDescriptor file(openFirmwareStagingFdIn(dir.path, "test-image"));
    ASSERT_GE(file.fd, 0);
    constexpr std::string_view payload = "firmware-bytes";
    ASSERT_EQ(write(file.fd, payload.data(), payload.size()),
              static_cast<ssize_t>(payload.size()));
    EXPECT_TRUE(finalizeFirmwareStagingFd(file.fd, file.fileBacked));
}

TEST(FirmwareStaging, OnlyAFullStagingFilesystemIsInsufficientStorage)
{
    for (const bool fileBacked : {false, true})
    {
        crow::Response full;
        firmwareStagingWriteError(full, ENOSPC, fileBacked);
        EXPECT_EQ(full.resultInt(), fileBacked ? 507 : 500);

        crow::Response quota;
        // NOLINTNEXTLINE(misc-include-cleaner)
        firmwareStagingWriteError(quota, EDQUOT, fileBacked);
        EXPECT_EQ(quota.resultInt(), fileBacked ? 507 : 500);

        crow::Response ioError;
        firmwareStagingWriteError(ioError, EIO, fileBacked);
        EXPECT_EQ(ioError.resultInt(), 500);
    }
}

TEST(FirmwareStaging, MissingDirectoryFallsBackToMemfd)
{
    // A link to a directory that is not there yet is how a platform whose
    // staging partition is not mounted looks.
    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    const std::string danglingLink = dir.path + "/link";
    ASSERT_EQ(symlink((dir.path + "/unmounted").c_str(), danglingLink.c_str()),
              0);

    for (const std::string& missing :
         {std::string("/nonexistent-bmcweb-fw-staging"),
          std::string("/dev/null/fw-staging"), danglingLink})
    {
        FirmwareStagingFd staged = openFirmwareStagingFdIn(missing, "probe");
        ASSERT_GE(staged.fd, 0) << missing;
        EXPECT_FALSE(staged.fileBacked) << missing;
        EXPECT_TRUE(isMemfdNamed(staged.fd, "probe")) << missing;
        // The space check and the flush must not fail the fallback.
        EXPECT_TRUE(firmwareStagingHasSpaceFor(
            staged.fd, staged.fileBacked,
            firmwareImageLimitBytes + firmwareStagingReserveBytes));
        constexpr std::string_view payload = "firmware-bytes";
        ASSERT_EQ(write(staged.fd, payload.data(), payload.size()),
                  static_cast<ssize_t>(payload.size()));
        EXPECT_TRUE(finalizeFirmwareStagingFd(staged.fd, staged.fileBacked));
        close(staged.fd);
    }
}

TEST(FirmwareStaging, UnreachableDirectoryFallsBackToMemfd)
{
    if (geteuid() == 0)
    {
        GTEST_SKIP() << "root ignores the directory mode";
    }
    // A staging directory bmcweb cannot see is not provided to it.
    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    const std::string closed = dir.path + "/closed";
    const std::string staging = closed + "/fw-staging";
    ASSERT_EQ(mkdir(closed.c_str(), 0700), 0);
    ASSERT_EQ(mkdir(staging.c_str(), 0700), 0);
    ASSERT_EQ(chmod(closed.c_str(), 0), 0);

    FirmwareStagingFd staged = openFirmwareStagingFdIn(staging, "probe");
    EXPECT_GE(staged.fd, 0);
    EXPECT_FALSE(staged.fileBacked);
    if (staged.fd >= 0)
    {
        close(staged.fd);
    }
    EXPECT_EQ(chmod(closed.c_str(), 0700), 0);
}

TEST(FirmwareStaging, UnwritableDirectoryDoesNotFallBack)
{
    if (geteuid() == 0)
    {
        GTEST_SKIP() << "root ignores the directory mode";
    }
    StagingDir dir;
    ASSERT_FALSE(dir.path.empty());
    ASSERT_EQ(chmod(dir.path.c_str(), 0500), 0);
    FirmwareStagingFd staged = openFirmwareStagingFdIn(dir.path, "probe");
    EXPECT_EQ(staged.fd, -1);
    if (staged.fd >= 0)
    {
        close(staged.fd);
    }
    EXPECT_EQ(chmod(dir.path.c_str(), 0700), 0);
}
// Nvidia code ends here
} // namespace
} // namespace redfish
