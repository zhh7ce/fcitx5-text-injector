cmake_minimum_required(VERSION 3.10)

project(fcitx5-text-injector VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
find_package(Fcitx5Core REQUIRED)
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    find_package(nlohmann_json QUIET)
endif()

add_library(text_injector MODULE
    src/module.cpp
    src/ipc_server.cpp
)
target_include_directories(text_injector PRIVATE src)
target_link_libraries(text_injector Fcitx5::Core)

if(nlohmann_json_FOUND)
    target_link_libraries(text_injector nlohmann_json::nlohmann_json)
else()
    target_include_directories(text_injector PRIVATE /usr/include)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(TARGET text_injector APPEND_STRING PROPERTY
                 LINK_FLAGS " -Wl,--no-undefined")
endif()

set_target_properties(text_injector PROPERTIES PREFIX "")

install(TARGETS text_injector LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5)
install(FILES data/text_injector.conf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon)
