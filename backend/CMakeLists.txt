cmake_minimum_required(VERSION 3.25)

project(jirani_circle_api VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)

add_executable(jirani-circle-api
  src/main.cc
  src/api_routes.cc
)

target_link_libraries(jirani-circle-api PRIVATE Drogon::Drogon unofficial-sodium::sodium)

add_executable(jirani-circle-bootstrap-admin
  src/bootstrap_admin.cc
)

target_link_libraries(jirani-circle-bootstrap-admin
  PRIVATE
    Drogon::Drogon
    unofficial-sodium::sodium
)
