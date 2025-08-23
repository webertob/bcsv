# Basic test placeholder
add_executable(basic_test basic_test.cpp)

# Link the bcsv library to the test
target_link_libraries(basic_test PRIVATE bcsv)

# Set output directory
set_target_properties(basic_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Basic test new
add_executable(basic_test_new basic_test_new.cpp)
target_link_libraries(basic_test_new PRIVATE bcsv)
set_target_properties(basic_test_new PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# CRC performance test (if Boost is available and file exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/crc_performance_test.cpp")
    add_executable(crc_performance_test crc_performance_test.cpp)
    target_link_libraries(crc_performance_test PRIVATE bcsv)
    set_target_properties(crc_performance_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()
