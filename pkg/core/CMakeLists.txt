# Core utilities (util.h) and the platform layer (sys.h).
add_library(uni_core STATIC arena.c base64.c buf.c container.c fail.c quote.c str.c sys.c)
target_include_directories(uni_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
