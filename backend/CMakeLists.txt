cmake_minimum_required(VERSION 3.25)

project(jirani_circle_api VERSION 0.1.0 LANGUAGES CXX)

find_package(Drogon CONFIG REQUIRED)

add_executable(jirani-circle-api
  src/main.cc
)

target_compile_features(jirani-circle-api PRIVATE cxx_std_20)
target_link_libraries(jirani-circle-api PRIVATE Drogon::Drogon)
