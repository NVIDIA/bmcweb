// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace serial_interface
{
// clang-format off

enum class SignalType{
    Invalid,
    Rs232,
    Rs485,
};

enum class BitRate{
    Invalid,
    1200,
    2400,
    4800,
    9600,
    19200,
    38400,
    57600,
    115200,
    230400,
};

enum class Parity{
    Invalid,
    None,
    Even,
    Odd,
    Mark,
    Space,
};

enum class DataBits{
    Invalid,
    5,
    6,
    7,
    8,
};

enum class StopBits{
    Invalid,
    1,
    2,
};

enum class FlowControl{
    Invalid,
    None,
    Software,
    Hardware,
};

enum class PinOut{
    Invalid,
    Cisco,
    Cyclades,
    Digi,
};

enum class ConnectorType{
    Invalid,
    RJ45,
    RJ11,
    DB9Female,
    DB9Male,
    DB25Female,
    DB25Male,
    USB,
    mUSB,
    uUSB,
};

NLOHMANN_JSON_SERIALIZE_ENUM(SignalType, {
    {SignalType::Invalid, "Invalid"},
    {SignalType::Rs232, "Rs232"},
    {SignalType::Rs485, "Rs485"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(BitRate, {
    {BitRate::Invalid, "Invalid"},
    {BitRate::1200, "1200"},
    {BitRate::2400, "2400"},
    {BitRate::4800, "4800"},
    {BitRate::9600, "9600"},
    {BitRate::19200, "19200"},
    {BitRate::38400, "38400"},
    {BitRate::57600, "57600"},
    {BitRate::115200, "115200"},
    {BitRate::230400, "230400"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(Parity, {
    {Parity::Invalid, "Invalid"},
    {Parity::None, "None"},
    {Parity::Even, "Even"},
    {Parity::Odd, "Odd"},
    {Parity::Mark, "Mark"},
    {Parity::Space, "Space"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(DataBits, {
    {DataBits::Invalid, "Invalid"},
    {DataBits::5, "5"},
    {DataBits::6, "6"},
    {DataBits::7, "7"},
    {DataBits::8, "8"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(StopBits, {
    {StopBits::Invalid, "Invalid"},
    {StopBits::1, "1"},
    {StopBits::2, "2"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(FlowControl, {
    {FlowControl::Invalid, "Invalid"},
    {FlowControl::None, "None"},
    {FlowControl::Software, "Software"},
    {FlowControl::Hardware, "Hardware"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(PinOut, {
    {PinOut::Invalid, "Invalid"},
    {PinOut::Cisco, "Cisco"},
    {PinOut::Cyclades, "Cyclades"},
    {PinOut::Digi, "Digi"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ConnectorType, {
    {ConnectorType::Invalid, "Invalid"},
    {ConnectorType::RJ45, "RJ45"},
    {ConnectorType::RJ11, "RJ11"},
    {ConnectorType::DB9Female, "DB9 Female"},
    {ConnectorType::DB9Male, "DB9 Male"},
    {ConnectorType::DB25Female, "DB25 Female"},
    {ConnectorType::DB25Male, "DB25 Male"},
    {ConnectorType::USB, "USB"},
    {ConnectorType::mUSB, "mUSB"},
    {ConnectorType::uUSB, "uUSB"},
});

}
// clang-format on
