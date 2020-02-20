/******************************************************************************\
 * cti_websocket.hpp - HTTP and WebSocket helper objects
 *
 * Copyright 2020 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/
#pragma once

#include <thread>
#include <future>

// // Boost JSON
// #include <boost/property_tree/ptree.hpp>
// #include <boost/property_tree/json_parser.hpp>

// Boost WebSocket
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace cti
{

template <typename Func>
class WebSocketTask
{
public: // types
    using WebSocketStream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;
    using Buffer = boost::beast::flat_buffer::const_buffers_type;

private: // helpers
    static auto make_WebSocketStream(boost::asio::io_context& ioc,
        std::string const& hostname, std::string const& port)
    {
        auto resolver = boost::asio::ip::tcp::resolver{ioc};
        auto result = WebSocketStream{ioc};

        auto const resolver_results = resolver.resolve(hostname, port);
        boost::asio::connect(result.next_layer(), resolver_results.begin(), resolver_results.end());

        return result;
    }

    static int relayWorker(WebSocketStream&& webSocketStream,
        std::string const& hostname, std::string const& endpoint, std::string const& body,
        Func&& dataCallback)
    {
        webSocketStream.handshake(hostname, endpoint);
        webSocketStream.write(boost::asio::buffer(body));

        auto buffer = boost::beast::flat_buffer{};
        auto ec = boost::beast::error_code{};

        while (auto const bytes_read = webSocketStream.read(buffer, ec)) {
            if (ec == boost::beast::websocket::error::closed) {
                break;
            }

            if (auto const rc = dataCallback(buffer.cdata())) {
                webSocketStream.close(boost::beast::websocket::close_code::normal);

                return rc;
            }

            buffer.clear();
        }

        webSocketStream.close(boost::beast::websocket::close_code::normal);

        return 0;
    }

private: // members
    boost::asio::io_context m_ioc;

    std::future<int> m_relayFuture;

public: // interface
    WebSocketTask(std::string const& hostname, std::string const& endpoint, std::string const& body,
        Func&& dataCallback)
        : m_ioc{}
        , m_relayFuture{std::async(std::launch::async
            , relayWorker
            , make_WebSocketStream(m_ioc, hostname, "80")
            , hostname
            , endpoint
            , body
            , std::forward<Func>(dataCallback))}
    {}

    int get()
    {
        return m_relayFuture.get();
    }
};

}