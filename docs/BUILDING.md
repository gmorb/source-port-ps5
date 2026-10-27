# Building Source Port from source

The build runs on an x86-64 Linux host (developed on Ubuntu 24.04). It
cross-compiles everything for the PS5 with the ps5-payload-dev toolchain
(clang 18) and links it into a native folder app.

> These steps reproduce the alpha build. Expect to spend some time on the first
> setup: Mesa, SDL2 and the engine are large builds.

## 1. Host packages

```bash
sudo apt install -y git python3 python3-pip cmake ninja-build meson pkg-config \
    clang-18 lld-18 llvm-18 libclang-rt-18-dev ccache flex bison \
    glslang-tools spirv-tools wget unzip zip
```

## 2. Upstream sources (pinned revisions)

Put every checkout side by side in one folder, `src/`, next to this
repository. `ps5/env.sh` expects `../src` by default; set `SRC=/path` to
override.

```
work/
├── source-port-ps5/            ← this repository
└── src/
    ├── source-engine/
    ├── ps5-opengl/
    ├── ps5-SDL/
    ├── ps5-native-app-boilerplate/
    └── ps5-sdk/
```

| Folder | Repository | Revision |
| --- | --- | --- |
| `source-engine` | https://github.com/nillerusr/source-engine (with submodules) | `ed8209cc35c61fbd8ddff8480962a01c981eef2f` |
| `ps5-opengl` | https://github.com/Marice/ps5-opengl (mirror of BlackBearReloaded/ps5-opengl) | `00ea837b6954290275dc06db0fbd852b008a49f5` |
| `ps5-SDL` | https://github.com/ps5-payload-dev/SDL (branch `release-2.30.x-ps5`) | `8c56053f13ca13a0c050de613706ff69eb615836` (used by ps5-opengl's SDL bridge) |
| `ps5-native-app-boilerplate` | https://github.com/mpereiraesaa/ps5-native-app-boilerplate | `38c9175869f101c8ab7f2c14195c69c5e581e905` |
| `ps5-sdk` | https://github.com/ps5-payload-dev/sdk | `a7685ce8e97ce1f8a4f38253a41d54241c9124f1` (reference; the boilerplate installs SDK v0.42) |

```bash
mkdir -p ../src && cd ../src
git clone https://github.com/nillerusr/source-engine.git
git -C source-engine checkout ed8209cc35c61fbd8ddff8480962a01c981eef2f
git -C source-engine submodule update --init --depth 1
git clone https://github.com/Marice/ps5-opengl.git
git -C ps5-opengl checkout 00ea837b6954290275dc06db0fbd852b008a49f5
git clone https://github.com/ps5-payload-dev/SDL.git ps5-SDL
git clone https://github.com/mpereiraesaa/ps5-native-app-boilerplate.git
git -C ps5-native-app-boilerplate checkout 38c9175869f101c8ab7f2c14195c69c5e581e905
```

## 3. Apply the patches

```bash
cd ../src
git -C source-engine apply ../source-port-ps5/patches/source-engine-ps5.patch
git -C ps5-opengl    apply ../source-port-ps5/patches/ps5-opengl.patch
# Only if archive.mesa3d.org is unreachable from your host (Mesa then comes from
# the PS5_Mesa git history instead of the release tarball):
# git -C ps5-opengl apply ../source-port-ps5/patches/ps5-opengl-mesa-from-git.patch
```

| Patch | What it changes |
| --- | --- |
| `source-engine-ps5.patch` | `waf --ps5` target; PS5 fixes in the launcher, filesystem, sockets, fonts, joystick, SDL audio and window; togl tweaks (no persistent buffers, draw hooks) |
| `ps5-opengl.patch` | GPU blits for presents, refraction and screen-effect copies (they were CPU); audio enabled in the SDL bridge |
| `ps5-opengl-mesa-from-git.patch` | Optional: fetch Mesa 26.2.0 from git instead of archive.mesa3d.org |
| `ps5-opengl-local.patch` | Everything in `ps5-opengl.patch` plus the local override used for the release build: `toolchain/apply-mesa-ps5-patch.sh` accepts Mesa 26.2.0 exported from git (patch pre-applied) when archive.mesa3d.org is unreachable |

## 4. Toolchain and dependencies

