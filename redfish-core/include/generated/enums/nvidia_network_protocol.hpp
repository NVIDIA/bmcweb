#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_network_protocol
{
// clang-format off

enum class ClientStatus{
    Invalid,
    Enabled,
    Disabled,
};

enum class TLSStatus{
    Invalid,
    Enabled,
    Disabled,
};

enum class Protocol{
    Invalid,
    TCP,
    UDP,
};

enum class FilterFacility{
    Invalid,
    Daemon,
    Kern,
    All,
};

enum class FilterSeverity{
    Invalid,
    Error,
    Warning,
    Info,
    All,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ClientStatus, {
    {ClientStatus::Invalid, "Invalid"},
    {ClientStatus::Enabled, "Enabled"},
    {ClientStatus::Disabled, "Disabled"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(TLSStatus, {
    {TLSStatus::Invalid, "Invalid"},
    {TLSStatus::Enabled, "Enabled"},
    {TLSStatus::Disabled, "Disabled"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(Protocol, {
    {Protocol::Invalid, "Invalid"},
    {Protocol::TCP, "TCP"},
    {Protocol::UDP, "UDP"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(FilterFacility, {
    {FilterFacility::Invalid, "Invalid"},
    {FilterFacility::Daemon, "Daemon"},
    {FilterFacility::Kern, "Kern"},
    {FilterFacility::All, "All"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(FilterSeverity, {
    {FilterSeverity::Invalid, "Invalid"},
    {FilterSeverity::Error, "Error"},
    {FilterSeverity::Warning, "Warning"},
    {FilterSeverity::Info, "Info"},
    {FilterSeverity::All, "All"},
});

}
// clang-format on
