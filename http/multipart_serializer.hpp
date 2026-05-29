// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "logging.hpp"
#include "ossl_random.hpp"

#include <boost/beast/http/fields.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

class MultipartSerializer
{
  public:
    explicit MultipartSerializer(
        std::move_only_function<void(std::string_view)>&& putBytesIn) :
        boundary(bmcweb::getRandomIdOfLength(32)),
        putBytes(std::move(putBytesIn))
    {}

    std::string_view getBoundary() const
    {
        return boundary;
    }

    std::string getContentType() const
    {
        return "multipart/form-data; boundary=" + boundary;
    }

    void start()
    {
        putBytes("\r\n");
    }

    void beginPart(const boost::beast::http::fields& fields)
    {
        putBytes("\r\n--");
        putBytes(boundary);
        putBytes("\r\n");
        for (const auto& field : fields)
        {
            putBytes(field.name_string());
            putBytes(": ");
            putBytes(field.value());
            putBytes("\r\n");
        }
        putBytes("\r\n");
    }

    void put(std::string_view buffer)
    {
        putBytes(buffer);
    }

    void putJsonObject(nlohmann::json::object_t&& json)
    {
        nlohmann::json wrapped = std::move(json);
        putBytes(wrapped.dump(-1, ' ', false,
                              nlohmann::json::error_handler_t::replace));
    }

    void finish()
    {
        putBytes("\r\n--");
        putBytes(boundary);
        putBytes("--\r\n");
        BMCWEB_LOG_DEBUG("Finishing multipart serializer");
    }

  private:
    std::string boundary;
    std::move_only_function<void(std::string_view)> putBytes;
};
