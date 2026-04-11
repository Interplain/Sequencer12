cmake_minimum_required(VERSION 3.20)

project(sequencer12 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sequencer12
    src/main.cpp
    app/app.cpp
    core/scheduler.cpp
    devices/sequencer/sequencer_device.cpp
    devices/sequencer/pattern_bank.cpp
    devices/sequencer/arp_engine.cpp
    devices/sequencer/chords/chord_library.cpp
    devices/sequencer/chords/user_chords.cpp
    platform/midi/midi_clock.cpp
    platform/midi/midi_parser.cpp
)

target_include_directories(sequencer12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)