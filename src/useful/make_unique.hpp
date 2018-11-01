/******************************************************************************\
 * make_unique.hpp - shim for GCC 4.8
 *
 * Copyright 2018 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <memory>

template <typename T>
using UniquePtrDestr = std::unique_ptr<T, std::function<void(T*)>>;

namespace shim {

#ifdef __cpp_lib_make_unique
	using std::make_unique;
#else
	template<typename T, typename... Args>
	std::unique_ptr<T> make_unique(Args&&... args) {
		return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	}
#endif
}
