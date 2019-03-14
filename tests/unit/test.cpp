#include "gtest/gtest.h"

#include "TestFrontend.hpp"

namespace {

// The fixture for testing class Foo.
class FrontendTest : public ::testing::Test
{
	protected:
		FrontendTest()
		{
			// You can do set-up work for each test here.
		}

		~FrontendTest() override
		{
			// You can do clean-up work that doesn't throw exceptions here.
		}

		void SetUp() override
		{
			// Code here will be called immediately after the constructor (right before each test).
		}

		void TearDown() override
		{
			// Code here will be called immediately after each test (right before the destructor).
		}

		// Objects declared here can be used by all tests in the test case for Foo.
};

// Tests that the TestFrontend::getHostname() method returns a hostname.
TEST_F(FrontendTest, GetHostnameReturnsHostname) {
	TestFrontend frontend;
	EXPECT_NE(frontend.getHostname().size(), 0);
}

} // namespace

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}