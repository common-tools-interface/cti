/******************************************************************************\
 * cti_fe_unit_test.cpp - Frontend unit tests for CTI
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#include <type_traits>
#include <cstddef>

#include "cti_header_unit_test.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

// Hack around including headers within namespace issue
#include <stdint.h>
#include <sys/types.h>

namespace external
{
#include "cray_tools_fe.h"
}

namespace internal
{
#include "cti_defs.h"
}

// test cti_attr_type
TEST_F(CTIHeaderUnitTest, test_cti_attr_type_t)
{
    ASSERT_TRUE((std::is_enum<external::cti_attr_type>::value));
    ASSERT_TRUE((std::is_enum<internal::cti_attr_type>::value));
    ASSERT_TRUE((std::is_same<typename std::underlying_type<external::cti_attr_type>::type
                             ,typename std::underlying_type<internal::cti_attr_type>::type
                             >::value));
    EXPECT_EQ(external::CTI_ATTR_STAGE_DEPENDENCIES, internal::CTI_ATTR_STAGE_DEPENDENCIES);
}

// test cti_host_t
TEST_F(CTIHeaderUnitTest, test_cti_host_t)
{
    ASSERT_TRUE((std::is_class<external::cti_host_t>::value));
    ASSERT_TRUE((std::is_class<internal::cti_host_t>::value));
    ASSERT_TRUE((std::is_same<decltype(external::cti_host_t::hostname)
                             ,decltype(internal::cti_host_t::hostname)
                             >::value));
    ASSERT_TRUE((std::is_same<decltype(external::cti_host_t::numPes)
                             ,decltype(internal::cti_host_t::numPes)
                             >::value));
    EXPECT_EQ(offsetof(external::cti_host_t, hostname), offsetof(internal::cti_host_t, hostname));
    EXPECT_EQ(offsetof(external::cti_host_t, numPes), offsetof(internal::cti_host_t, numPes));
}

// test cti_hostsList_t
TEST_F(CTIHeaderUnitTest, test_cti_hostsList_t)
{
    ASSERT_TRUE((std::is_class<external::cti_hostsList_t>::value));
    ASSERT_TRUE((std::is_class<internal::cti_hostsList_t>::value));
    ASSERT_TRUE((std::is_same<decltype(external::cti_hostsList_t::numHosts)
                             ,decltype(internal::cti_hostsList_t::numHosts)
                             >::value));
    // Ensure hosts is a cti_host_t for both external and internal types
    ASSERT_TRUE((std::is_same<decltype(external::cti_hostsList_t::hosts)
                             ,external::cti_host_t *
                             >::value));
    ASSERT_TRUE((std::is_same<decltype(internal::cti_hostsList_t::hosts)
                             ,internal::cti_host_t *
                             >::value));
    EXPECT_EQ(offsetof(external::cti_hostsList_t, numHosts), offsetof(internal::cti_hostsList_t, numHosts));
    EXPECT_EQ(offsetof(external::cti_hostsList_t, hosts), offsetof(internal::cti_hostsList_t, hosts));
}

// test cti_wlm_type
TEST_F(CTIHeaderUnitTest, test_cti_wlm_type_t)
{
    ASSERT_TRUE((std::is_enum<external::cti_wlm_type>::value));
    ASSERT_TRUE((std::is_enum<internal::cti_wlm_type>::value));
    ASSERT_TRUE((std::is_same<typename std::underlying_type<external::cti_wlm_type>::type
                             ,typename std::underlying_type<internal::cti_wlm_type>::type
                             >::value));
    EXPECT_EQ(external::CTI_WLM_NONE, internal::CTI_WLM_NONE);
    EXPECT_EQ(external::CTI_WLM_CRAY_SLURM, internal::CTI_WLM_CRAY_SLURM);
    EXPECT_EQ(external::CTI_WLM_SSH, internal::CTI_WLM_SSH);
}

// test other types
TEST_F(CTIHeaderUnitTest, test_other_types)
{
    ASSERT_TRUE((std::is_fundamental<external::cti_app_id_t>::value));
    ASSERT_TRUE((std::is_fundamental<internal::cti_app_id_t>::value));
    ASSERT_TRUE((std::is_same<external::cti_app_id_t
                             ,internal::cti_app_id_t
                             >::value));
    ASSERT_TRUE((std::is_fundamental<external::cti_session_id_t>::value));
    ASSERT_TRUE((std::is_fundamental<internal::cti_session_id_t>::value));
    ASSERT_TRUE((std::is_same<external::cti_session_id_t
                             ,internal::cti_session_id_t
                             >::value));
    ASSERT_TRUE((std::is_fundamental<external::cti_manifest_id_t>::value));
    ASSERT_TRUE((std::is_fundamental<internal::cti_manifest_id_t>::value));
    ASSERT_TRUE((std::is_same<external::cti_manifest_id_t
                             ,internal::cti_manifest_id_t
                             >::value));
}

// TODO: Test function types and BE library

