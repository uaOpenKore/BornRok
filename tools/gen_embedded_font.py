#!/usr/bin/env python3
# Regenerate src/ui/EmbeddedFont.cpp from the shipped TTF so the client can bake the
# font with no external asset. Run from the Client/ directory:  python3 tools/gen_embedded_font.py
import sys
SRC = "assets/fonts/DejaVuSansMono.ttf"
OUT = "src/ui/EmbeddedFont.cpp"
data = open(SRC, "rb").read()
with open(OUT, "w") as f:
    f.write("// Auto-generated from assets/fonts/DejaVuSansMono.ttf - do not edit by hand.\n")
    f.write("// Regenerate with tools/gen_embedded_font.py. The client bakes this face when no\n")
    f.write("// on-disk font is present, so a single exe renders text with no external assets.\n")
    f.write("#include <cstddef>\n\nnamespace uaro {\n\n")
    f.write("extern const unsigned char kEmbeddedFontTtf[] = {\n")
    for i in range(0, len(data), 20):
        f.write("    " + ",".join(str(b) for b in data[i:i+20]) + ",\n")
    f.write("};\n")
    f.write("extern const std::size_t kEmbeddedFontTtfSize = sizeof(kEmbeddedFontTtf);\n\n")
    f.write("}  // namespace uaro\n")
print("wrote", OUT, len(data), "bytes")
