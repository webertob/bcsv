# Basic test placeholder
add_executable(basic_test basic_test.cpp)

# Link the bcsv library to the test
target_link_libraries(basic_test PRIVATE bcsv)

# Set output directory
set_target_properties(basic_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
