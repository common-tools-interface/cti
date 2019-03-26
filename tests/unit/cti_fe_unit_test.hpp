#pragma once

#include "frontend/cti_fe_iface.h"

#include "MockFrontend/Frontend.hpp"

// internal cti functions to facilitate testing
void _cti_setFrontend(std::unique_ptr<Frontend>&& expiring);
cti_app_id_t _cti_registerApp(std::unique_ptr<App>&& expiring);
App& _cti_getApp(cti_app_id_t const appId);

static constexpr auto SUCCESS = int{0};
static constexpr auto FAILURE = int{1};

static constexpr auto APP_ERROR = cti_app_id_t{0};

// The fixture for unit testing the C frontend interface
class CTIFEUnitTest : public ::testing::Test
{
protected:
	CTIFEUnitTest();
	virtual ~CTIFEUnitTest();
};

// The fixture for unit testing the C app interface
class CTIAppUnitTest : public CTIFEUnitTest
{
protected: // variables
	cti_app_id_t const appId;
	MockApp::Nice& mockApp;

protected: // interface
	CTIAppUnitTest();
	~CTIAppUnitTest();
};
