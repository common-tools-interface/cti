/*********************************************************************************\
 * cti_dlopen: manage a dynamically-loaded library, load pointers from function names
 *   and return function objects
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
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
