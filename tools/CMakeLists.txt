# Command-line tools built on the client libraries.
# grfinfo: inspect/extract GRF archives via the resource layer. Needs zlib.
add_executable(grfinfo grfinfo.cpp)
target_link_libraries(grfinfo PRIVATE uaro::resource ZLIB::ZLIB)

# grflub: verify the data-from-GRF lub layer (GrfData) against a real GRF end-to-end.
add_executable(grflub grflub.cpp)
target_link_libraries(grflub PRIVATE uaro::resource ZLIB::ZLIB)

# formatcheck: parse every SPR/ACT in a GRF through the format parsers.
add_executable(formatcheck formatcheck.cpp)
target_link_libraries(formatcheck PRIVATE uaro::resource uaro::formats uaro::world ZLIB::ZLIB)

# maptest: load a full map (RSW+GND+textures+models) end-to-end from a GRF.
add_executable(maptest maptest.cpp)
target_link_libraries(maptest PRIVATE uaro::world uaro::resource ZLIB::ZLIB)

# strdump: list a .str effect's layer textures (which .bmp/.tga to swap for HD .png).
add_executable(strdump strdump.cpp)
target_link_libraries(strdump PRIVATE uaro::formats uaro::resource ZLIB::ZLIB)

# sprdump: unpack a .spr's frames to PNG (for HD upscaling -> #109 png.d override).
add_executable(sprdump sprdump.cpp)
target_link_libraries(sprdump PRIVATE uaro::formats uaro::resource ZLIB::ZLIB)

# sprbatch (batch .spr -> .png.d unpacker) removed from the build — no longer needed (S.). The
# source sprbatch.cpp is kept in the tree for reference but is not compiled or copied next to the exe.
