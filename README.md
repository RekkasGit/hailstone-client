<p align="center">
  <img src="docs/images/logo.jpg" alt="Project Hailstone" width="560">
</p>

# Hailstone Client

A portable, open-source client for EQEmu servers, written in C++20 with Vulkan 1.3. It speaks the RoF2 protocol and runs on Linux, Windows and macOS (via MoltenVK).

> **Beta.** Expect rough edges and missing features. Bug reports are welcome.

Hailstone is an educational project: a study of how a game client of its era works (its network protocol, file formats, rendering and game rules), carried out by writing one from scratch.

Hailstone is an independent project. It contains no code or assets from proprietary software. You supply your own RoF2-compatible game data folder, which Hailstone only reads and never modifies or redistributes.

## What works

- Server list, login, character create/delete, world and zone entry
- Zone and character rendering, with an optional HD texture pack tool
- Movement, targeting, combat, spells, buffs, pets and groups
- Inventory, loot, merchants, trade, barter and the guild bank
- Chat, hot bar, skills and abilities
- Zone sound and music
- Lua UI packs (`modern`, `gamepad`) with full gamepad support
- MacroQuest-style Lua scripting and compiled plugins ([docs/plugins.md](docs/plugins.md))
- Optional server-side support: client file delivery, cross-zone guild invites ([server/README.md](server/README.md))

Work in progress and known gaps are tracked in [docs/plan.md](docs/plan.md) and [docs/unknowns.md](docs/unknowns.md).

## System requirements

| | Minimum |
|---|---|
| **Operating system** | Linux x64: Ubuntu 24.04 or a distribution of the same age or newer (the release is built against its glibc). Windows x64: Windows 10 version 1903 or later. macOS and arm64: build from source (no release packages yet). |
| **Graphics** | A GPU and driver with Vulkan 1.3, dynamic rendering, synchronization2 and descriptor indexing (runtime descriptor arrays, non-uniform sampled image indexing, partially bound and variable-count bindings). On macOS, MoltenVK. |
| **Game data** | Your own RoF2-compatible game folder. Hailstone reads it and never writes to it. |
| **Server** | An EQEmu server that accepts RoF2 clients. |
| **Network** | UDP to the login and world servers. Automatic updates need HTTPS: on Linux the system's libcurl. |
| **Input** | Keyboard and mouse, or a gamepad. |

To check a GPU, run `vulkaninfo --summary` and look for `apiVersion` 1.3 or higher, or look the GPU up on [vulkan.gpuinfo.org](https://vulkan.gpuinfo.org). If no GPU qualifies, Hailstone says so in a dialog that names each GPU and what it lacks; `hailstone/logs/hailstone.log` has the same. Textures in BC formats are used directly where the GPU supports them and decoded on the CPU otherwise, so GPUs without BC support still run.

Memory, VRAM and CPU minimums have not been measured yet.

## Quick start

```sh
git clone https://github.com/gennro/hailstone-client.git
cd hailstone-client
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
build/hailstone/hailstone
```

On first start Hailstone asks for your game data folder and remembers it. You can also pass `--game-dir PATH` or set `EQ_GAME_DIR`.

Hailstone is portable: settings, per-server files, characters and logs live in the `hailstone/` folder next to the binary ([docs/layout.md](docs/layout.md)).

### Build requirements

- CMake 3.25+
- GCC 13+, Clang 17+ or MSVC 2022
- `glslc` (shaderc) or `glslangValidator`
- A Vulkan 1.3 driver (MoltenVK on macOS)

Dependencies are fetched automatically unless the system already provides them. Run tests with `ctest --test-dir build` and see all options with `--help`.

## Documentation

| | |
|---|---|
| [docs/plan.md](docs/plan.md) | Roadmap and milestones |
| [docs/layout.md](docs/layout.md) | Folder layout and configuration |
| [docs/dev-ui.md](docs/dev-ui.md) | Writing UI packs |
| [docs/gamepad.md](docs/gamepad.md) | Gamepad support |
| [docs/plugins.md](docs/plugins.md) | Lua scripts and plugins |
| [docs/protocol/](docs/protocol/) | Network protocol notes |
| [docs/formats/](docs/formats/) | File format notes |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), and [AGENTS.md](AGENTS.md) for AI coding tools. Security issues: [SECURITY.md](SECURITY.md). Changes: [CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE). Bundled and fetched third-party code keeps its own licenses, listed in [THIRD_PARTY.md](THIRD_PARTY.md).

## Credits

Hailstone stands on the work of many people and projects:

- [EQEmu](https://github.com/EQEmu/Server): the server this client targets, and the protocol reference for RoF2 packets and sessions
- [OpenEQ](https://github.com/daeken/OpenEQ) (daeken): research notes on asset formats, rendering and audio
- [LanternExtractor](https://github.com/LanternEQ/LanternExtractor) and [libeq](https://github.com/cjab/libeq): file format specifications
- [EQEmu documentation](https://docs.eqemu.dev) and [zone-utilities](https://github.com/EQEmu/zone-utilities): format and behaviour references
- [MacroQuest](https://www.macroquest.org): the scripting API that Hailstone's Lua layer is compatible with
- [eqmac](https://github.com/kevrgithub/eqmac) (kevrgithub): symbol research
- [Dear ImGui](https://github.com/ocornut/imgui), [SDL3](https://libsdl.org), [Lua](https://www.lua.org), [volk](https://github.com/zeux/volk), [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [glm](https://github.com/g-truc/glm), [zlib](https://zlib.net), [doctest](https://github.com/doctest/doctest), [TinySoundFont](https://github.com/schellingb/TinySoundFont), [dr_libs](https://github.com/mackron/dr_libs), [stb](https://github.com/nothings/stb), [bcdec](https://github.com/iOrange/bcdec), [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo)
- [PBRify Remix](https://github.com/Kim2091/PBRify_Remix) (Kim2091): models for the optional HD texture tool
