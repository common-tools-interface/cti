/*********************************************************************************\
 * temp_file_handle.hpp - generate a temporary file and remove it on destruction
 *
 * Copyright 2014-2019 Cray Inc.	All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 *********************************************************************************/

#pragma once

#include <string>
#include <memory>

class temp_file_handle
{
private:
	std::unique_ptr<char, decltype(&::free)> m_path;

public:
	temp_file_handle(std::string const& templ)
		: m_path{strdup(templ.c_str()), ::free}
	{
		// use template to generate filename
		mktemp(m_path.get());
		if (m_path.get()[0] == '\0') {
			throw std::runtime_error("mktemp failed");
		}
	}

	temp_file_handle(temp_file_handle&& moved)
		: m_path{std::move(moved.m_path)}
	{
		moved.m_path.reset();
	}

	~temp_file_handle()
	{
		if (m_path && remove(m_path.get()) < 0) {
			// could have been destroyed without file being opened
			std::cerr << "warning: remove " << std::string{m_path.get()} << " failed" << std::endl;
		}
	}

	char const* get() const { return m_path.get(); }
};