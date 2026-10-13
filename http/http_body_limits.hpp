// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include "bmcweb_config.h"

#include <cstdint>

// Shared by both HTTP/1.1 (http_connection.hpp) and HTTP/2
// (http2_connection.hpp) request body handling; neither is specific to
// one protocol version.
namespace crow
{

// request body limit size set by the BMCWEB_HTTP_BODY_LIMIT option
constexpr uint64_t httpReqBodyLimit = 1024UL * 1024UL * BMCWEB_HTTP_BODY_LIMIT;

constexpr uint64_t loggedOutPostBodyLimit = 4096U;

// Multipart uploads stream, so they get an extremely large limit.
constexpr uint64_t multipartBodyLimit = 4ULL * 1024ULL * 1024ULL * 1024ULL;

} // namespace crow
