// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/url/url.hpp>

namespace redfish::nvidia_http_utils
{

// Build a filtered header set for outbound forwarding:
// - Drop HTTP/2 pseudo-headers (those whose names start with ':', e.g.
// :authority)
// - Drop any incoming Host header
// - Set Host from the provided outbound URL
// Optionally, the caller can add/override other headers after this call
inline boost::beast::http::fields filterHeadersDropAuthority(
    const boost::beast::http::fields& inHeaders, const boost::urls::url& outUrl)
{
    boost::beast::http::fields out;

    for (const auto& h : inHeaders)
    {
        // Skip HTTP/2 pseudo-headers like :authority
        auto nameView = h.name_string();
        if (!nameView.empty() && nameView.front() == ':')
        {
            continue;
        }

        // Skip existing Host; we'll set it based on outUrl
        if (h.name() == boost::beast::http::field::host)
        {
            continue;
        }

        out.set(h.name(), h.value());
    }

    // Ensure Host is set appropriately for the outbound request
    out.set(boost::beast::http::field::host, outUrl.encoded_host_address());
    return out;
}

} // namespace redfish::nvidia_http_utils
