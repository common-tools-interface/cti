# Install script for directory: /cray/css/users/azahdeh/projects/cti_sles/external/libssh/src

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

if(NOT CMAKE_INSTALL_COMPONENT OR "${CMAKE_INSTALL_COMPONENT}" STREQUAL "Unspecified")
  foreach(file
      "$ENV{DESTDIR}/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so.4.4.2"
      "$ENV{DESTDIR}/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so.4"
      "$ENV{DESTDIR}/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      file(RPATH_CHECK
           FILE "${file}"
           RPATH "")
    endif()
  endforeach()
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so.4.4.2;/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so.4;/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib" TYPE SHARED_LIBRARY FILES
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/src/libssh.so.4.4.2"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/src/libssh.so.4"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/src/libssh.so"
    )
  foreach(file
      "$ENV{DESTDIR}/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so.4.4.2"
      "$ENV{DESTDIR}/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so.4"
      "$ENV{DESTDIR}/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      if(CMAKE_INSTALL_DO_STRIP)
        execute_process(COMMAND "/usr/bin/strip" "${file}")
      endif()
    endif()
  endforeach()
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/src/threads/cmake_install.cmake")

endif()

