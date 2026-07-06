// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include "http_request.hpp"
#include "multipart_parser.hpp"
#include "multipart_serializer.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/url/url.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
namespace redfish
{
// Drop any incoming x-auth-token headers and keep Host and Content-Type. Set
// Accept.
inline crow::Request createNewRequest(const crow::Request& localReq)
{
    boost::system::error_code ec;

    std::string bodyData{localReq.body()};
    MultipartSerializer serializer([&bodyData](std::string_view chunk) {
        bodyData.append(chunk.data(), chunk.size());
    });
    std::span<const FormPart> parts = localReq.multipart();
    if (!parts.empty())
    {
        for (const FormPart& part : parts)
        {
            // Only re-emit the part headers we expect; don't relay arbitrary
            // client-supplied part headers on to the satellite BMC.
            boost::beast::http::fields partHeaders;
            for (const auto& field : part.fields)
            {
                boost::beast::http::field name = field.name();
                if (name == boost::beast::http::field::content_disposition ||
                    name == boost::beast::http::field::content_type)
                {
                    partHeaders.insert(field.name_string(), field.value());
                }
            }
            serializer.beginPart(partHeaders);
            serializer.put(part.content);
        }
        serializer.finish();
    }

    // Note, this is an expensive copy.  It ideally shouldn't be done, but no
    // option at this point.
    crow::Request req(bodyData, ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to set body.  Continuing");
    }

    // Preserve method and target (URI) from the original request
    req.method(localReq.method());
    if (!req.target(localReq.target()))
    {
        BMCWEB_LOG_ERROR("Failed to set target on aggregated request");
    }

    if (!parts.empty())
    {
        req.addHeader(boost::beast::http::field::content_type,
                      serializer.getContentType());
    }

    for (const auto& field : localReq.fields())
    {
        // Drop any incoming x-auth-token headers and keep Host and
        // Content-Type. Set Accept.
        auto headerName = field.name();
        if (headerName == boost::beast::http::field::host ||
            (headerName == boost::beast::http::field::content_type &&
             req.getHeaderValue(boost::beast::http::field::content_type)
                 .empty()))
        {
            req.addHeader(headerName, field.value());
        }
    }
    // Set Accept header to application/json, application/octet-stream
    req.addHeader(boost::beast::http::field::accept,
                  "application/json, application/octet-stream");
    return req;
}
} // namespace redfish
