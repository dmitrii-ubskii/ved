add_executable(ved
    main.cpp
    editor.cpp
    ops.cpp
)

target_compile_features(ved PRIVATE cxx_std_20)

find_package(Curses REQUIRED)
target_include_directories(ved
    PUBLIC
        .
        ../extern/ncursespp/include
    PRIVATE
        SYSTEM ${CURSES_INCLUDE_DIRS})
target_link_libraries(ved PRIVATE ncursespp ${CURSES_LIBRARIES} pthread dl)

