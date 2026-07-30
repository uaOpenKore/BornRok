# Dependency-free unit tests for core/. Built with a tiny in-tree harness
# (tests/microtest.hpp) so they run with nothing but a C++20 compiler.
add_executable(client_core_tests
  test_main.cpp
  test_io.cpp
  test_math.cpp
  test_sha512.cpp
  test_patcher.cpp
  test_ini.cpp
  test_spr.cpp
  test_act.cpp
  test_pngsprite.cpp
  test_gat.cpp
  test_gnd.cpp
  test_rsw.cpp
  test_rsm.cpp
  test_bmp.cpp
  test_tga.cpp
  test_imageio.cpp
  test_gr2.cpp
  test_unityfs.cpp
  test_particles.cpp
  test_groundmesh.cpp
  test_modelmesh.cpp
  test_pathfind.cpp
  test_protocol.cpp
  test_jobnames.cpp
  test_skillnames.cpp
  test_skillcastdelay.cpp
  test_skilldesc.cpp
  test_grfalias.cpp
  test_mp3nametable.cpp
  test_clientinfo.cpp
  test_socket.cpp
  test_thread.cpp
)
target_include_directories(client_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_core_tests PRIVATE uaro::core uaro::formats uaro::world uaro::net uaro::patcher)

# Resource-layer tests (GRF/VFS) build a synthetic GRF with zlib, so they need
# both the resource library and zlib. Added only when resource/ was built.
if(TARGET uaro::resource)
  target_sources(client_core_tests PRIVATE test_grf.cpp test_vfs.cpp test_zip.cpp test_itemdb.cpp test_contentroute.cpp)
  target_link_libraries(client_core_tests PRIVATE uaro::resource ZLIB::ZLIB)
endif()

add_test(NAME client_core_tests COMMAND client_core_tests)
