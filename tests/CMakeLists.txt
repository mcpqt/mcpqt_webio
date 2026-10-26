add_executable(test_webio test_webio.cpp)
set_target_properties(test_webio PROPERTIES AUTOMOC ON)
target_include_directories(test_webio PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/net
)
target_link_libraries(test_webio PRIVATE
    webio_net
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
target_compile_definitions(test_webio PRIVATE
    WEBIO_PLUGIN_PATH="$<TARGET_FILE:mcpqt_webio>"
)
add_dependencies(test_webio mcpqt_webio)
add_test(NAME webio.unit COMMAND test_webio)
