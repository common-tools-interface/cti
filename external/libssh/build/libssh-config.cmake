get_filename_component(LIBSSH_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)

if (EXISTS "${LIBSSH_CMAKE_DIR}/CMakeCache.txt")
    # In build tree
    include(${LIBSSH_CMAKE_DIR}/libssh-build-tree-settings.cmake)
else()
    set(LIBSSH_INCLUDE_DIR /cray/css/users/azahdeh/projects/cti_sles/external/install/include)
endif()

set(LIBSSH_LIBRARY /cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so)
set(LIBSSH_LIBRARIES /cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so)

set(LIBSSH_THREADS_LIBRARY /cray/css/users/azahdeh/projects/cti_sles/external/install/lib/libssh.so)
