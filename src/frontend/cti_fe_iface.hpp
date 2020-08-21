/*********************************************************************************\
 * cti_fe_iface.hpp - External C interface for the cti frontend.
 *
 * (C) Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
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

#include "cti_defs.h"

// Need to include external interface definitions
#include "common_tools_fe.h"

#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations
class App;
class Session;
class Manifest;

class FE_iface final {
private:
    // store and associate an arbitrary C++ object with an id (to make it accessible to C clients)
    template <typename IdType, typename T>
    class Registry {
    private: // variables
        std::unordered_map<IdType, std::weak_ptr<T>> m_list;
        IdType m_id = IdType{};

    public: // interface
        // Remove an id from the list
        void erase(IdType const id) { m_list.erase(id); }
        // Test if an id is still valid
        bool isValid(IdType const id)
        {
            // Ensure the id is valid
            auto const idWeakPtrPair = m_list.find(id);
            if (idWeakPtrPair != m_list.end()) {
                // Check if the pointer is valid
                if (!idWeakPtrPair->second.expired()) {
                    return true;
                } else {
                    // Pointer is no longer valid, cleanup the id
                    erase(id);
                    return false;
                }
            } else {
                return false;
            }
        }
        // Get a handle to the object.
        std::shared_ptr<T> get_handle(IdType const id)
        {
            auto const idWeakPtrPair = m_list.find(id);
            if (idWeakPtrPair != m_list.end()) {
                if (auto sharedPtr = idWeakPtrPair->second.lock()) {
                    return sharedPtr;
                } else {
                    // Cleanup the id
                    erase(id);
                    throw std::runtime_error("ID " + std::to_string(id) + " is no longer valid");
                }
            } else {
                throw std::runtime_error("ID " + std::to_string(id) + " invalid");
            }
        }
        // Add a weak_ptr to the registry
        IdType add(std::weak_ptr<T> wp)
        {
            auto const newId = ++m_id;
            m_list.emplace(std::make_pair(newId, wp));
            return newId;
        }
    };
private: // Static internal data
    // Error string we export to callers - we want this to leak!
    static char *       _cti_err_str;
    static std::string  m_err_str;
    // Attribs string we export to callers - we want this to leak!
    static char *       _cti_attr_str;
private: // Internal data
    // Internal associations between iterface ids and internal objects
    Registry<cti_app_id_t,App> m_app_registry;
    Registry<cti_session_id_t,Session> m_session_registry;
    Registry<cti_manifest_id_t,Manifest> m_manifest_registry;

private:
    // Used to set the external facing error string
    static void set_error_str(std::string str);

public:
    // Used to obtain a pointer to the internal error string.
    // This is for external consumption.
    static const char *get_error_str();
    // Used to obtain a pointer to the internal attribute string.
    static const char *get_attr_str(const char *value);

    // Return codes
    static constexpr auto SUCCESS = int{0};
    static constexpr auto FAILURE = int{1};

    static constexpr auto APP_ERROR      = cti_app_id_t{0};
    static constexpr auto SESSION_ERROR  = cti_session_id_t{0};
    static constexpr auto MANIFEST_ERROR = cti_manifest_id_t{0};

    // Safely run code that can throw and use it to set cti error instead.
    // A C api should never allow an exception to escape the runtime.
    template <typename FuncType, typename ReturnType = decltype(std::declval<FuncType>()())>
    static ReturnType
    runSafely(std::string const& caller, FuncType&& func, ReturnType const onError) {
        try {
            return std::forward<FuncType>(func)();
        } catch (std::exception const& ex) {
            auto const message = std::string{ex.what() ? ex.what() : "(null error string)"};
            set_error_str(caller + ": " + message);
            return onError;
        }
    }

    // App accessors/mutators
    cti_app_id_t
    trackApp(std::weak_ptr<App> wp) { return m_app_registry.add(wp); }
    std::shared_ptr<App>
    getApp(cti_app_id_t id) { return m_app_registry.get_handle(id); }
    bool
    validApp(cti_app_id_t id) { return m_app_registry.isValid(id); }
    void
    removeApp(cti_app_id_t id) { m_app_registry.erase(id); }
    // Session accessors/mutators
    cti_session_id_t
    trackSession(std::weak_ptr<Session> wp) { return m_session_registry.add(wp); }
    std::shared_ptr<Session>
    getSession(cti_session_id_t id) { return m_session_registry.get_handle(id); }
    bool
    validSession(cti_session_id_t id) { return m_session_registry.isValid(id); }
    void
    removeSession(cti_session_id_t id) { m_session_registry.erase(id); }
    // Manifest accessors/mutators
    cti_manifest_id_t
    trackManifest(std::weak_ptr<Manifest> wp) { return m_manifest_registry.add(wp); }
    std::shared_ptr<Manifest>
    getManifest(cti_manifest_id_t id) { return m_manifest_registry.get_handle(id); }
    bool
    validManifest(cti_manifest_id_t id) { return m_manifest_registry.isValid(id); }
    void
    removeManifest(cti_manifest_id_t id) { m_manifest_registry.erase(id); }

public: // Constructor/destructors
    FE_iface();
    ~FE_iface() = default;
    FE_iface(const FE_iface&) = delete;
    FE_iface& operator=(const FE_iface&) = delete;
    FE_iface(FE_iface&&) = delete;
    FE_iface& operator=(FE_iface&&) = delete;
};
