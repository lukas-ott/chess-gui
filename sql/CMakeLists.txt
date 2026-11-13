# kiebitz_sql: the small SQLite helpers the modules that keep data in SQLite share (statements
# that finalize themselves, binding and reading text). Header-only. Like core, it must never
# depend on Qt.

add_library(kiebitz_sql INTERFACE)
add_library(kiebitz::sql ALIAS kiebitz_sql)

target_include_directories(kiebitz_sql INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_sql INTERFACE thirdparty::sqlite)
