# CompileShaders.cmake
#
# Compiles bgfx shaders (.sc) into per-renderer bytecode (.bin) placed next to
# the target executable under <output>/shaders/<profile>/. The client loads
# these at runtime based on the active bgfx renderer (see render/Shader.hpp).
#
# Requires the bgfx "shaderc" tool. vcpkg's bgfx port installs it; we also look
# for it on PATH. If it cannot be found the build still succeeds — the client
# degrades gracefully to a clear-only frame and logs a warning at runtime.
#
#   client_compile_shaders(<target>
#       SHADERS  <vs_*.sc> <fs_*.sc> ...
#       VARYING  <varying.def.sc>)

# Optional: reuse pre-compiled shader bytecode instead of running shaderc.
# Point CLIENT_PREBUILT_SHADERS at a "shaders/" dir laid out as <profile>/<name>.bin
# (e.g. an existing build's build/<preset>/src/shaders). Compiled shader .bin is GPU
# bytecode per renderer profile (glsl/spirv/dx11/...) and does NOT depend on the CPU
# arch, so a cross build (arm64) can take the .bin from an x86/x64 build and skip
# shaderc/glslang entirely (S.: "шейдеры возьми по пути, чтобы не компилировать").
# When set: no shaderc is needed; the .bin are copied next to the exe and embedded.
set(CLIENT_PREBUILT_SHADERS "" CACHE PATH "Reuse compiled shader .bin from this shaders/ dir instead of running shaderc")

# Where the compiled .bin live: the prebuilt dir if given, else our shaderc output dir.
# CLIENT_PREBUILT_SHADERS may be a LIST of candidate dirs -- the first that exists wins
# (e.g. a native linux x64 build's shaders, falling back to a Windows build's shaders
# reachable through a WSL symlink). If none exist, returns the first for messaging.
function(_client_shader_outroot OUTVAR)
  if(CLIENT_PREBUILT_SHADERS)
    foreach(_cand IN LISTS CLIENT_PREBUILT_SHADERS)
      if(IS_DIRECTORY "${_cand}")
        set(${OUTVAR} "${_cand}" PARENT_SCOPE)
        return()
      endif()
    endforeach()
    list(GET CLIENT_PREBUILT_SHADERS 0 _first)
    set(${OUTVAR} "${_first}" PARENT_SCOPE)
  else()
    set(${OUTVAR} "${CMAKE_CURRENT_BINARY_DIR}/_compiled_shaders" PARENT_SCOPE)
  endif()
endfunction()

