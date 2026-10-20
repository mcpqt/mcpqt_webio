cmake_minimum_required(VERSION 3.20)
project(mcpqt_template_plugin VERSION 0.1.0 LANGUAGES CXX)

# ----- C++ / Qt boilerplate ------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Plugins are QObject-derived Qt plugins, so moc must run.
set(CMAKE_AUTOMOC ON)

# ----- Find the host library ----------------------------------------------
#
# Preferred: build against an installed mcpqt_host:
#
#     cmake --install /path/to/mcpqt_host/build --prefix /usr/local
#     cmake -S . -B build
#
# Alternatively, build against an in-tree host checkout without installing:
#
#     cmake -S . -B build -DMCPQT_HOST_SOURCE_DIR=/path/to/mcpqt_host
#
find_package(mcp 0.1 QUIET)
if(NOT mcp_FOUND)
    if(MCPQT_HOST_SOURCE_DIR)
        add_subdirectory("${MCPQT_HOST_SOURCE_DIR}" mcpqt_host)
    else()
        message(FATAL_ERROR
            "mcp not found. Install mcpqt_host (cmake --install build) or "
            "configure with -DMCPQT_HOST_SOURCE_DIR=/path/to/mcpqt_host")
    endif()
endif()

# ----- The plugin module ---------------------------------------------------
#
# MODULE + no "lib" prefix produces mcpqt_template_plugin.so/.dylib/.dll,
# which is what QPluginLoader expects when pointed at a file.
# plugin.cpp/plugin.h are vendored copies of the host's plugin ABI so this
# repository is self-contained; keep them in sync if the ABI changes.
add_library(mcpqt_template_plugin MODULE template_plugin.cpp plugin.cpp)
set_target_properties(mcpqt_template_plugin PROPERTIES PREFIX "")
target_link_libraries(mcpqt_template_plugin PRIVATE mcp::mcp)

target_compile_options(mcpqt_template_plugin PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
)