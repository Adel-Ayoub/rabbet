<div align="center">

# Rabbet

**A from-scratch C++ game engine**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/)
[![OpenGL](https://img.shields.io/badge/OpenGL-4.1-5586A4.svg)](https://www.opengl.org/)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)](#platform-support)

</div>

---

## Screenshot

<p align="center">
  <img src="docs/editor.png" alt="The Rabbet 'forge' editor — hierarchy, viewport, inspector, console and assets panels around a render-to-texture scene">
</p>

## Features

- **Core & ECS** — a sparse-set ECS (versioned entity handles, dense component pools, multi-component queries) over a modular runtime of systems, resources and modules, with an `Always` / `Play` system scheduler and play-session edges so gameplay is gated behind Play/Stop.
- **Rendering** — OpenGL 4.1 forward renderer, metallic-roughness PBR (Cook–Torrance) and Phong, directional shadow maps with PCF, directional & point lights, a cubemap skybox with a procedural sky fallback, and RAII GL wrappers.
- **Assets** — stable UUID asset handles, a generational-handle `AssetManager` with an on-disk asset database and `.import` sidecars, assimp model import (glTF / OBJ / FBX, base-color), and stb image loading.
- **Serialization** — one component reflection registry powering human-readable JSON scene save/load with id-remapped round-trips, plus the editor's add/remove-component menu.
- **Editor (forge)** — a dockable ImGui workspace (Hierarchy · Inspector · Console · Assets · render-to-texture Viewport), a free-look camera, ID-buffer click-picking, ImGuizmo translate/rotate/scale with snapping, and Play / Pause / Step / Stop with snapshot-restore.
- **Scripting** — Lua components (lua + sol2) with `Transform` / `Input` / frame-time bindings, inspector-exposed script fields, and live file hot-reload.
- **Physics** — Jolt rigidbodies (static / dynamic / kinematic) with box & sphere colliders, fixed-timestep play-mode stepping with transform write-back, and a collider debug-draw pass.
- **Audio** — miniaudio sound emitters with volume / pitch / loop and spatial attenuation & pan from the active camera listener; wav / mp3 / flac decode natively, ogg via stb_vorbis.
- **Quality** — 34 headless CTest suites and strict `-Werror` builds in both Debug and Release.

**In progress / planned:** materials & shaders as first-class assets&nbsp;·&nbsp;prefabs&nbsp;·&nbsp;extended lighting (spot / image-based)&nbsp;·&nbsp;a particle system&nbsp;·&nbsp;an HDR post-processing stack (bloom / tonemap / AA)&nbsp;·&nbsp;terrain&nbsp;·&nbsp;a deeper asset browser.

## Quick start

```sh
git clone https://github.com/Adel-Ayoub/Rabbet.git
cd Rabbet

cmake -S . -B build          # configures and fetches dependencies
cmake --build build -j       # builds the engine, editor, examples and tests
./build/bin/forge            # launch the editor
```

Requires **CMake 3.24+** and a **C++20** compiler. Dependencies are fetched automatically (or reused if already installed); a handful of small single-header libraries are vendored in-tree under `third_party/`.

## Build

| Command                  | Description                              |
| ------------------------ | ---------------------------------------- |
| `cmake -S . -B build`    | Configure (fetches dependencies)         |
| `cmake --build build -j` | Build engine, editor, examples and tests |
| `./build/bin/forge`      | Launch the editor                        |
| `ctest --test-dir build` | Run the test suite                       |

Options: `-DCMAKE_BUILD_TYPE=Release`, `-DRABBET_BUILD_EDITOR=OFF`, `-DRABBET_BUILD_TESTS=OFF`.

## Platform support

| Platform | Support   |
| -------- | --------- |
| macOS    | Supported |
| Linux    | Supported |

## Acknowledgements

Built on excellent open-source libraries: [GLFW](https://www.glfw.org/), [GLM](https://github.com/g-truc/glm), [Dear ImGui](https://github.com/ocornut/imgui) + [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo), [assimp](https://github.com/assimp/assimp), [nlohmann/json](https://github.com/nlohmann/json), [Lua](https://www.lua.org/) + [sol2](https://github.com/ThePhD/sol2), [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [miniaudio](https://github.com/mackron/miniaudio), [stb](https://github.com/nothings/stb) and [glad](https://github.com/Dav1dde/glad). The sample crate model is from [Poly Haven](https://polyhaven.com/) (CC0).

## License

Apache License 2.0 — Copyright (c) 2026 Adel-Ayoub. See [LICENSE](LICENSE).
