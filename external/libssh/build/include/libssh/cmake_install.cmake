# Install script for directory: /cray/css/users/azahdeh/projects/cti_sles/external/libssh/include/libssh

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/cray/css/users/azahdeh/projects/cti_sles/external/install")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

if(NOT CMAKE_INSTALL_COMPONENT OR "${CMAKE_INSTALL_COMPONENT}" STREQUAL "headers")
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/cray/css/users/azahdeh/projects/cti_sles/external/install/include/libssh/callbacks.h;/cray/css/users/azahdeh/projects/cti_sles/external/install/include/libssh/libssh.h;/cray/css/users/azahdeh/projects/cti_sles/external/install/include/libssh/ssh2.h;/cray/css/users/azahdeh/projects/cti_sles/external/install/include/libssh/legacy.h;/cray/css/users/azahdeh/projects/cti_sles/external/install/include/libssh/libsshpp.hpp")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "/cray/css/users/azahdeh/projects/cti_sles/external/install/include/libssh" TYPE FILE FILES
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/include/libssh/callbacks.h"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/include/libssh/libssh.h"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/include/libssh/ssh2.h"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/include/libssh/legacy.h"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/include/libssh/libsshpp.hpp"
    )
endif()

