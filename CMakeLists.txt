cmake_minimum_required(VERSION 3.16)
project(my_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json 3.7.3 CONFIG REQUIRED)

# set(TgBot_DIR )
find_package(TgBot CONFIG REQUIRED PATHS "/home/ans/Desktop/tgbot-cpp/build/install")

add_executable(my_bot src/main.cpp)
target_link_libraries(my_bot PRIVATE TgBot::TgBot nlohmann_json::nlohmann_json)
target_include_directories(my_bot PRIVATE "/home/ans/Desktop/tgbot-cpp/build/install/include")
