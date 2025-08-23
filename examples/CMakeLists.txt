add_executable(example example.cpp)

# Link the bcsv library to the example
target_link_libraries(example PRIVATE bcsv)

# Set output directory
set_target_properties(example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
