cmake_minimum_required(VERSION 3.12...3.16)

project(threadsafe
  DESCRIPTION "Threading utilities: object oriented (read/write) locking and more"
  LANGUAGES CXX
)

include(AICxxProject)

#==============================================================================
# BUILD PROJECT
#

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library(threadsafe_ObjLib OBJECT)

# The list of source files.
target_sources(threadsafe_ObjLib
  PRIVATE
    "PointerStorage.cxx"

    "AIMutex.h"
    "AIReadWriteMutex.h"
    "AIReadWriteSpinLock.h"
    "ConditionVariable.h"
    "PointerStorage.h"
    "ObjectTracker.h"
    "ObjectTracker.inl.h"

    "threadsafe.h"
)

# Required include search-paths.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(threadsafe_ObjLib
  PUBLIC
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}" # For sys.h and debug.h.
)

# Require support for C++17.
target_compile_features(threadsafe_ObjLib
  PUBLIC cxx_std_17
)

# Set link dependencies.
target_link_libraries(threadsafe_ObjLib
  PUBLIC
    AICxx::utils
)

# Create an ALIAS target.
add_library(AICxx::threadsafe ALIAS threadsafe_ObjLib)

# Prepend this object library to the list.
set(AICXX_OBJECTS_LIST AICxx::threadsafe ${AICXX_OBJECTS_LIST} CACHE INTERNAL "List of OBJECT libaries that this project uses.")
