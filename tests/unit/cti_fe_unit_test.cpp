
#include "cti_fe_unit_test.hpp"

/* cti frontend C interface tests */

// Tests that the frontend type is set to mock.
TEST_F(CTIFEUnitTest, HaveMockFrontend) {
	ASSERT_EQ(cti_current_wlm(), CTI_WLM_MOCK);
}
