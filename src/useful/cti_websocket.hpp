/******************************************************************************\
 * cti_websocket.hpp - HTTP and WebSocket helper objects
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
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
#include <type_traits>

// Boost WebSocket
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <boost/beast/http/string_body.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace cti
{

namespace
{

// Load the proper gateway / API certificates into the SSL context for a stream
template <typename SslCtx>
static inline auto set_ssl_certs(SslCtx&& ssl_ctx)
{

    // Shasta job launch tools do not currently do any certificate verification for SSL,
    // once that is implemented, we can use the same certificates to perform verification here.
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

}

// Initialize SSL certificates and connect a stream to the given host
template <typename Ioc, typename SslCtx, typename Stream>
static inline auto connect_ssl_stream(Ioc&& ioc, SslCtx&& ssl_ctx, Stream&& stream, std::string const& hostname)
{
    // Load in gateway / API certificates
    set_ssl_certs(ssl_ctx);

    // Set up the SSL context to connect to the given hostname
    if(!SSL_set_tlsext_host_name(stream.native_handle(), hostname.c_str())) {
        boost::beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
        throw boost::beast::system_error{ec};
    }

    // Resolve the hostname and connect the underlying stream
    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto const resolver_results = resolver.resolve(hostname, "443");
    boost::beast::get_lowest_layer(stream).connect(resolver_results);

    // Perform SSL handshake
    stream.handshake(boost::asio::ssl::stream_base::client);
}

// Clean up the stream's connection
template <typename Stream,
    // Stream must be moved into function, as it will become invalidated.
    // Stream&& is a universal reference, so enforce this via type traits
    class = typename std::enable_if<!std::is_lvalue_reference<Stream>::value>::type>
static inline auto shutdown_ssl_stream(Stream&& stream)
{
    // Shut down the underlying socket
    auto ec = boost::beast::error_code{};
    boost::beast::get_lowest_layer(stream).socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

    // Check for shutdown error
    if (ec && (ec != boost::beast::errc::not_connected)) {
        throw boost::beast::system_error{ec};
    }
}

using TcpStream = boost::beast::ssl_stream<boost::beast::tcp_stream>;
using WebSocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::asio::ip::tcp::socket>>;

}

// Perform HTTPS GET request to https://<hostname>/<endpoint>, authenticated with <token>
static inline std::string httpGetReq(std::string const& hostname, std::string const& endpoint, std::string const& token)
{
    // Set up SSL stream and connect to host
    auto ioc = boost::asio::io_context{};
    auto ssl_ctx = boost::asio::ssl::context{boost::asio::ssl::context::tlsv12_client};
    auto stream = TcpStream{ioc, ssl_ctx};
    connect_ssl_stream(ioc, ssl_ctx, stream, hostname);

    // Create GET request and fill in required headers
    auto req = boost::beast::http::request<boost::beast::http::string_body>{boost::beast::http::verb::get, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);
    req.set(boost::beast::http::field::accept, "application/json");

    // Prepare and write request
    req.prepare_payload();
    boost::beast::http::write(stream, req);

    // Receive string response
    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};
    boost::beast::http::read(stream, buffer, resp);

    // Check HTTP response code
    if (resp.base().result_int() != 200) {
        throw std::runtime_error("GET " + endpoint + " failed: " + std::to_string(resp.base().result_int()));
    }

    // Extract result and shut down stream
    auto const result = std::string{resp.body().data()};
    shutdown_ssl_stream(std::move(stream));

    return result;
}

// Perform HTTPS DELETE request to https://<hostname>/<endpoint>, authenticated with <token>
static inline std::string httpDeleteReq(std::string const& hostname, std::string const& endpoint, std::string const& token)
{
    // Set up SSL stream and connect to host
    auto ioc = boost::asio::io_context{};
    auto ssl_ctx = boost::asio::ssl::context{boost::asio::ssl::context::tlsv12_client};
    auto stream = TcpStream{ioc, ssl_ctx};
    connect_ssl_stream(ioc, ssl_ctx, stream, hostname);

    // Cretae DELETE request and fill in required headers
    auto req = boost::beast::http::request<boost::beast::http::string_body>{boost::beast::http::verb::delete_, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);
    req.set(boost::beast::http::field::accept, "application/json");

    // Prepare and write request
    req.prepare_payload();
    boost::beast::http::write(stream, req);

    // Receive string response
    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};
    boost::beast::http::read(stream, buffer, resp);

    // Check HTTP response code
    if (resp.base().result_int() != 200) {
        throw std::runtime_error("DELETE " + endpoint + " failed: " + std::to_string(resp.base().result_int()));
    }

    // Extract result and shut down stream
    auto const result = std::string{resp.body().data()};
    shutdown_ssl_stream(std::move(stream));

    return result;
}

// Perform HTTPS POST request to https://<hostname>/<endpoint>, sending JSON <body>
static inline std::string httpPostJsonReq(std::string const& hostname, std::string const& endpoint, std::string const& token, std::string const& body)
{
    // Set up SSL stream and connect to host
    auto ioc = boost::asio::io_context{};
    auto ssl_ctx = boost::asio::ssl::context{boost::asio::ssl::context::tlsv12_client};
    auto stream = TcpStream{ioc, ssl_ctx};
    connect_ssl_stream(ioc, ssl_ctx, stream, hostname);

    // Create string POST request and fill in required headers
    auto req = boost::beast::http::request<boost::beast::http::string_body>{boost::beast::http::verb::post, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);
    req.set(boost::beast::http::field::accept, "application/json");
    req.set(boost::beast::http::field::content_type, "application/json");

    // Prepare body and write request
    req.body() = body;
    req.prepare_payload();
    boost::beast::http::write(stream, req);

    // Receive string response
    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};
    boost::beast::http::read(stream, buffer, resp);

    // Check HTTP response code
    if (resp.base().result_int() != 200) {
        throw std::runtime_error("POST " + endpoint + " failed: " + std::to_string(resp.base().result_int()));
    }

    // Extract result and shut down stream
    auto const result = std::string{resp.body().data()};
    shutdown_ssl_stream(std::move(stream));

    return result;
}

// Perform HTTPS POST request to https://<hostname>/<endpoint>, sending file at <filePath>
static inline std::string httpPostFileReq(std::string const& hostname, std::string const& endpoint, std::string const& token, std::string const& filePath)
{
    // Set up SSL stream and connect to host
    auto ioc = boost::asio::io_context{};
    auto ssl_ctx = boost::asio::ssl::context{boost::asio::ssl::context::tlsv12_client};
    auto stream = TcpStream{ioc, ssl_ctx};
    connect_ssl_stream(ioc, ssl_ctx, stream, hostname);

    // Create file POST request and fill in required headers
    auto req = boost::beast::http::request<boost::beast::http::file_body>{boost::beast::http::verb::post, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);
    req.set(boost::beast::http::field::accept, "application/json");
    req.set(boost::beast::http::field::content_type, "application/octet-stream");

    // Read file into body and write request
    auto ec = boost::beast::error_code{};
    req.body().open(filePath.c_str(), boost::beast::file_mode::read, ec);
    if (ec) {
        throw boost::beast::system_error{ec};
    }
    req.prepare_payload();
    boost::beast::http::write(stream, req);

    // Receive string response
    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};
    boost::beast::http::read(stream, buffer, resp);

    // Check HTTP response code
    if (resp.base().result_int() != 200) {
        throw std::runtime_error("GET " + endpoint + " failed: " + std::to_string(resp.base().result_int()));
    }

    // Extract result and shut down stream
    auto const result = std::string{resp.body().data()};
    shutdown_ssl_stream(std::move(stream));

    return result;
}

// Open persistent Websocket stream at wss://<hostname>:<port>, caller must connect to endpoint
template <typename Ioc, typename SslCtx>
static inline auto make_WebSocketStream(Ioc&& ioc, SslCtx&& ssl_ctx,
    std::string const& hostname, std::string const& port,
    std::string const& token)
{
    // Load in gateway / API certificates
    set_ssl_certs(ssl_ctx);

    // Initialize stream
    auto result = WebSocketStream{ioc, ssl_ctx};

    // Resolve hostname and connect asynchronously
    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto const resolver_results = resolver.resolve(hostname, port);
    boost::asio::connect(boost::beast::get_lowest_layer(result), resolver_results.begin(), resolver_results.end());

    // Peform SSL handshake
    result.next_layer().handshake(boost::asio::ssl::stream_base::client);

    // Set required headers
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

// Helper to write strings produced by <dataSource> to <webSocketStream>
// Can be paired with std::future / std::async for asynchronous input to a Websocket
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
        auto buffer = boost::asio::buffer(message);
        auto written = size_t{0};
        while (written < buffer.size()) {
            written += webSocketStream.write(buffer + written, ec);

            // Check for websocket error
            if (ec && (ec != boost::asio::error::interrupted)) {
                throw std::runtime_error(ec.message());
            }
        }

        // If callback requested exit, end loop
        if (completed) {
            return;
        }
    }
};

// Helper to write strings produced by <webSocketStream> to <dataSink>
// Can be paired with std::future / std::async for asynchronous output from a Websocket
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
        } else if (ec && (ec != boost::asio::error::interrupted)) {
            throw std::runtime_error(ec.message());
        }
    }
}

// Synchronously read a single string from <webSocketStream>
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
