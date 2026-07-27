cmake_minimum_required(VERSION 3.16)
project(Rublox LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.3.8)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG v0.1.36)
FetchContent_MakeAvailable(glad)

FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 0.9.9.8)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(lua GIT_REPOSITORY https://github.com/lua/lua.git GIT_TAG v5.4.4)
FetchContent_MakeAvailable(lua)

FetchContent_Declare(assimp GIT_REPOSITORY https://github.com/assimp/assimp.git GIT_TAG v5.2.5)
FetchContent_MakeAvailable(assimp)

FetchContent_Declare(enet GIT_REPOSITORY https://github.com/lsalzman/enet.git GIT_TAG v1.3.17)
FetchContent_MakeAvailable(enet)

file(GLOB LUA_SRC ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SRC EXCLUDE REGEX ".*lua\\.c|.*luac\\.c")

add_executable(Rublox main.cpp ${LUA_SRC})
target_include_directories(Rublox PRIVATE
    ${lua_SOURCE_DIR}
    ${glad_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${assimp_SOURCE_DIR}/include
    ${enet_SOURCE_DIR}/include
)
target_link_libraries(Rublox PRIVATE glfw glad assimp enet)

if(WIN32)
    add_custom_command(TARGET Rublox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:glfw> $<TARGET_FILE_DIR:Rublox>)
endif()