function(_client_find_shaderc OUTVAR)
  set(_found "")
  # Explicit HOST shaderc override (for cross builds like Android): point CLIENT_HOST_SHADERC at a
  # host-arch shaderc.exe (e.g. the one from the desktop win-msvc/vcpkg build). This is the only
  # shaderc that can run on the build host when the target is arm64-android. (S.)
  if(CLIENT_HOST_SHADERC AND EXISTS "${CLIENT_HOST_SHADERC}")
    set(${OUTVAR} "${CLIENT_HOST_SHADERC}" PARENT_SCOPE)
    return()
  endif()
  # When CROSS-COMPILING (e.g. Android on a Windows/Linux host), vcpkg's bgfx::shaderc is the TARGET-arch
  # binary (arm64-android) and cannot run on the build host -- ninja fails with "not recognized as a
  # command". shaderc is a BUILD-TIME tool, so only a HOST shaderc is usable: skip the target target and
  # look on PATH only. If none, shaders are skipped (graceful clear-only), letting the APK still build.
  if(NOT ANDROID AND NOT CMAKE_CROSSCOMPILING AND TARGET bgfx::shaderc)
    foreach(_prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG IMPORTED_LOCATION)
      get_target_property(_loc bgfx::shaderc ${_prop})
      if(_loc AND EXISTS "${_loc}")
        set(_found "${_loc}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT _found)
    find_program(CLIENT_SHADERC NAMES shaderc bgfx-shaderc shadercRelease)
    if(CLIENT_SHADERC AND EXISTS "${CLIENT_SHADERC}")
      set(_found "${CLIENT_SHADERC}")
    endif()
  endif()
  set(${OUTVAR} "${_found}" PARENT_SCOPE)
endfunction()

# Renderer profiles we target for desktop (v0). Mobile profiles (essl/metal)
# are added in v6. Format: "<subdir>|<shaderc --platform>|<vs profile>|<fs profile>"
set(_CLIENT_SHADER_PROFILES
  "glsl|linux|120|120"
  "spirv|linux|spirv|spirv"
)
if(WIN32)
  list(APPEND _CLIENT_SHADER_PROFILES
    "dx11|windows|s_5_0|s_5_0"
  )
endif()
if(ANDROID)
  # GLES3 (essl 300) for the Android/OpenGLES backend -- desktop GL 120 shaders don't satisfy GLSL ES's
  # stricter rules (precision qualifiers etc.). shader_profile_dir() returns "essl" on Android.
  list(APPEND _CLIENT_SHADER_PROFILES
    "essl|android|300_es|300_es"
  )
endif()
if(APPLE)
  # Metal for bgfx's macOS/iOS backend. shader_profile_dir() already maps RendererType::Metal -> "metal".
  list(APPEND _CLIENT_SHADER_PROFILES
    "metal|osx|metal|metal"
  )
endif()

function(client_compile_shaders TARGET)
  cmake_parse_arguments(ARG "" "VARYING" "SHADERS" ${ARGN})

  # Prebuilt-shaders path: no shaderc, just deploy the existing .bin next to the exe.
  if(CLIENT_PREBUILT_SHADERS)
    _client_shader_outroot(_outroot)
    set(_deps "")
    set(_copy_cmds "")
    foreach(_src IN LISTS ARG_SHADERS)
      get_filename_component(_name "${_src}" NAME_WE)
      foreach(_p IN LISTS _CLIENT_SHADER_PROFILES)
        string(REPLACE "|" ";" _parts "${_p}")
        list(GET _parts 0 _sub)
        set(_bin "${_outroot}/${_sub}/${_name}.bin")
        if(EXISTS "${_bin}")   # only copy profiles/names the prebuilt dir actually has
          list(APPEND _deps "${_bin}")
          list(APPEND _copy_cmds
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/shaders/${_sub}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_bin}" "$<TARGET_FILE_DIR:${TARGET}>/shaders/${_sub}/${_name}.bin")
        endif()
      endforeach()
    endforeach()
    list(GET ARG_SHADERS 0 _firstsrc)
    get_filename_component(_grp "${_firstsrc}" NAME_WE)
    add_custom_target(${TARGET}_shaders_${_grp} DEPENDS ${_deps})
    add_dependencies(${TARGET} ${TARGET}_shaders_${_grp})
    if(_copy_cmds)
      add_custom_command(TARGET ${TARGET}_shaders_${_grp} POST_BUILD ${_copy_cmds}
        COMMENT "Deploying prebuilt shaders next to ${TARGET}" VERBATIM)
    endif()
    return()
  endif()

  _client_find_shaderc(SHADERC)
  if(NOT SHADERC)
    if(ANDROID)
      # No shaderc AND no CLIENT_PREBUILT_SHADERS (that path returned above) -> the essl (GLES) shaders
      # can't be built, so the APK would render only a grey clear colour (S. 2026-08-06 hit exactly this
      # after the repo split staled the hostShaderc path). FAIL LOUDLY instead of shipping a dead APK.
      message(FATAL_ERROR
        "shaderc not found for the Android build -> the essl (GLES) shaders can't be compiled and the "
        "APK would show only a grey screen. Provide a HOST shaderc: set env HOST_SHADERC or gradle "
        "'hostShaderc' (android/gradle.properties) to a host-arch shaderc(.exe) from a desktop build "
        "(BornRok/build/win-msvc/vcpkg_installed/x64-windows-static/tools/bgfx/shaderc.exe), or pass "
        "-DCLIENT_PREBUILT_SHADERS=<dir with essl/*.bin>. See docs/android-build-km.md.")
    endif()
    message(WARNING "shaderc not found: skipping shader compilation for '${TARGET}'. "
                    "The client will run but render only a clear color until shaders are built.")
    return()
  endif()

  # Compile into a fixed configure-time path (a generator expression like
  # $<TARGET_FILE_DIR:..> is NOT allowed in add_custom_command OUTPUT). The
  # results are copied next to the executable in a POST_BUILD step below.
  set(_outroot "${CMAKE_CURRENT_BINARY_DIR}/_compiled_shaders")
  set(_all_outputs "")
  set(_copy_cmds "")   # per-file deploy commands (built in the loop) -- see the POST_BUILD note below

  foreach(_src IN LISTS ARG_SHADERS)
    get_filename_component(_name "${_src}" NAME_WE)        # e.g. vs_sprite
    string(SUBSTRING "${_name}" 0 2 _prefix)               # vs / fs
    if(_prefix STREQUAL "vs")
      set(_type vertex)
      set(_profile_idx 2)
    else()
      set(_type fragment)
      set(_profile_idx 3)
    endif()

    foreach(_p IN LISTS _CLIENT_SHADER_PROFILES)
      string(REPLACE "|" ";" _parts "${_p}")
      list(GET _parts 0 _sub)
      list(GET _parts 1 _plat)
      list(GET _parts ${_profile_idx} _prof)

      set(_out "${_outroot}/${_sub}/${_name}.bin")
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_outroot}/${_sub}"
        COMMAND "${SHADERC}"
                -f "${_src}" -o "${_out}"
                --type ${_type}
                --platform ${_plat}
                -p ${_prof}
                --varyingdef "${ARG_VARYING}"
                -i "${UARO_CLIENT_ROOT}/assets/shaders"
        DEPENDS "${_src}" "${ARG_VARYING}"
        COMMENT "shaderc ${_name} -> ${_sub}"
        VERBATIM)
      list(APPEND _all_outputs "${_out}")
      # Deploy THIS file only (not the whole dir) -- see the POST_BUILD note. Copying per-file means
      # parallel shader groups touch DISJOINT files, so there is no copy_directory collision under -j.
      list(APPEND _copy_cmds
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/shaders/${_sub}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_out}" "$<TARGET_FILE_DIR:${TARGET}>/shaders/${_sub}/${_name}.bin")
    endforeach()
  endforeach()

  # Unique target name (derived from the first shader) so this can be called for
  # several shader groups (sprite, ground, ...) on the same executable.
  list(GET ARG_SHADERS 0 _firstsrc)
  get_filename_component(_grp "${_firstsrc}" NAME_WE)
  add_custom_target(${TARGET}_shaders_${_grp} DEPENDS ${_all_outputs})
  add_dependencies(${TARGET} ${TARGET}_shaders_${_grp})

  # Deploy the compiled shaders next to the executable. CRITICAL: attach this to the SHADER target,
  # NOT the exe target. A POST_BUILD on the exe only fires when the exe RELINKS — so a shader-only
  # edit (no .cpp change) regenerates the .bin in _compiled_shaders but NEVER copies them next to the
  # exe, and the client keeps loading stale bytecode (this silently ate a whole day of "no difference"
  # shader tweaks). The shader custom target runs every build, so this copies the fresh .bin whenever
  # shaders change. $<TARGET_FILE_DIR:${TARGET}> still resolves the exe's dir from here.
  # PER-FILE copy (not copy_directory of the shared _outroot): several shader groups build in parallel
  # under -j and each one's POST_BUILD ran concurrently, so N copy_directory calls raced on the same
  # shaders/ dir -> "Error copying directory" (S., Debian -j build). Copying only this group's own
  # .bin files makes the parallel copies touch disjoint paths.
  add_custom_command(TARGET ${TARGET}_shaders_${_grp} POST_BUILD
    ${_copy_cmds}
    COMMENT "Deploying compiled shaders next to ${TARGET}"
    VERBATIM)