The boilerplate's setup script downloads the ps5-payload-dev SDK (v0.42),
PacBrew v0.40.2 (freetype, zlib, png, OpenAL, …) and its runtime pieces:

```bash
cd ps5-native-app-boilerplate
bash tools/setup-native-dependencies.sh
chmod +x tooling/prospero-clang18
cd ..
```

## 5. ps5-opengl SDK (OpenGL 4.6) and SDL2 bridge

```bash
cd ps5-opengl
source ../../source-port-ps5/ps5/env.sh && unset CC CXX AR
export PS5_DYNAMIC_SCANOUT=1 PS5_SCANOUT_HEIGHT=2160 PS5_SCANOUT_FPS=120
make sdk-gl46                                    # first time: builds Mesa (long)
bash toolchain/install-ps5-opengl-gl46.sh build/sdk/ps5-opengl-gl46
python3 integration/SDL2/build.py native --sdl-source "$SRC/ps5-SDL" \
    --sdk-prefix "$PS5_OPENGL_PREFIX" --out build/sdl-native \
    --payload-sdk "$PS5_PAYLOAD_SDK" \
    --compiler-wrapper "$PS5_NATIVE_APP_TEMPLATE/tooling/prospero-clang18"
cd ..
```

## 6. Build the app

```bash
cd ../source-port-ps5
ps5/build-all.sh
```

Steps (`ps5/build-all.sh`):

1. `ps5/build-engine.sh`: `waf configure --ps5 --build-games=hl2` and build.
2. `ps5/collect-links.py`: records each module's link line.
3. `ps5/staticlink.py`: turns every module into one relocatable object,
   localizes its symbols and writes the static `dlopen`/`dlsym` table.
4. Compiles `ps5/runtime/` and links everything into one object.
5. `ps5/package-app.sh`: links `eboot.bin` with the ps5-opengl runtime and the
   SDL bridge, adds `assets/sce_sys` (icon, backgrounds, music and
   `param.json`) and writes the app folder.

Output: `build-app/PPSA20040/dist/PPSA20040/`. Zip that folder (with the
folder itself at the top of the archive) for a release.

Faster rebuilds after the first one:

```bash
SKIP_COLLECT=1 ps5/build-all.sh               # engine changed
SKIP_ENGINE=1 SKIP_COLLECT=1 ps5/build-all.sh # only runtime, assets or driver changed
```

## Title metadata

`assets/sce_sys/param.json` is the single source of truth for the title ID
(`PPSA20040`), name and `contentVersion`. **Raise `contentVersion`
(`NN.NNN.NNN`) in every release.** Consoles and the homebrew catalog use it to
detect updates.

## Home-screen assets

- `assets/sce_sys/icon0.png` (512×512 PNG), `pic0.dds` and `pic1.dds`
  (3840×2160 BC7 DDS) are generated by `assets/art-src/make_art.py`. The DDS
  files are compressed with [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo).
- `assets/sce_sys/snd0.at9` is the selection music. The `assets/art-src/music*.py`
  scripts are alternative seamless loops (`music.py` is the released one,
  `make_music.py` is the original); each renders a 48 kHz WAV.
- Encoding is done by `tools/at9enc/at9enc.py`, a self-contained ATRAC9 encoder
  for the PS4/PS5 `snd0.at9` profile (48 kHz stereo, 192 kb/s), after FFmpeg
  loudness normalization to -28 LUFS. Sony's `ps4_at9tool`
  (`-e -br 192 -wholeloop`) produces the same profile if you have it.
  This is the template's `prepare-assets` recipe. Check the result with the
  template's `tools/validate-assets.sh`.

## Debugging

- Logs go to the kernel log (klogsrv, port 3232) and to `zmr.log` in the app's
  data (`/download0/zmr`).
- A crash handler logs the faulting address. A watchdog dumps the main
  thread's stack if no frame is presented for 10 s.
- `-ps5_prof` in `game/args.txt` enables a sampling profiler. Resolve its
  addresses with `llvm-symbolizer-18 --obj=build-app/PPSA20040/build/llvm-pie.elf`,
  using the address minus `0x400000`.
- `-ps5_gltrace` traces draw calls; `-ps5_nogpublit` and `-ps5_nogpupresent`
  turn off the GPU copy paths for comparison.
