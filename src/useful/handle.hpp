#pragma once

/*********************************************************************************\
 * handles: RAII objects for file descriptor, MPIR barrier, C-style string
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 *********************************************************************************/

#include <memory>

#include "mpir_iface/mpir_iface.h"

namespace handle {
	// managed file descriptor
	struct Fd {
		int data;

		operator bool() const { return (data >= 0); }
		void reset() { if (*this) { ::close(data); data = int{}; } }
		int get() const { return data; }

		Fd(int data_) : data{data_} {}
		Fd(Fd&& moved) : data{std::move(moved.data)} {}
		~Fd() { reset(); }
	};

	// managed MPIR session
	struct MPIR {
		mpir_id_t data;
		operator bool() const { return (data >= 0); }
		void reset() { if (*this) { _cti_mpir_releaseInstance(data); data = mpir_id_t{-1}; } }
		mpir_id_t get() const { return data; }
		MPIR() : data{-1} {}
		MPIR(mpir_id_t data_) : data{data_} {}
		MPIR(MPIR&& moved) : data{std::move(moved.data)} { moved.data = mpir_id_t{-1}; }
		~MPIR() { reset(); }
	};

	// managed c-style string
	namespace {
		using cstr_type = std::unique_ptr<char, decltype(::free)*>;
	}
	struct cstr : cstr_type {
		cstr(char* str) : cstr_type{str, ::free} {}
	};
}
