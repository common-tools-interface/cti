/*********************************************************************************\
 * cti_dlopen: manage a dynamically-loaded library, load pointers from function names
 *   and return function objects
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 *********************************************************************************/
#pragma once

#include <iostream>
#include <functional>

#include <stdlib.h>
#include <dlfcn.h>

namespace cti {

namespace Dlopen {
	template <class T>
	class NonCopyable {
	protected:
		NonCopyable() {}
		~NonCopyable() {}
		NonCopyable(NonCopyable&&);
		NonCopyable& operator= (NonCopyable&&);
	};

	/* helper function to force cast a void* function pointer to a typed C++ std::function */
	template <typename FnType>
	std::function<FnType> fptr_cast(void* fptr) {
		using FnPtrType = FnType*;
		using PtrSizeType = std::conditional<sizeof(fptr) == 4, long, long long>::type;
		return reinterpret_cast<FnPtrType>(reinterpret_cast<PtrSizeType>(fptr));
	}

	/* RAII class for dlopen handle. throws runtime error if loading failed */
	class Handle : private NonCopyable<Handle> {
	private:
		void *handle = NULL;

	public:
		/* initialization: make call to dlopen */
		Handle(std::string const& name) {
			dlerror();
			handle = dlopen(name.c_str(), RTLD_LAZY);
			if (!handle) {
				throw std::runtime_error(dlerror());
			}
		}

		/* destruction: make call to dlclose */
		~Handle() {
			dlclose(handle);
		}

		/* possibly load a function symbol (or return nullptr) */
		template <typename FnType>
		std::function<FnType> loadFailable(std::string const& fn_name){
			dlerror();
			void *raw_fn_ptr = dlsym(handle, fn_name.c_str());
			if (raw_fn_ptr == nullptr) {
				return nullptr;
			}
			return fptr_cast<FnType>(raw_fn_ptr);
		}

		/* load a function symbol and cast it to a typed std::function */
		template <typename FnType>
		std::function<FnType> load(std::string const& fn_name){
			dlerror(); /* clear error code */
			void *raw_fn_ptr = dlsym(handle, fn_name.c_str());
			char *error = NULL;
			if ((error = dlerror()) != NULL) {
				throw std::runtime_error(error);
			}
			return fptr_cast<FnType>(raw_fn_ptr);
		}
	};
} /* namespace cti::Dlopen */

} /* namespace cti */
