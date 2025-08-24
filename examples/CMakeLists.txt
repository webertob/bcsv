add_executable(example example.cpp)
add_executable(simple_example simple_example.cpp)

# Link the bcsv library to the examples
target_link_libraries(example PRIVATE bcsv)
target_link_libraries(simple_example PRIVATE bcsv)

# Add LZ4 streaming file example (independent of bcsv)
add_executable(lz4_streaming_file_example lz4_streaming_file_example.cpp)
target_link_libraries(lz4_streaming_file_example PRIVATE lz4_static)

# Add LZ4 raw streaming example (uses raw LZ4 streaming API)
add_executable(lz4_raw_streaming_example lz4_raw_streaming_example.cpp)
target_link_libraries(lz4_raw_streaming_example PRIVATE lz4_static)

# Add LZ4 independent blocks example (random access with fault tolerance)
add_executable(lz4_independent_blocks_example lz4_independent_blocks_example.cpp)
target_link_libraries(lz4_independent_blocks_example PRIVATE lz4_static)

# Set output directory
set_target_properties(example simple_example lz4_streaming_file_example lz4_raw_streaming_example lz4_independent_blocks_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
