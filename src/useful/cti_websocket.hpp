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

#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <boost/beast/http/string_body.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace cti
{

static inline std::string httpGetReq(std::string const& hostname, std::string const& endpoint, std::string const& token)
{
    auto ioc = boost::asio::io_context{};

    auto ssl_ctx = boost::asio::ssl::context{boost::asio::ssl::context::tlsv12_client};
#if 0
    ssl_ctx.add_certificate_authority(
        boost::asio::buffer(cert.data(), cert.size()), ec);
    if (ec) {
        throw boost::beast::system_error{ec};
    }
    ssl_ctx.set_verify_mode(boost::asio::ssl::verify_peer);
#else
    ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);
#endif

    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto stream = boost::beast::ssl_stream<boost::beast::tcp_stream>{ioc, ssl_ctx};

    if(!SSL_set_tlsext_host_name(stream.native_handle(), hostname.c_str())) {
        boost::beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
        throw boost::beast::system_error{ec};
    }

    auto const resolver_results = resolver.resolve(hostname, "443");

    boost::beast::get_lowest_layer(stream).connect(resolver_results);

    stream.handshake(boost::asio::ssl::stream_base::client);

    auto req = boost::beast::http::request<boost::beast::http::string_body>{boost::beast::http::verb::get, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);

    req.set(boost::beast::http::field::accept, "application/json");
    req.prepare_payload();

    boost::beast::http::write(stream, req);

    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};

    boost::beast::http::read(stream, buffer, resp);

    if (resp.base().result_int() == 301) {
        auto const location = std::string{resp.base()["Location"]};
        throw std::runtime_error("301 redirect: " + location);
    }

    auto const result = std::string{resp.body().data()};

    auto ec = boost::beast::error_code{};
    boost::beast::get_lowest_layer(stream).socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

    if (ec && (ec != boost::beast::errc::not_connected)) {
        throw boost::beast::system_error{ec};
    }

    return result;
}

static inline std::string httpDeleteReq(std::string const& hostname, std::string const& endpoint, std::string const& token)
{
    auto ioc = boost::asio::io_context{};

    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto stream = boost::beast::tcp_stream{ioc};

    auto const resolver_results = resolver.resolve(hostname, "80");

    stream.connect(resolver_results);

    auto req = boost::beast::http::request<boost::beast::http::string_body>{boost::beast::http::verb::delete_, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);

    req.set(boost::beast::http::field::accept, "application/json");
    req.prepare_payload();

    boost::beast::http::write(stream, req);

    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};

    boost::beast::http::read(stream, buffer, resp);

    auto const result = std::string{resp.body().data()};

    auto ec = boost::beast::error_code{};
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

    if (ec && (ec != boost::beast::errc::not_connected)) {
        throw boost::beast::system_error{ec};
    }

    return result;
}

static inline std::string httpPostJsonReq(std::string const& hostname, std::string const& endpoint, std::string const& token, std::string const& body)
{
    auto ioc = boost::asio::io_context{};

    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto stream = boost::beast::tcp_stream{ioc};

    auto const resolver_results = resolver.resolve(hostname, "80");

    stream.connect(resolver_results);

    auto req = boost::beast::http::request<boost::beast::http::string_body>{boost::beast::http::verb::post, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);

    req.set(boost::beast::http::field::accept, "application/json");
    req.set(boost::beast::http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    boost::beast::http::write(stream, req);

    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};

    boost::beast::http::read(stream, buffer, resp);

    auto const result = std::string{resp.body().data()};

    auto ec = boost::beast::error_code{};
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

    if (ec && (ec != boost::beast::errc::not_connected)) {
        throw boost::beast::system_error{ec};
    }

    return result;
}

using WebSocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::asio::ip::tcp::socket>>;

template <typename Ioc, typename SslCtx>
static inline auto make_WebSocketStream(Ioc&& ioc, SslCtx&& ssl_ctx,
    std::string const& hostname, std::string const& port,
    std::string const& token)
{

#if 0
    ssl_ctx.add_certificate_authority(
        boost::asio::buffer(cert.data(), cert.size()), ec);
    if (ec) {
        throw boost::beast::system_error{ec};
    }
    ssl_ctx.set_verify_mode(boost::asio::ssl::verify_peer);
#else
    ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);
#endif

    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto result = WebSocketStream{ioc, ssl_ctx};

    auto const resolver_results = resolver.resolve(hostname, port);
    boost::asio::connect(boost::beast::get_lowest_layer(result), resolver_results.begin(), resolver_results.end());

    result.next_layer().handshake(boost::asio::ssl::stream_base::client);

    result.set_option(boost::beast::websocket::stream_base::decorator(
        [hostname, token](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::host, hostname);
            req.set("Authorization", "Bearer " + token);
            req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);

            req.set(boost::beast::http::field::accept, "application/json");
            req.set(boost::beast::http::field::content_type, "application/json");
        }));

    return result;
}

template <typename Func>
static inline void webSocketInputTask(WebSocketStream& webSocketStream, Func&& dataSource)
{
    // Set up buffers
    auto buffer = boost::beast::flat_buffer{};
    auto ec = boost::beast::error_code{};

    // Write loop
    while (true) {
        // Fill string from data source
        auto message = std::string{};

        // Invoke user-provided callback
        auto const completed = dataSource(message);

        // Perform websocket write
        webSocketStream.write(boost::asio::buffer(message), ec);

        // Check for websocket error
        if (ec) {
            throw std::runtime_error(ec.message());

        // If callback requested exit, end loop
        } else if (completed) {
            return;
        }
    }
};

template <typename Func>
static inline void webSocketOutputTask(WebSocketStream& webSocketStream, Func&& dataSink)
{
    // Set up buffers
    auto buffer = boost::beast::flat_buffer{};
    auto ec = boost::beast::error_code{};

    // Read loop
    while (true) {
        // Perform websocket read
        auto const bytes_read = webSocketStream.read(buffer, ec);

        if (bytes_read > 0) {
            // Null-terminate message
            auto charsPtr = static_cast<char*>(buffer.data().data());
            charsPtr[bytes_read] = '\0';

            // Invoke user-provided callback
            if (auto const completed = dataSink(charsPtr)) {

                // Callback requested exit, end loop
                return;
            }

            // Clear for next read
            buffer.consume(bytes_read);
        }

        // Check for completed read
        if ((ec == boost::asio::error::eof)
         || (ec == boost::beast::websocket::error::closed)) {
            return;

        // Check for websocket error
        } else if (ec) {
            throw std::runtime_error(ec.message());
        }
    }
}

static inline auto webSocketReadString(WebSocketStream& webSocketStream)
{
    // Set up buffers
    auto buffer = boost::beast::flat_buffer{};
    auto ec = boost::beast::error_code{};

    // Perform websocket read
    auto const bytes_read = webSocketStream.read(buffer, ec);

    if (bytes_read > 0) {
        // Null-terminate message
        auto charsPtr = static_cast<char*>(buffer.data().data());
        charsPtr[bytes_read] = '\0';

        return std::string{charsPtr};
    } else {
        // Check for websocket error
        if (ec) {
            throw std::runtime_error("read failed: " + std::string{ec.message()});
        }

        return std::string{};
    }
}

}
