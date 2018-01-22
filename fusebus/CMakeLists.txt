execute_process(COMMAND pkg-config --cflags-only-I fuse
                RESULT_VARIABLE pkg_err
                OUTPUT_VARIABLE pkg_includes
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT pkg_err EQUAL 0)
  message(FATAL_ERROR "Failed to execute pkg-config --cflags-only-I fuse ${pkg_err}")
endif()

execute_process(COMMAND pkg-config --cflags-only-other fuse
                RESULT_VARIABLE pkg_err
                OUTPUT_VARIABLE pkg_cflags
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT pkg_err EQUAL 0)
  message(FATAL_ERROR "Failed to execute pkg-config --cflags-only-I fuse")
endif()

execute_process(COMMAND pkg-config --libs-only-L --libs-only-other fuse
                RESULT_VARIABLE pkg_err
                OUTPUT_VARIABLE pkg_lflags
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT pkg_err EQUAL 0)
  message(FATAL_ERROR "Failed to execute pkg-config --libs-only-L "
                      "--libs-only-other fuse")
endif()

execute_process(COMMAND pkg-config --libs-only-l fuse
                RESULT_VARIABLE pkg_err
                OUTPUT_VARIABLE pkg_libs
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT pkg_err EQUAL 0)
  message(FATAL_ERROR "Failed to execute pkg-config --libs-only-L "
                      "--libs-only-other fuse")
endif()


add_library(fusebus STATIC fusebus.cc)
add_library(fusebus_client STATIC fusebus_client.cc)

add_executable(fusebus.exe fusebus_main.cc)
set_target_properties(fusebus.exe PROPERTIES OUTPUT_NAME fusebus)

# NOTE(josh): on ubuntu 16.04 this is `-I/usr/include/fuse`, but we dont
# want to #include <fuse.h>, we want to #include <fuse/fuse.h> so we ignore
# this one.
# target_include_directories(fusebus PUBLIC ${pkg_includes})

# NOTE(josh): on ubuntu 16.04 this is `-D_FILE_OFFSET_BITS=64`
target_compile_options(fusebus PUBLIC ${pkg_cflags})
target_compile_options(fusebus_client PUBLIC ${pkg_cflags})
target_compile_options(fusebus.exe PUBLIC ${pkg_cflags})

# NOTE(josh): on ubuntu 16.04 this is `-pthread`
set_target_properties(fusebus PROPERTIES LINK_FLAGS ${pkg_lflags})
set_target_properties(fusebus_client PROPERTIES LINK_FLAGS ${pkg_lflags})
set_target_properties(fusebus.exe PROPERTIES LINK_FLAGS ${pkg_lflags})

# NOTE(josh): on ubuntu 16.04 this is `-fuse`
target_link_libraries(fusebus PUBLIC ${pkg_libs} re2)
target_link_libraries(fusebus_client PUBLIC fusebus ${pkg_libs} re2)
target_link_libraries(fusebus.exe fusebus)

set(fusebus_files
    fusebus.h
    fusebus.cc
    fusebus_client.h
    fusebus_client.cc
    fusebus_main.cc
    test/bitset_test.cc
    test/client_test.cc
    test/clientmgr_test.cc
    test/ringbuffer_test.cc
    test/util_test.cc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fusebus_format.stamp
                   COMMAND clang-format -style file -i ${fusebus_files}
                   COMMAND touch ${CMAKE_CURRENT_BINARY_DIR}/fusebus_format.stamp
                   DEPENDS ${fusebus_files}
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_target(fusebus_format
                  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fusebus_format.stamp)
add_dependencies(format fusebus_format)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fusebus_lint.stamp
                   COMMAND cpplint ${fusebus_files}
                   COMMAND touch ${CMAKE_CURRENT_BINARY_DIR}/fusebus_lint.stamp
                   DEPENDS ${fusebus_files}
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_target(fusebus_lint
                  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fusebus_lint.stamp)

# NOTE(josh): format before lint-check, avoid formatting lint
add_dependencies(fusebus_lint fusebus_format)
add_dependencies(lint fusebus_lint)

add_subdirectory(doc)
add_subdirectory(test)