endfunction()

# Bake the compiled sprite shaders (all profiles) into the executable so the patcher UI renders
# from a bare exe with no external shaders/ folder. Must be called AFTER client_compile_shaders()
# for the sprite group on the same target. No-op if shaderc is unavailable (the checked-in stub
# EmbeddedShaders.cpp keeps the on-disk path). The generated TU registers itself additively.
function(client_embed_sprite_shaders TARGET)
  # Optional 2nd arg: the target whose client_compile_shaders() produced the sprite .bin (defaults
  # to TARGET). Lets a second exe (e.g. the AVX2 build) reuse the primary's compiled shaders.
  set(_shtgt "${TARGET}")
  if(ARGC GREATER 1)
    set(_shtgt "${ARGV1}")
  endif()
  # bin2c only reads .bin, so prebuilt shaders can be embedded with no shaderc.
  if(NOT CLIENT_PREBUILT_SHADERS)
    _client_find_shaderc(SHADERC)
    if(NOT SHADERC)
      return()
    endif()
  endif()
  _client_shader_outroot(_outroot)
  set(_gen "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedSpriteShaders_${TARGET}.cpp")

  # The .bin the generator reads (only existing profiles are baked; the script skips the rest).
  set(_deps "")
  foreach(_p IN LISTS _CLIENT_SHADER_PROFILES)
    string(REPLACE "|" ";" _parts "${_p}")
    list(GET _parts 0 _sub)
    # In prebuilt mode these are plain files (no generating rule) -- only depend on ones that exist,
    # else the custom command errors on a missing dependency.
    foreach(_b vs_sprite fs_sprite)
      if(NOT CLIENT_PREBUILT_SHADERS OR EXISTS "${_outroot}/${_sub}/${_b}.bin")
        list(APPEND _deps "${_outroot}/${_sub}/${_b}.bin")
      endif()
    endforeach()
  endforeach()

  add_custom_command(
    OUTPUT "${_gen}"
    COMMAND ${CMAKE_COMMAND}
            -DINROOT=${_outroot}
            -DOUT=${_gen}
            "-DPROFILES=glsl;spirv;dx11;metal;essl"
            "-DNAMES=vs_sprite;fs_sprite"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/bin2c_shaders.cmake"
    DEPENDS ${_deps} ${_shtgt}_shaders_vs_sprite
    COMMENT "Embedding sprite shaders into ${TARGET}"
    VERBATIM)
  target_sources(${TARGET} PRIVATE "${_gen}")
