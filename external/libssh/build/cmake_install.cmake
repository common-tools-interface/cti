# Install script for directory: /cray/css/users/azahdeh/projects/cti_sles/external/libssh

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

if(NOT CMAKE_INSTALL_COMPONENT OR "${CMAKE_INSTALL_COMPONENT}" STREQUAL "pkgconfig")
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/pkgconfig/libssh.pc;/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/pkgconfig/libssh_threads.pc")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/pkgconfig" TYPE FILE FILES
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/libssh.pc"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/libssh_threads.pc"
    )
endif()

if(NOT CMAKE_INSTALL_COMPONENT OR "${CMAKE_INSTALL_COMPONENT}" STREQUAL "pkgconfig")
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/pkgconfig/libssh.pc;/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/pkgconfig/libssh_threads.pc")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/pkgconfig" TYPE FILE FILES
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/libssh.pc"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/libssh_threads.pc"
    )
endif()

if(NOT CMAKE_INSTALL_COMPONENT OR "${CMAKE_INSTALL_COMPONENT}" STREQUAL "devel")
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/cmake/libssh/libssh-config.cmake;/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/cmake/libssh/libssh-config-version.cmake")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "/cray/css/users/azahdeh/projects/cti_sles/external/install/lib/cmake/libssh" TYPE FILE FILES
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/libssh-config.cmake"
    "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/libssh-config-version.cmake"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/doc/cmake_install.cmake")
  include("/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/include/cmake_install.cmake")
  include("/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/src/cmake_install.cmake")
  include("/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/examples/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/cray/css/users/azahdeh/projects/cti_sles/external/libssh/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
