# Clean test using new API
add_executable(clean_test clean_test.cpp)

# Link the bcsv library to the test
target_link_libraries(clean_test PRIVATE bcsv)

# Set output directory
set_target_properties(clean_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