endfunction()

# Bake an ARBITRARY set of compiled shaders into the exe (S.: "в ехе зашей все шейдеры"), so the whole
# client renders from a single file with no external shaders/ folder. Coexists with the sprite embed
# above: bin2c gives each generated TU internal-linkage symbols (static + anon namespace), so several
# generated units register into the same runtime map without collision. Pass the shader base names to
# embed as extra arguments (the sprite pair is already covered, so pass the rest).
#   client_embed_shaders(<target> <name1> <name2> ...)
function(client_embed_shaders TARGET)
  if(NOT CLIENT_PREBUILT_SHADERS)
    _client_find_shaderc(SHADERC)
    if(NOT SHADERC)
      return()
    endif()
  endif()
  set(_names ${ARGN})
  if(NOT _names)
    return()
  endif()
  _client_shader_outroot(_outroot)
  set(_gen "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedShadersAll_${TARGET}.cpp")
  set(_deps "")
  foreach(_p IN LISTS _CLIENT_SHADER_PROFILES)
    string(REPLACE "|" ";" _parts "${_p}")
    list(GET _parts 0 _sub)
    foreach(_n IN LISTS _names)
      # Prebuilt .bin have no generating rule -- only depend on ones that exist.
      if(NOT CLIENT_PREBUILT_SHADERS OR EXISTS "${_outroot}/${_sub}/${_n}.bin")
        list(APPEND _deps "${_outroot}/${_sub}/${_n}.bin")
      endif()
    endforeach()
  endforeach()
  string(REPLACE ";" ";" _namestr "${_names}")
  add_custom_command(
    OUTPUT "${_gen}"
    COMMAND ${CMAKE_COMMAND}
            -DINROOT=${_outroot}
            -DOUT=${_gen}
            "-DPROFILES=glsl;spirv;dx11;metal;essl"
            "-DNAMES=${_namestr}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/bin2c_shaders.cmake"
    DEPENDS ${_deps}
    COMMENT "Embedding all shaders into ${TARGET}"
    VERBATIM)
  target_sources(${TARGET} PRIVATE "${_gen}")
endfunction()
