# Third-party components

The released `eboot.bin` links the following projects. Their licenses apply to
their parts of the binary. This repository contains only build scripts,
patches, a small runtime layer and artwork.

| Component | Source | License |
| --- | --- | --- |
| Source engine (community fork) | https://github.com/nillerusr/source-engine | No open-source license from Valve; community-maintained code derived from Valve's Source engine |
| ps5-opengl (OpenGL 4.6 for PS5) | https://github.com/Marice/ps5-opengl | GPL-3.0-or-later (with bundled third-party notices) |
| Mesa 3D (PS5 Gallium driver, via PS5_Mesa) | https://github.com/mihawk-99/PS5_Mesa | MIT and others (see Mesa's documentation) |
| SDL2 (PS5 fork) | https://github.com/ps5-payload-dev/SDL | zlib |
| ps5-native-app-boilerplate (runtime, packaging) | https://github.com/mpereiraesaa/ps5-native-app-boilerplate | GPL-3.0-or-later |
| ps5-payload-dev SDK (crt, libc glue) | https://github.com/ps5-payload-dev/sdk | GPL-3.0 |
| PacBrew libraries: freetype, libpng, zlib, bzip2, libjpeg, OpenAL Soft, expat, fontconfig, libiconv, curl, OpenSSL, libpsl, zstd | https://github.com/PacBrew | Their respective licenses (FTL/GPL-2, libpng, zlib, bzip2, IJG, LGPL-2.1, MIT, MIT, LGPL-2.1, curl, Apache-2.0, MIT, BSD) |
| LLVM libc++, libc++abi, libunwind, compiler-rt | https://llvm.org | Apache-2.0 WITH LLVM-exception |
| Dear ImGui fonts (DroidSans, Cousine) | bundled with ps5-opengl | Apache-2.0 |
| Barlow Condensed (logo font) | https://github.com/google/fonts/tree/main/ofl/barlowcondensed | SIL Open Font License 1.1 (`assets/art-src/fonts/OFL.txt`) |

The artwork (logo, backgrounds, icon) and the selection music were made for
this project and are released under the repository's GPL-3.0 license.
Half-Life 2 game content is not included and is not covered by any of the above.
