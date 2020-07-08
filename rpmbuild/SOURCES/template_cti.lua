--[[

    Module cray-cti

    (C) COPYRIGHT CRAY INC.
    UNPUBLISHED PROPRIETARY INFORMATION.
    ALL RIGHTS RESERVED.

]]--

-- local vars: define & assign --

-- template variables ----------------------------------------------------------
local INSTALL_ROOT       = "[@%PREFIX_PATH%@]"
local MOD_LEVEL          = "[@%MODULE_VERSION%@]"
--------------------------------------------------------------------------------

local NICKNAME  = "cti"
local PE_DIR    = INSTALL_ROOT .. "/" .. NICKNAME .. "/" .. MOD_LEVEL

 -- module release info variables
local REL_FILE            = PE_DIR .. "/release_info"
local rel_info            = ""
if isFile(REL_FILE) then
    local f = io.open(REL_FILE, "r")
    local data = f:read("*all")
    f:close()
    if data ~= nil then rel_info = data end
end

 -- standard Lmod functions --

help ([[

The modulefile defines the system paths and
variables for the product cray-cti.

]] .. rel_info .. "\n" .. [[

===================================================================
To re-display ]] .. tostring(myModuleName()) .. "/" .. MOD_LEVEL .. [[ release information,
type:    less ]] .. REL_FILE .. "\n" .. [[
===================================================================

]])

whatis("Loads the Cray Tools Interface.")

 -- environment modifications --

setenv (           "CTI_VERSION",              MOD_LEVEL                     )
setenv (           "CTI_INSTALL_DIR",          PE_DIR                        )
setenv (           "PE_CTI_MODULE_NAME",       myModuleName()                )

append_path   (    "PE_PRODUCT_LIST",          "CRAY-CTI"                    )

prepend_path  (    "PE_PKGCONFIG_PRODUCTS",    "PE_CTI"                      )
prepend_path  (    "PKG_CONFIG_PATH",          PE_DIR .. "/lib/pkgconfig"    )
prepend_path  (    "PE_PKG_CONFIG_PATH",       PE_DIR .. "/lib/pkgconfig"    )

 -- set LD_LIBRARY_PATH only if non-default version
local DEFAULT_VER_FILE = INSTALL_ROOT .. "/lmod/modulefiles/core/cray-cti/.version"
local default_ver      = nil
if isFile(DEFAULT_VER_FILE) then
    local f = io.open(DEFAULT_VER_FILE, "r")
    local data = f:read("*line")
    data = f:read("*line")
    f:close()
    default_ver = string.match(data, "set ModulesVersion \"(.+)\"")
end

if (default_ver == nil) or (MOD_LEVEL == default_ver) then
    prepend_path  (    "CRAY_LD_LIBRARY_PATH",     PE_DIR .. "/lib"              )
else
    prepend_path  (    "LD_LIBRARY_PATH",          PE_DIR .. "/lib"              )
end
