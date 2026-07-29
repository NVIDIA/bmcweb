// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "logging.hpp"

#include <boost/beast/http/fields.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class ParserError
{
    PARSER_SUCCESS,
    ERROR_BOUNDARY_FORMAT,
    ERROR_EMPTY_HEADER,
    ERROR_HEADER_NAME,
    ERROR_HEADER_NAME_TOO_LONG,
    ERROR_HEADER_VALUE,
    ERROR_HEADER_VALUE_TOO_LONG,
    ERROR_HEADER_ENDING,
    ERROR_UNEXPECTED_END_OF_HEADER,
    ERROR_UNEXPECTED_CHARACTER,
    ERROR_UNEXPECTED_END_OF_INPUT,
    ERROR_OUT_OF_RANGE,
    ERROR_DATA_AFTER_FINAL_BOUNDARY,
    ERROR_DATA_AFTER_ERROR
};

enum class State
{
    START,
    START_BOUNDARY,
    BOUNDARY,
    FIRST_BOUNDARY_CHAR,       // 3
    SECOND_BOUNDARY_CHAR_LF,
    SECOND_BOUNDARY_CHAR_DASH, // 5
    HEADER_FIELD_START,
    HEADER_FIELD,
    HEADER_VALUE_START,
    HEADER_VALUE,
    HEADER_VALUE_ALMOST_DONE,
    HEADERS_ALMOST_DONE,
    PART_DATA_START,
    PART_DATA,
    END, // 14
    ERROR
};

struct FormPart
{
    boost::beast::http::fields fields;
    std::string content;
};

struct MultipartParserStreamingCallbacks
{
    std::function<void(std::function<void()> pauseRead,
                       std::function<void()> resumeRead)>
        onStart;

    std::function<void(boost::beast::http::fields, size_t remainingBodyLength)>
        onHeadersComplete;
    std::function<void(std::string_view)> onDataAvailable;
    std::function<void()> onSectionComplete;
    std::function<void()> onParseComplete;
    std::function<void(ParserError)> onParseError;
};

class MultipartParser
{
  public:
    std::optional<MultipartParserStreamingCallbacks> callbacks;

    MultipartParser(size_t contentLengthIn) : contentLength(contentLengthIn) {}

    static constexpr std::string_view boundaryFormat =
        "multipart/form-data; boundary=";

    static bool hasMultipartBoundary(std::string_view contentType)
    {
        return contentType.starts_with(boundaryFormat);
    }

    [[nodiscard]] ParserError start(std::string_view contentType)
    {
        if (!hasMultipartBoundary(contentType))
        {
            state = State::ERROR;
            return ParserError::ERROR_BOUNDARY_FORMAT;
        }
        std::string_view boundaryStr =
            contentType.substr(boundaryFormat.size());
        boundary = std::format("\r\n--{}", boundaryStr);
        boundary_first = std::format("--{}\r\n", boundaryStr);
        state = State::START;
        return ParserError::PARSER_SUCCESS;
    }

    [[nodiscard]] ParserError parse(std::string_view contentType,
                                    std::string_view body)
    {
        contentLength = body.size();
        ParserError ret = start(contentType);
        if (ret != ParserError::PARSER_SUCCESS)
        {
            return ret;
        }

        ret = parsePart(body);
        if (ret != ParserError::PARSER_SUCCESS)
        {
            return ret;
        }
        return finish();
    }

