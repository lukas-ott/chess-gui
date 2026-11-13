# kiebitz_training: the training plan run in order (openings, middlegame, tactics, endgames,
# self-study) and the minutes trained per day. Like core, it must never depend on Qt; the
# Training page lives in app/.

add_library(kiebitz_training
    src/log.cpp
    src/session.cpp)
add_library(kiebitz::training ALIAS kiebitz_training)

target_include_directories(kiebitz_training PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_training
    PUBLIC kiebitz::core
    PRIVATE kiebitz::sql)
kiebitz_set_warnings(kiebitz_training)
