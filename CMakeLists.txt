cmake_minimum_required(VERSION 3.20)

project(reac_decoder_example LANGUAGES CXX)

if(WIN32)
    add_executable(reac_play
        src/main.cpp
        src/pcap_capture.cpp
        src/reac_decoder.cpp
        src/reac_settings.cpp
        src/waveout_player.cpp
    )

    target_compile_features(reac_play PRIVATE cxx_std_17)
    target_compile_definitions(reac_play PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(reac_play PRIVATE advapi32 winmm)

    add_library(reac_asio SHARED
        src/reac_asio_driver.cpp
        src/pcap_capture.cpp
        src/reac_decoder.cpp
        src/reac_ring_buffer.cpp
        src/reac_settings.cpp
        src/waveout_player.cpp
    )

    target_compile_features(reac_asio PRIVATE cxx_std_17)
    target_compile_definitions(reac_asio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(reac_asio PRIVATE ole32 advapi32 uuid winmm)

    add_executable(reac_config WIN32
        src/config_app.cpp
        src/pcap_capture.cpp
        src/reac_settings.cpp
        src/waveout_player.cpp
    )

    target_compile_features(reac_config PRIVATE cxx_std_17)
    target_compile_definitions(reac_config PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(reac_config PRIVATE advapi32 winmm)
endif()

if(APPLE)
    add_executable(reac_macos_probe
        src/macos/reac_macos_probe.cpp
        src/macos/macos_pcap_capture.cpp
        src/reac_decoder.cpp
    )

    target_include_directories(reac_macos_probe PRIVATE src src/macos)
    target_compile_features(reac_macos_probe PRIVATE cxx_std_17)
    target_link_libraries(reac_macos_probe PRIVATE pcap)
endif()
