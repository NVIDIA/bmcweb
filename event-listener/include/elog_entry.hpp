// NOLINTBEGIN
#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/** @namespace phosphor
 *  @brief The main namespace for OpenBMC phosphor components
 */
namespace phosphor
{
/** @namespace logging
 *  @brief Namespace for OpenBMC logging functionality
 */
namespace logging
{
using EntryIfaces = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Logging::server::Entry>;

/** @class evtEntry
 *  @brief OpenBMC logging event entry implementation.
 *  @details A concrete implementation for the
 *  xyz.openbmc_project.Logging.Entry and
 *  xyz.openbmc_project.Associations.Definitions DBus APIs.
 *  This class uses multiple inheritance:
 *  - EntryIfaces: Provides the D-Bus interface implementation
 *  - std::enable_shared_from_this: Enables safe shared_ptr management for
 * callbacks
 */
class evtEntry final :
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
     *  @param[in] resolution - The resolution information for the error.
     *  @param[in] additionalDataErr - The error metadata.
     */
    evtEntry(sdbusplus::bus::bus& bus, const std::string& path, uint32_t id,
             uint64_t timestamp, Level severity, std::string&& msg,
             std::string&& resolution,
             std::unordered_map<std::string, std::string>&& additionalData);

    /** @brief Destructor */
    ~evtEntry() override;

    // Delete copy and move operations
    evtEntry(const evtEntry&) = delete;
    evtEntry& operator=(const evtEntry&) = delete;
    evtEntry(evtEntry&&) = delete;
    evtEntry& operator=(evtEntry&&) = delete;

    /**
     * @brief Returns the file descriptor to the Entry file.
     * @return unix_fd - File descriptor to the Entry file.
     * @throws None
     * @note This is a placeholder implementation that returns 0.
     *       Override this method in derived classes to provide actual
     *       file descriptor handling. The base implementation returns 0
     *       to indicate an invalid/uninitialized file descriptor.
     */
    auto getEntry() -> sdbusplus::message::unix_fd override
    {
        return 0;
    }
};

} // namespace logging
} // namespace phosphor
// NOLINTEND
