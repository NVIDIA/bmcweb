/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sse_connection.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace crow
{

// Mock ConnectionPolicy for testing
class MockConnectionPolicy : public ConnectionPolicy
{
  public:
    MockConnectionPolicy()
    {
        maxRetryAttempts = 1;
        maxConnections = 1;
    }
};

// Mock ConnectionInfo for testing
class SSEConnectionTestHelper : public crow::SSEConnection
{
  public:
    using SSEConnection::doRead;
    using SSEConnection::getConnectionState;

    SSEConnectionTestHelper(
        boost::asio::io_context& iocIn, const std::string& idIn,
        const std::shared_ptr<ConnectionPolicy>& connPolicyIn,
        const boost::urls::url_view_base& urlIn, uint32_t retryCountIn,
        std::function<void(boost::system::error_code, std::string_view)>
            sseDataCallbackIn,
        std::function<void(crow::Response&)> sseInitialCallbackIn) :
        SSEConnection(iocIn, idIn, connPolicyIn, urlIn, retryCountIn,
                      std::move(sseDataCallbackIn),
                      std::move(sseInitialCallbackIn))
    {}

    virtual ~SSEConnectionTestHelper() override = default;

    ConnState getConnState() const
    {
        return getConnectionState();
    }

    void setConnState(ConnState newState)
    {
        state = newState;
    }

    void prepareAndCommitBuffer(const std::string& data)
    {
        auto preparedBuffer = buffer.prepare(data.size());
        std::memcpy(preparedBuffer.data(), data.data(), data.size());
        buffer.commit(data.size());
    }

    void testDoReadCallback(const boost::beast::error_code& ec,
                            std::size_t bytesTransferred)
    {
        std::shared_ptr<ConnectionInfo> self =
            std::static_pointer_cast<ConnectionInfo>(shared_from_this());
        doReadCallback(self, ec, bytesTransferred);
    }
};

class MockHttpClientSse : public crow::HttpClientSse
{
  public:
    using HttpClientSse::HttpClientSse;

    bool startSseCalled = false;
    boost::urls::url lastUrl;
    boost::beast::http::fields lastHeaders;

    virtual ~MockHttpClientSse() override = default;

    void startSSEConnection(
        const boost::urls::url& url, const boost::beast::http::fields& headers,
        std::function<void(boost::system::error_code, std::string_view)>
        /*dataCallback*/,
        std::function<void(crow::Response&)> /*initialCallback*/) override
    {
        startSseCalled = true;
        lastUrl = url;
        lastHeaders = headers;
    }

    void cleanupSSEConnections() override
    {
        // don't attempt to clean up real connections
    }
};

// Test HttpClient SSE functionality
class HttpClientSseTest : public ::testing::Test
{
  protected:
    boost::asio::io_context ioc;
    std::shared_ptr<ConnectionPolicy> policy;
    std::shared_ptr<SSEConnectionTestHelper> sseConn;
    bool dataCallbackCalled{false};
    bool initialCallbackCalled{false};
    boost::system::error_code callbackEc;
    std::string callbackData;

    void SetUp() override
    {
        policy = std::make_shared<MockConnectionPolicy>();
    }

    void TearDown() override
    {
        // Clear any remaining callbacks
        sseConn.reset();
        policy.reset();
    }

    boost::urls::url createTestUrl()
    {
        return boost::urls::url(
            "http://test-host:8080/redfish/v1/EventService/SSE");
    }

    void setupTestConnection()
    {
        const std::string id = "test-id";
        unsigned int retryCount = 0;
        boost::urls::url url = createTestUrl();

        auto dataCallback =
            [this](boost::system::error_code ec, std::string_view data) {
                this->dataCallbackCalled = true;
                this->callbackEc = ec;
                this->callbackData = std::string(data);
            };

        auto initialCallback = [this](crow::Response&) {
            this->initialCallbackCalled = true;
        };

        sseConn = std::make_shared<SSEConnectionTestHelper>(
            ioc, id, policy, url, retryCount, dataCallback, initialCallback);
    }

    void resetCallbackState()
    {
        dataCallbackCalled = false;
        initialCallbackCalled = false;
        callbackEc = boost::system::error_code();
        callbackData.clear();
    }
};

// Test ConnectionInfo::setStreamingCallback
TEST_F(HttpClientSseTest, SSEConnectionSetCallback)
{
    setupTestConnection();
    EXPECT_NE(sseConn, nullptr);
    EXPECT_EQ(sseConn->getConnState(), ConnState::initialized);
}

TEST_F(HttpClientSseTest, SSEConnectionDoRead)
{
    setupTestConnection();

    EXPECT_NO_THROW(sseConn->doRead());
    EXPECT_EQ(sseConn->getConnState(), ConnState::recvInProgress);
}

TEST_F(HttpClientSseTest, SSEConnectionAfterRead)
{
    setupTestConnection();

    // Test read with success
    {
        sseConn->setConnState(ConnState::recvInProgress);

        boost::beast::error_code ec;
        std::string testData = "data: test event\n\n";

        // prepare the buffer
        sseConn->prepareAndCommitBuffer(testData);
        // Use the test wrapper with the correct signature
        sseConn->testDoReadCallback(ec, testData.size());

        EXPECT_EQ(sseConn->getConnState(), ConnState::recvInProgress);
        EXPECT_FALSE(callbackEc);
        EXPECT_EQ(callbackData, testData);
    }

    // Test case 2: Multiple events in single read
    {
        resetCallbackState();
        sseConn->setConnState(ConnState::recvInProgress);

        boost::beast::error_code ec;
        std::string testData = "data: event1\n\ndata: event2\n\n";

        sseConn->prepareAndCommitBuffer(testData);
        sseConn->testDoReadCallback(ec, testData.size());

        EXPECT_EQ(sseConn->getConnState(), ConnState::recvInProgress);
        EXPECT_FALSE(callbackEc);
        EXPECT_EQ(callbackData, testData);
    }

    // Test read with error
    {
        resetCallbackState();
        sseConn->setConnState(ConnState::recvInProgress);

        boost::beast::error_code ec = boost::asio::error::connection_reset;
        std::size_t bytesTransferred = 0;
        sseConn->testDoReadCallback(ec, bytesTransferred);

        EXPECT_EQ(sseConn->getConnState(), ConnState::recvFailed);
    }

    // Test EOF
    {
        resetCallbackState();
        sseConn->setConnState(ConnState::recvInProgress);

        boost::beast::error_code ec = boost::asio::error::eof;
        std::size_t bytesTransferred = 0;
        sseConn->testDoReadCallback(ec, bytesTransferred);

        EXPECT_EQ(sseConn->getConnState(), ConnState::recvFailed);
    }

    // Test operation aborted
    {
        resetCallbackState();
        sseConn->setConnState(ConnState::recvInProgress);

        boost::beast::error_code ec = boost::asio::error::operation_aborted;
        std::size_t bytesTransferred = 0;
        sseConn->testDoReadCallback(ec, bytesTransferred);

        EXPECT_EQ(sseConn->getConnState(), ConnState::closed);
    }
}

// Test HttpClientSse class
TEST_F(HttpClientSseTest, HttpClientSseCreation)
{
    MockHttpClientSse client(ioc, policy);

    // Create fields for the connection
    boost::beast::http::fields headers;
    headers.set(boost::beast::http::field::accept, "text/event-stream");

    boost::urls::url url = createTestUrl();

    bool dataCallbackCalled = false;
    bool initialCallbackCalled = false;

    auto dataCallback =
        [&dataCallbackCalled](boost::system::error_code, std::string_view) {
            dataCallbackCalled = true;
        };

    auto initialCallback = [&initialCallbackCalled](crow::Response&) {
        initialCallbackCalled = true;
    };

    // Start an SSE connection
    EXPECT_NO_THROW(
        client.startSSEConnection(url, headers, dataCallback, initialCallback));

    EXPECT_TRUE(client.startSseCalled);
    EXPECT_EQ(client.lastUrl, url);

    auto acceptIter =
        client.lastHeaders.find(boost::beast::http::field::accept);
    EXPECT_NE(acceptIter, client.lastHeaders.end());
    EXPECT_EQ(acceptIter->value(), "text/event-stream");
}
} // namespace crow
