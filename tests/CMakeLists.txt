#  _               _ 
# | |__   _____  _(_)
# | '_ \ / _ \ \/ / | MIT & Apache 2.0 dual licensed
# | | | |  __/>  <| | Version 1.0
# |_| |_|\___/_/\_\_| https://github.com/EmberEmu/hexi

set(EXECUTABLE_NAME unit_tests)

set(EXECUTABLE_SRC
    binary_stream.cpp
    binary_stream_pmc.cpp
    buffer_adaptor.cpp
    buffer_adaptor_pmc.cpp
    buffer_utility.cpp
    dynamic_buffer.cpp
    file_buffer.cpp
    intrusive_storage.cpp
    static_buffer.cpp
    tls_block_allocator.cpp
    null_buffer.cpp
	helpers.h
	final_action.h
    )

add_executable(${EXECUTABLE_NAME} ${EXECUTABLE_SRC})
target_link_libraries(${EXECUTABLE_NAME} gtest gtest_main)
target_include_directories(${EXECUTABLE_NAME} PRIVATE ../include)
gtest_discover_tests(${EXECUTABLE_NAME})

add_custom_command(TARGET ${EXECUTABLE_NAME} PRE_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${CMAKE_SOURCE_DIR}/tests/data ${CMAKE_CURRENT_BINARY_DIR}/data)

INSTALL(TARGETS ${EXECUTABLE_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})
INSTALL(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_PREFIX}/data)