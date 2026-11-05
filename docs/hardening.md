# Parser hardening

The client parses untrusted bytes from the network (session, login, world and zone packets) and from game files
(PFS/S3D/EQG archives, WLD, TER/MOD/ZON, heightmap DAT, DDS/BMP/TGA, XMI, sound and effect tables, ini). This page
lists what is checked and what was found. Every input is synthetic.

## Sanitizer build

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNMSC_SANITIZE=ON
cmake --build build-asan --target nmsc_tests
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure
```

`NMSC_SANITIZE` adds `-fsanitize=address,undefined -fno-sanitize-recover=undefined` to every target (GCC or Clang), so
a finding aborts the test with a stack trace. The whole existing suite (561 test cases, 44 skipped for missing
`EQ_GAME_DIR` / vector files) ran clean under it before any fix; the findings below all came from the new fuzz tests.

## Fuzz-style tests (always on, a few seconds)

`tests/fuzz_net_test.cpp`, `tests/fuzz_assets_test.cpp`, helpers in `tests/fuzz_support.h`. Inputs are deterministic
(SplitMix64 with fixed seeds): known-good synthetic fixtures cut at every length up to 700 bytes (sparser beyond),
then seeded mutations (bit flips, boundary bytes, 32-bit fields overwritten with `0xffffffff`/`0x80000000`/small
counts, deleted/duplicated runs, appended tails), then random data behind a valid magic.
`tests/fuzz_alloc_guard.cpp` replaces `operator new` so a single request over 64 MB fails the test (a parser
trusting a bogus length). A call over 2 s also fails.

| Area | What is run | Must |
|---|---|---|
| 224 typed decoders `decode_*(const AppPacket&)` (`tests/fuzz_decoders.inc`; a test checks the list covers every declaration in `src/net/*.h`) | 5 patterns (zeros, 0xff, counting, random, text) of 24 KB cut at ~900 lengths each, 300 mutations, and an opcode sweep for the decoders that look at the opcode | never throw, never over-allocate |
| Session codec | `decode_datagram` for all pass/CRC combinations (also mutated plain packets re-encoded with a valid CRC), `decompress_body` (including a zlib bomb), `split_combined`/`split_app_combined`, `decode_app`, `parse_session_response`, `InboundStream` (fragments, out-of-order, wraparound, 20 000 random packets) | never throw |
| Live `Session` | a real session over loopback UDP receiving 3 000 corrupt datagrams | keep running |
| Login replies | `parse_handshake_reply`, `parse_login_reply`, `parse_server_list`, `parse_play_reply`, `reply_body`, with compressed/bomb seeds | error by exception only (the caller catches `std::exception`) |
| Assets | PFS (multi-block), WLD (mesh fixtures plus every fragment type 0x03-0x37, `build_mesh`, `build_skeleton_pose`), EQG TER/MOD, ZON, heightmap ZON/DAT, ECO, TOG, water, EQGS/EQGM meshes, animations, DDS/BMP (all depths, RLE)/TGA, XMI, `_sounds.eff`, spellsnew, EDD, sound-bank/asset/emt/race text, ini, zone-line tags | no crash/hang/huge allocation; errors by `std::exception` |

Optional libFuzzer targets (`-DNMSC_LIBFUZZER=ON`, Clang): `fuzz_pfs`, `fuzz_wld`, `fuzz_texture`, `fuzz_soe`, `fuzz_decoders` (every typed decoder of `tests/fuzz_decoders.inc`, byte 0 choosing it),
`fuzz_packets` from `tests/fuzz_libfuzzer.cpp`. These were only syntax-checked here (no Clang build of the
dependencies was run); the deterministic tests are the verified part.

## Findings and fixes

| Where | Problem | Fix |
|---|---|---|
| `sdk/nmsc_byte_io.h` (`core/byte_io.h`) `ByteReader::strnlen_bounded` | `memchr(nullptr, 0, 0)` on an empty buffer: UB (null pointer passed to a nonnull parameter), hit by `fixed_string`/`cstring` on empty input | return 0 for a zero length |
| `src/assets/pfs.cpp` `inflate_entry` | the directory's file size was used for `std::vector(size)` before any check: a 12-byte directory entry could request 4 GB | reject a size above 1032 x the archive size (zlib's maximum expansion) |
| `src/assets/texture.cpp` `decode_bmp` | `biClrUsed` larger than the bit depth can index (e.g. 256 colours at 1 bit) wrote past the palette array: heap-buffer-overflow | clamp the entry count to `1 << bits` |
| `src/assets/eqg.cpp` `parse_heightmap` | tile longitude/latitude `i32 - 100000` overflowed a signed int for values near `INT_MIN` | subtract as `uint32_t` and wrap |
| `src/assets/eqg.cpp` `HeightmapTile::height_at` | a non-finite object position (corrupt record) reached float-to-unsigned casts | treat non-finite coordinates as 0 |

Behaviour for valid input is unchanged. The typed packet decoders, the session layer, the login parsers and the WLD, ZON,
model, DDS, TGA, XMI and table parsers produced no findings.

## Not covered

- Real game files: the synthetic fixtures reach the parsers' normal paths but cannot reproduce every real-world quirk.
- Packet decoders get structure only from the patterns above; deep fields behind several valid prefixes (e.g. the
  later parts of the player profile) are reached only with all-zero or counting data.
- `ReliableChannel` (timing logic, no parser) and the game-connection state machines beyond the `Session`.
- Windows/macOS: the sanitizer build was run on Linux with GCC only.
