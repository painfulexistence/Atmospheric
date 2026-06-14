# Header-only library of reusable gameplay components shared by the examples.
# Consumers just need the include directory; all real dependencies (engine, glm,
# fmt, ...) are already linked by each example target.
add_library(ExampleComponents INTERFACE)
target_include_directories(ExampleComponents INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
