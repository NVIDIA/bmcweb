#pragma once

#include "xyz/openbmc_project/Logging/Entry/server.hpp"

namespace phosphor
{
namespace logging
{
using EntryIfaces = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Logging::server::Entry>;

/** @class Entry
 *  @brief OpenBMC logging entry implementation.
 *  @details A concrete implementation for the
 *  xyz.openbmc_project.Logging.Entry and
 *  xyz.openbmc_project.Associations.Definitions DBus APIs.
 */
class evtEntry :
    public EntryIfaces,
    public std::enable_shared_from_this<evtEntry>
{
  public:
    /** @brief Constructor to put object onto bus at a dbus path.
     *         Defer signal registration (pass true for deferSignal to the
     *         base class) until after the properties are set.
     *  @param[in] bus - Bus to attach to.
     *  @param[in] path - Path to attach at.
     *  @param[in] idErr - The error entry id.
     *  @param[in] timestampErr - The commit timestamp.
     *  @param[in] severityErr - The severity of the error.
     *  @param[in] msgErr - The message of the error.
     *  @param[in] additionalDataErr - The error metadata.
     */
    evtEntry(sdbusplus::bus::bus& bus, const std::string& path, uint32_t id,
             uint64_t timestamp, Level severity, std::string&& msg,
             std::string&& resolution,
             std::vector<std::string>&& additionalData);
    ~evtEntry();

    /**
     * @brief Returns the file descriptor to the Entry file.
     * @return unix_fd - File descriptor to the Entry file.
     */
    sdbusplus::message::unix_fd getEntry()
    {
        return 0;
    };
};

} // namespace logging
} // namespace phosphor
