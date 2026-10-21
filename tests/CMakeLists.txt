project(GTProxy_tests)

find_package(GTest REQUIRED)
find_package(fmt REQUIRED)
find_package(glaze REQUIRED)
find_package(glm REQUIRED)
find_package(LibreSSL REQUIRED)
find_package(magic_enum REQUIRED)
find_package(spdlog REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(GTProxy_tests
    core/test_scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/scheduler.cpp
    license/test_license.cpp
    ${CMAKE_SOURCE_DIR}/src/license/license.cpp
    packet/test_dialog.cpp
    packet/test_game_packets.cpp
    packet/test_packet_id.cpp
    packet/test_text_packets.cpp
    packet/test_server_packets.cpp
    packet/test_packet_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/packet/packet_decoder.cpp
    player/test_inventory.cpp
    player/test_staff.cpp
    utils/test_text_parse.cpp
    utils/test_byte_stream.cpp
    utils/test_chat_text.cpp
    utils/test_roulette.cpp
    world/test_object_finder.cpp
    world/test_player_finder.cpp
    world/test_world_parse.cpp
    item/test_item_database.cpp
    utils/test_hash_zlib.cpp
    ${CMAKE_SOURCE_DIR}/src/world/tile_extra.cpp
    ${CMAKE_SOURCE_DIR}/src/item/item_database.cpp)

target_include_directories(GTProxy_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(GTProxy_tests PRIVATE
    GTest::gtest_main
    ZLIB::ZLIB
    fmt::fmt
    glaze::glaze
    glm::glm
    LibreSSL::Crypto
    magic_enum::magic_enum
    spdlog::spdlog)

target_compile_definitions(GTProxy_tests PRIVATE
    SPDLOG_FMT_EXTERNAL
    "GTPROXY_TEST_RESOURCES_DIR=\"${CMAKE_SOURCE_DIR}/resources\"")

include(GoogleTest)
gtest_discover_tests(GTProxy_tests)