    ParserError parsePart(std::string_view buffer)
    {
        // BMCWEB_LOG_DEBUG("Parsing {} bytes", buffer.size());
        for (const char c : buffer)
        {
            parsedCount++;
            // BMCWEB_LOG_DEBUG("State: {}", static_cast<int>(state));
            switch (state)
            {
                case State::START:
                    index = 0;
                    state = State::START_BOUNDARY;
                    [[fallthrough]];
                case State::START_BOUNDARY:
                {
                    if (index < boundary_first.size())
                    {
                        if (c != boundary_first[index])
                        {
                            state = State::ERROR;
                            return ParserError::ERROR_BOUNDARY_FORMAT;
                        }
                    }
                    index++;
                    if (index == boundary_first.size())
                    {
                        mime_fields.emplace_back();
                        state = State::HEADER_FIELD_START;
                        index = 0;
                    }

                    break;
                }
                case State::HEADER_FIELD_START:
                    state = State::HEADER_FIELD;
                    index = 0;
                    [[fallthrough]];
                case State::HEADER_FIELD:
                {
                    if (currentHeaderName.size() > 400)
                    {
                        state = State::ERROR;
                        return ParserError::ERROR_HEADER_NAME_TOO_LONG;
                    }
                    if (c == '\r')
                    {
                        state = State::HEADERS_ALMOST_DONE;
                        break;
                    }

                    index++;

                    if (c == ':')
                    {
                        if (currentHeaderName.empty())
                        {
                            state = State::ERROR;
                            return ParserError::ERROR_EMPTY_HEADER;
                        }

                        state = State::HEADER_VALUE_START;
                        break;
                    }
                    char cl = lower(c);
                    if ((cl < 'a' || cl > 'z') && cl != '-')
                    {
                        state = State::ERROR;
                        return ParserError::ERROR_HEADER_NAME;
                    }
                    currentHeaderName.push_back(cl);
                    break;
                }
                case State::HEADER_VALUE_START:
                    if (c == ' ')
                    {
                        break;
                    }
                    state = State::HEADER_VALUE;
                    [[fallthrough]];

                case State::HEADER_VALUE:
                {
                    if (currentHeaderValue.size() > 400)
                    {
                        state = State::ERROR;
                        return ParserError::ERROR_HEADER_VALUE_TOO_LONG;
                    }
                    if (c == '\r')
                    {
                        boost::beast::error_code ec;
                        using boost::beast::http::field;
                        mime_fields.back().fields.insert(
                            boost::beast::http::string_to_field(
                                currentHeaderName),
                            currentHeaderName, currentHeaderValue, ec);
                        if (ec)
                        {
                            return ParserError::ERROR_HEADER_VALUE;
                        }
                        currentHeaderName.clear();
                        currentHeaderValue.clear();
                        state = State::HEADER_VALUE_ALMOST_DONE;
                        break;
                    }
                    currentHeaderValue.push_back(c);
                    break;
                }
                case State::HEADER_VALUE_ALMOST_DONE:
                {
                    if (c != '\n')
                    {
                        state = State::ERROR;
                        return ParserError::ERROR_HEADER_VALUE;
                    }
                    state = State::HEADER_FIELD_START;
                    break;
                }
                case State::HEADERS_ALMOST_DONE:
                {
                    if (c != '\n')
                    {
                        state = State::ERROR;
                        return ParserError::ERROR_HEADER_ENDING;
                    }
                    if (index > 0)
                    {
                        state = State::ERROR;
                        return ParserError::ERROR_UNEXPECTED_END_OF_HEADER;
                    }

                    if (callbacks)
                    {
                        if (callbacks->onHeadersComplete)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Calling On Headers Complete callback");
                            std::optional<size_t> remaining =
                                remainingBodyLength();
                            if (!remaining)
                            {
                                return ParserError::ERROR_OUT_OF_RANGE;
                            }
                            callbacks->onHeadersComplete(
                                mime_fields.back().fields, *remaining);
                        }
                    }

                    state = State::PART_DATA_START;
                    break;
                }
                case State::PART_DATA_START:
                    state = State::PART_DATA;
                    index = 0;
                    [[fallthrough]];

                case State::PART_DATA:
                {
                    std::string& content = mime_fields.back().content;
                    content += c;
                    if (content.ends_with(boundary))
                    {
                        state = State::FIRST_BOUNDARY_CHAR;
                    }
                    if (content.size() > 4096 && callbacks)
                    {
                        if (callbacks->onDataAvailable)
                        {
                            size_t validChars =
                                content.size() - boundary.size();
                            std::string_view contentView(content);
                            contentView.remove_suffix(boundary.size());
                            if (callbacks)
                            {
                                if (callbacks->onDataAvailable)
                                {
                                    callbacks->onDataAvailable(contentView);
                                    content.erase(0, validChars);
                                }
                            }
                        }
                    }
                    break;
                }
                case State::FIRST_BOUNDARY_CHAR:
                {
                    std::string& content = mime_fields.back().content;
                    content += c;
                    if (c == '\r')
                    {
                        state = State::SECOND_BOUNDARY_CHAR_LF;
                        break;
                    }
                    if (c == '-')
                    {
                        state = State::SECOND_BOUNDARY_CHAR_DASH;
                        break;
                    }
                    state = State::PART_DATA;
                    break;
                }
                case State::SECOND_BOUNDARY_CHAR_LF:
                {
                    std::string& content = mime_fields.back().content;
                    if (c != '\n')
                    {
                        content += c;
                        state = State::PART_DATA;
                        break;
                    }
                    content.resize(content.size() - boundary.size() - 1);
                    if (callbacks)
                    {
                        if (callbacks->onDataAvailable)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Calling On Data Available callback");
                            if (callbacks->onDataAvailable)
                            {
                                callbacks->onDataAvailable(content);
                            }
                            content.clear();
                        }
                        if (callbacks->onSectionComplete)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Calling On Section Complete callback");
                            if (callbacks->onSectionComplete)
                            {
                                callbacks->onSectionComplete();
                            }
                        }
                    }
                    state = State::HEADER_FIELD_START;
                    index = 0;
                    mime_fields.emplace_back();
                    break;
                }
                case State::SECOND_BOUNDARY_CHAR_DASH:
                {
                    std::string& content = mime_fields.back().content;
                    if (c != '-')
                    {
                        content += c;
                        state = State::PART_DATA;
                        break;
                    }
                    content.resize(content.size() - boundary.size() - 1);

                    if (callbacks)
                    {
                        if (callbacks->onDataAvailable)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Calling On Data Available callback");
                            if (callbacks->onDataAvailable)
                            {
                                callbacks->onDataAvailable(content);
                            }
                            content.clear();
                        }
                        if (callbacks->onSectionComplete)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Calling On Section Complete callback");
                            if (callbacks->onSectionComplete)
                            {
                                callbacks->onSectionComplete();
                            }
                        }
                    }

                    state = State::END;
                    index = 0;
                    break;
                }
                case State::END:
                {
                    switch (index)
                    {
                        case 0:
                            if (c != '\r')
                            {
                                return ParserError::
                                    ERROR_DATA_AFTER_FINAL_BOUNDARY;
                            }
                            index++;
                            break;
                        case 1:
                            if (c != '\n')
                            {
                                return ParserError::
                                    ERROR_DATA_AFTER_FINAL_BOUNDARY;
                            }
                            index++;
                            break;
                        default:
                            return ParserError::ERROR_DATA_AFTER_FINAL_BOUNDARY;
                    }
                    break;
                }
                case State::ERROR:
                {
                    return ParserError::ERROR_DATA_AFTER_ERROR;
                }

                default:
                {
                    state = State::ERROR;
                    return ParserError::ERROR_UNEXPECTED_END_OF_INPUT;
                }
            }
        }

        return ParserError::PARSER_SUCCESS;
    }

    ParserError finish()
    {
        if (state != State::END)
        {
            state = State::ERROR;
            BMCWEB_LOG_WARNING("Bad multipart data");
            return ParserError::ERROR_UNEXPECTED_END_OF_INPUT;
        }

        BMCWEB_LOG_DEBUG("Calling On Parse Complete callback");
        if (callbacks)
        {
            BMCWEB_LOG_DEBUG("callbacks was valid");
            if (callbacks->onParseComplete)
            {
                BMCWEB_LOG_DEBUG("onParseComplete was valid");
                callbacks->onParseComplete();
            }
            else
            {
                BMCWEB_LOG_DEBUG("onParseComplete was not valid");
            }
            callbacks.reset();
        }
        else
        {
            BMCWEB_LOG_DEBUG("callbacks was not valid");
        }

        BMCWEB_LOG_DEBUG("Multipart parser finished");
        return ParserError::PARSER_SUCCESS;
    }

    // Assuming this multipart field is the last one, returns the expected
    // remaining length.  Returns nullopt if parser has received more bytes than
    // expected.
    std::optional<size_t> remainingBodyLength() const
    {
        size_t remaining = contentLength;
        if (remaining < parsedCount)
        {
            return std::nullopt;
        }
        // Subtract the parsed bytes
        remaining -= parsedCount;

        size_t closingDelimiter = boundary.size() + 4;
        if (remaining < closingDelimiter)
        {
            return std::nullopt;
        }
        remaining -= closingDelimiter;

        BMCWEB_LOG_DEBUG("Remaining body length: {}", remaining);
        BMCWEB_LOG_DEBUG("parsedCount was: {}", parsedCount);
        BMCWEB_LOG_DEBUG("Content length was: {}", contentLength);
        BMCWEB_LOG_DEBUG("Boundary was: {}", boundary);
        return remaining;
    }

    std::vector<FormPart> mime_fields;
    std::string boundary;
    std::string boundary_first;

  private:
    static char lower(char c)
    {
        return static_cast<char>(c | 0x20);
    }

    std::string currentHeaderName;
    std::string currentHeaderValue;

    State state = State::START;
    size_t index = 0;
    size_t parsedCount = 0;
    size_t contentLength;
};
