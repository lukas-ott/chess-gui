# Global build settings and a helper that applies the project's warning set to a target.

# Speed up rebuilds with ccache when it is installed.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

if(CHESSGUI_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# Apply warnings only to our own targets (third-party code is included as SYSTEM).
function(chessgui_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wcast-align -Wunused -Woverloaded-virtual -Wnull-dereference
        -Wdouble-promotion -Wimplicit-fallthrough)
    if(CHESSGUI_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
