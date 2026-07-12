# tests will be added here later
add_executable(test_tokenizer test_tokenizer.cpp)
target_link_libraries(test_tokenizer PRIVATE histd_core)

add_executable(test_index test_index.cpp)
target_link_libraries(test_index PRIVATE histd_core)

add_executable(test_failure_clusterer test_failure_clusterer.cpp)
target_link_libraries(test_failure_clusterer PRIVATE histd_core)