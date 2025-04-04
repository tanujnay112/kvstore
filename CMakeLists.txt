cmake_minimum_required(VERSION 3.10)

# Force Unix Makefiles generator
if(NOT DEFINED CMAKE_GENERATOR)
    set(CMAKE_GENERATOR "Unix Makefiles" CACHE INTERNAL "Generator" FORCE)
endif()

project(kvstore)

# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()


# Output the build type for user information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Debug-specific options
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
# Release-specific options
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Show the actual flags being used
message(STATUS "Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")

# On macOS, manually set TBB paths for Homebrew
if(APPLE)
    # Check common Homebrew TBB installation locations
    set(POSSIBLE_TBB_INCLUDE_DIRS
        "/usr/local/include"
        "/opt/homebrew/include"
        "/usr/local/Cellar/tbb/latest/include"
        "/opt/homebrew/Cellar/tbb/latest/include"
    )
    
    set(POSSIBLE_TBB_LIBRARY_DIRS
        "/usr/local/lib"
        "/opt/homebrew/lib"
        "/usr/local/Cellar/tbb/latest/lib"
        "/opt/homebrew/Cellar/tbb/latest/lib"
    )
    
    # Find TBB include directory
    foreach(DIR ${POSSIBLE_TBB_INCLUDE_DIRS})
        if(EXISTS "${DIR}/tbb/concurrent_hash_map.h" OR EXISTS "${DIR}/oneapi/tbb/concurrent_hash_map.h")
            set(TBB_INCLUDE_DIRS "${DIR}")
            message(STATUS "Found TBB include directory: ${TBB_INCLUDE_DIRS}")
            break()
        endif()
    endforeach()
    
    # Find TBB library
    foreach(DIR ${POSSIBLE_TBB_LIBRARY_DIRS})
        if(EXISTS "${DIR}/libtbb.dylib" OR EXISTS "${DIR}/libtbb.a")
            set(TBB_LIBRARY_DIRS "${DIR}")
            set(TBB_LIBRARIES "tbb")
            message(STATUS "Found TBB library directory: ${TBB_LIBRARY_DIRS}")
            break()
        endif()
    endforeach()
    
    if(TBB_INCLUDE_DIRS AND TBB_LIBRARY_DIRS)
        set(TBB_FOUND TRUE)
        link_directories(${TBB_LIBRARY_DIRS})
    else()
        # Try to find TBB using find_package as a fallback
        find_package(TBB)
    endif()
else()
    # On non-macOS systems, use standard find_package
    find_package(TBB)
endif()
# Find TBB package
if(TBB_FOUND)
    message(STATUS "Found TBB: ${TBB_VERSION}")
else()
    message(FATAL_ERROR "TBB not found. Please install TBB library.")
endif()

# Include directories
include_directories(include ${TBB_INCLUDE_DIRS})

# Add the executable
add_executable(kvstore
    src/main.cpp
    src/kvstore.cpp
)
target_link_libraries(kvstore ${TBB_LIBRARIES})

# Add testing executable
add_executable(kvstore_test
    src/kvstore.cpp
    test/kvstore_test.cpp
)
target_link_libraries(kvstore_test ${TBB_LIBRARIES})

# Enable testing
enable_testing()
add_test(NAME kvstore_tests COMMAND kvstore_test)
