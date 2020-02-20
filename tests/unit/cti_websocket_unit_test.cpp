/******************************************************************************\
 * cti_websocket_unit_test.cpp - CTI HTTP and WebSocket tests
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

#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include "cti_useful_unit_test.hpp"

#include "useful/cti_websocket.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;
using ::testing::EndsWith;

CTIUsefulUnitTest::CTIUsefulUnitTest()
{
}

CTIUsefulUnitTest::~CTIUsefulUnitTest()
{
}

TEST_F(CTIUsefulUnitTest, WebSocketTask)
{
    auto const echoHost = "echo.websocket.org";
    auto const echoEndpoint = "/";
    auto const echoMessage = std::to_string(::getpid());

    auto webSocketTask = cti::WebSocketTask{echoHost, echoEndpoint, echoMessage,
        [echoMessage](auto const& responseBody) {
            auto const responseBodyString = std::string{(char const*)responseBody.data()};

            if (echoMessage != responseBodyString) {
                return -1;
            }

            return 1;
        }};

    EXPECT_GT(webSocketTask.get(), 0);
}

TEST_F(CTIUsefulUnitTest, PostJsonReq)
{
    auto const postHost = "httpbin.org";
    auto const postEndpoint = "/post";
    auto const postMessage = std::to_string(::getpid());

    auto const resp = cti::postJsonReq(postHost, postEndpoint, postMessage);

    EXPECT_TRUE(!resp.empty());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}