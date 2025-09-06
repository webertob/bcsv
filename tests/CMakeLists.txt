# Google Test Framework
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "Google Test not found, using FetchContent to download...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Google Test comprehensive test suite
add_executable(bcsv_gtest 
    bcsv_comprehensive_test.cpp
)

# Link the bcsv library and Google Test
target_link_libraries(bcsv_gtest PRIVATE 
    bcsv 
    gtest 
    gtest_main
)

# Set output directory
set_target_properties(bcsv_gtest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Clean test using new API
add_executable(clean_test clean_test.cpp)

# Link the bcsv library to the test
target_link_libraries(clean_test PRIVATE bcsv)

# Set output directory
set_target_properties(clean_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
