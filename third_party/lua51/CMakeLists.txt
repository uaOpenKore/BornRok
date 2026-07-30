# Vendored Lua 5.1.5 (MIT) — used to load the RO client's compiled .lub data tables (skill tree,
# sprite offsets, status icons, effects, ...) straight from the GRF at runtime, exactly like the
# original client runs them in its Lua VM. See LICENSE.
#
# The ONLY local change vs upstream 5.1.5 is in lundump.c: the bytecode loader accepts RO's
# 32-bit-compiled .lub on a 64-bit host (RO .lub have sizeof(size_t)==4). Both host and .lub are
# little-endian on every target we ship (x86 / ARM), which the patch relies on.

add_library(lua51 STATIC
    lapi.c lauxlib.c lbaselib.c lcode.c ldblib.c ldebug.c ldo.c ldump.c lfunc.c lgc.c linit.c
    liolib.c llex.c lmathlib.c lmem.c loadlib.c lobject.c lopcodes.c loslib.c lparser.c lstate.c
    lstring.c lstrlib.c ltable.c ltablib.c ltm.c lundump.c lvm.c lzio.c)

target_include_directories(lua51 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Lua's optional POSIX bits (loadlib dlopen, os.*) — harmless on desktop; guarded off where unsupported.
if(UNIX AND NOT ANDROID)
    target_compile_definitions(lua51 PRIVATE LUA_USE_POSIX)
    target_link_libraries(lua51 PRIVATE ${CMAKE_DL_LIBS})
endif()
if(UNIX)
    target_link_libraries(lua51 PRIVATE m)  # libm for Lua's math (part of the CRT on MSVC)
endif()

# Android: the NDK enables _FORTIFY_SOURCE=2 by default. During luaL_openlibs the vendored Lua
# scans library-name strings with strchr while registering the standard libs; bionic's fortified
# strchr (esp. on the x86_64 emulator) SIGABRTs on a benign over-read that is harmless on
# glibc/MSVC, crashing the client at startup (LuaTables ctor -> luaL_openlibs -> luaopen_os,
# "FORTIFY: strchr: prevented read past end of buffer"). Lua manages its own string bounds and is
# memory-safe on every other target we ship, so drop FORTIFY for just this vendored C. [S. 2026-07-19]
if(ANDROID)
    target_compile_options(lua51 PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
endif()

# Upstream C — silence its warnings so it doesn't trip -Werror / clutter the build log.
if(NOT MSVC)
    target_compile_options(lua51 PRIVATE -w)
else()
    # Harmless vendored-Lua warnings on MSVC x64 (S.): C4324 struct alignment padding (lua_longjmp),
    # C4334 32-bit shift widened to 64-bit (lgc/ltable hashing), plus the usual narrowing/unused ones.
    # Listed by number (not /W0) so it doesn't emit the "/W3 overridden" note and stays safe under /WX.
    target_compile_options(lua51 PRIVATE /wd4324 /wd4334 /wd4244 /wd4267 /wd4310 /wd4701 /wd4702)
endif()

add_library(uaro::lua51 ALIAS lua51)
