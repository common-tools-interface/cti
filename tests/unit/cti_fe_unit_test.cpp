#include "cti_fe_unit_test.hpp"

using ::testing::Return;

/* cti frontend C interface tests */

// Tests that the frontend type is set to mock.
TEST_F(CTIFEUnitTest, HaveMockFrontend)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock getWLMType
	EXPECT_CALL(mockFrontend, getWLMType())
		.WillRepeatedly(Return(CTI_WLM_MOCK));

	// run the test
	ASSERT_EQ(cti_current_wlm(), CTI_WLM_MOCK);
}
