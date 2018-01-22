add_executable(fusebus-bitset_test bitset_test.cc)
target_link_libraries(fusebus-bitset_test gtest gtest_main fusebus)
add_test(NAME fusebus-bitset_test
         COMMAND fusebus-bitset_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(fusebus-client_test client_test.cc)
target_link_libraries(fusebus-client_test gtest gtest_main fusebus_client re2)
add_test(NAME fusebus-client_test
         COMMAND fusebus-client_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(fusebus-clientmgr_test clientmgr_test.cc)
target_link_libraries(fusebus-clientmgr_test gtest gtest_main fusebus re2)
add_test(NAME fusebus-clientmgr_test
         COMMAND fusebus-clientmgr_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(fusebus-ringbuffer_test ringbuffer_test.cc)
target_link_libraries(fusebus-ringbuffer_test gtest gtest_main fusebus)
add_test(NAME fusebus-ringbuffer_test
         COMMAND fusebus-ringbuffer_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(fusebus-util_test util_test.cc)
target_link_libraries(fusebus-util_test gtest gtest_main fusebus)
add_test(NAME fusebus-util_test
         COMMAND fusebus-util_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

