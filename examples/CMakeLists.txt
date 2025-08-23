add_executable(example example.cpp)

# Link the bcsv library to the example
target_link_libraries(example PRIVATE bcsv)

# Add compression streaming demo
add_executable(compression_streaming_demo compression_streaming_demo.cpp)
target_link_libraries(compression_streaming_demo PRIVATE bcsv)

# Set output directory
set_target_properties(example compression_streaming_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
