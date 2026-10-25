cmake_minimum_required(VERSION 3.20)
project(mcpqt_webio VERSION 0.2.0 LANGUAGES CXX)

# ----- C++ / Qt boilerplate ------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Plugins are QObject-derived Qt plugins, so moc must run.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

# ----- generic HTTP client sources -----------------------------------------
# Minimal, website-agnostic transport layer. It performs raw GET and form POST
# requests and returns responses verbatim.
set(WEBIO_NET_SOURCES
    src/net/http_client.cpp
    src/net/http_client_qt.cpp
    src/net/net_utils.cpp
)

set(WEBIO_NET_HEADERS
    src/net/blocked_url_error.hpp
    src/net/connect_error.hpp
    src/net/http_client.hpp
    src/net/http_error.hpp
    src/net/http_response.hpp
    src/net/net_utils.hpp
    src/net/timeout_error.hpp
    src/net/tls_verify.hpp
)

add_library(mcpqt_webio MODULE
    src/webio_plugin.cpp
    src/webio_plugin.json
    src/plugin.cpp
    src/plugin.h
    ${WEBIO_NET_SOURCES}
    ${WEBIO_NET_HEADERS}
)
set_target_properties(mcpqt_webio PROPERTIES PREFIX "")

target_include_directories(mcpqt_webio PRIVATE
    src
    src/net
)

target_link_libraries(mcpqt_webio PRIVATE
    Qt6::Core
    Qt6::Network
)

target_compile_options(mcpqt_webio PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
)
