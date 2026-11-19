# Graphics

The client's mechanics follow the RoF2 client; its graphics do not (`docs/plan.md`, section 1). The classic renderer is
a DirectX 9 design from 2003: per-vertex lighting, a hard fog wall, no antialiasing, no shadows.

This client keeps the game's assets and world data (zones, models, the day clock, weather, the zone's fog values as
hints) and draws them with a modern renderer. The procedural sky's colour maps, the EQG material classes and the
multi-pass light details are not planned unless a feature needs them. The existing classic path stays as the `classic`
look.

## Shared GPU textures (experimental, Windows)

Clients on one machine can share the GPU memory of their immutable zone and actor textures instead of each
uploading a copy. Only the texture payloads are shared: each client keeps its own Vulkan device, renderer, UI and
window. Off unless a separately started broker is configured; not in the release package. Design and
limits: [Shared GPU memory](research/shared-gpu-memory.md).

- **The broker** (`hailstone_gpu_cache_broker`, `tools/gpu_lab/`) owns one copy of each texture on its own device and
  hands clients an opaque Win32 handle over a local named pipe (`platform/gpu_ipc.h`: current user and local peers
  only). A random endpoint and 256-bit token go in `HAILSTONE_GPU_IPC_ENDPOINT` and `HAILSTONE_GPU_IPC_TOKEN`, never
  in arguments or logs.
- **Same GPU:** clients and broker must use the same GPU model, driver and memory layout (checked in the handshake).
  `HAILSTONE_GPU_NAME` picks the GPU by its exact model name.
- **The client** (`render/gpu_shared_cache.h`) asks for a texture by its final content, after colour preparation and
  mip building (`render::prepare_immutable_texture_image`, so the broker's UNORM image never builds a colour chain),
  and imports the handle (`platform/gpu_external_memory.h`). A texture the broker declines (unsupported format, budget
  full, upload failure) is uploaded privately; sharing carries on.
- **Frames:** Vulkan lets one instance own an external image at a time. A frame that samples imports takes a
  machine-wide lease, acquires the imports from `VK_QUEUE_FAMILY_EXTERNAL`, submits, waits for its own fence and
  releases them. The barriers cover every live import, not only those sampled (the renderers bind whole texture
  arrays). Shared-texture GPU work is thus serialized across clients: memory is saved at a cost in frame overlap.
- **Failure never stops the game.** A bad request fails alone. A client that disconnects or crashes gives back its
  lease and references; the others carry on.
- **Leaving for good:** a client that loses the broker, waits over 5 seconds for the lease, does not see its frame
  finish within 2 seconds or gets an unusable reply stops sharing. It closes its connection, copies its imports into
  its own images before the next frame (they stay valid without the broker) and plays on privately
  (`render/gpu_share_link.h`).
- **Idle:** the broker closes a connection idle for 5 minutes; clients ping well within that.

#### Shared GPU textures: tools and trying it

Also only in the build tree:

- `hailstone_gpu_share_probe` reports each adapter's external memory capabilities (`--smoke --device-index N` checks
  buffer aliases on one device).
- `hailstone_gpu_asset_probe` renders a real zone's static assets in several offscreen views on one device, sharing on
  and off, and compares the pixels.

To try it, build `hailstone_gpu_cache_broker` and start it with `--serve` and the session variables. Start each client
with the same variables (`HAILSTONE_GPU_IPC_SERVER_PID` set to the broker's process id pins it). `--status` and
`--stop-if-idle` query and stop the broker.

Without the variables every texture is private. `HAILSTONE_GPU_BENCHMARK=1` logs the client's presented
frames and the time spent waiting for the lease and for shared frames' fences.

## Transient plugin geometry

The generic world geometry service (`sdk/hailstone_world_geometry.h`) accepts complete, transient batches of
colored triangles or one-pixel lines; thick lines, outlines and glyphs are authored triangles. A producer supplies
final world positions and compositing order: `render/WorldGeometryRenderer` never invents terrain heights.

It depth-tests against the current scene with reverse Z, blends premultiplied linear-light color without writing
depth, and writes the temporal reactive mask through the same scene attachments as the target ring. The per-frame
budget is at most 256 batches and 98,304 vertices. Invalid inputs and repeated owner/tag pairs cannot consume unbounded
validation work. Buffers belong to fenced frame slots.

#### Transient plugin geometry: scene and floor queries

A current scene frame is offered only when the app's connection, zone, instance and scene epoch match the loaded
collision scene. Connection retirement, zoning, skipped render attempts and renderer failure withdraw the service;
changed scenery transforms or animation time advance its collision revision.

Effect floor queries pick the highest upward-facing drawn opaque/masked surface at or below the caller's ceiling. The
producer bounds the ceiling to the desired layer; missing surfaces remain missing. These are visual surfaces, separate
from movement floor policy.

- **Included:** terrain, static props (passable parts included), current animated scenery, doors, placed scenery.
- **Excluded:** invisible collision, transparent materials, undersides, movement-only ladder slabs, NPC/player/pet
  meshes, dropped items, missiles, placement ghosts.
- **Visual mesh:** separate from movement's (which keeps its original mesh), with the renderer's selected smoothed
  terrain and replacement objects. Rebuilt with the scene; unavailable during a scene rebuild or failed upload.
- **Masked surfaces** use their mesh footprint: this CPU query does not sample texture alpha.
- **Animated surfaces** use the renderer's exact clock, independent of the fixed-step movement pose.

Actor anchors use the model transform prepared for this render attempt (displayed position, model origin, mount seat),
with a separate plausibility-checked floor sample when available. A hidden or clipped actor can have a prepared model
pose: this is not evidence of visibility.

The optional surface-patch query clips current upward scene faces against a producer triangle and proves coplanar
union coverage by polygon subtraction. On a mismatch it returns an interior witness and its highest source face.
Off-grid or thin props are not certified flat because point samples missed them. Coverage holes, incomplete
enumeration, numeric uncertainty and exhausted shared work never produce a planar certificate.

#### Transient plugin geometry: receipts

| Receipt | Means |
|---|---|
| recorded | All admitted vertices reached `vkCmdDraw` for the exact batch identity. |
| submitted | Also, the real frame reached a successful queue submission. |
| presented | `vkQueuePresentKHR` accepted that real frame (`VK_SUCCESS` or `VK_SUBOPTIMAL_KHR`); an optional generated frame cannot confer one. |

None proves visible pixels, lack of occlusion or physical display. `world_geometry_probe` checks actual recording,
frame submission/presentation and PNG readback for winding, alpha blending, depth occlusion, foreground geometry and
lines, without game assets.

## The frame

`render::VkContext` (`src/render/vk_context.h`):

1. `begin_frame_commands` starts the frame; the shadow cascades are drawn (`begin_shadow_cascade` ..
   `end_shadow_cascade`, depth only); `begin_scene` begins rendering into the scene target. Colour is RGBA16F,
   multisampled when `display.msaa` > 1. Depth has the same sample count: D32_SFLOAT, else a stencil format
   (D32_SFLOAT_S8_UINT or D24_UNORM_S8_UINT) whose barriers name the depth aspect alone (the device enables
   `separateDepthStencilLayouts`, which every Vulkan 1.2 driver has and the GPU check requires). Scene pipelines use
   `scene_format()` and `scene_samples()`. `begin_frame` is the two together, for callers without shadows.
2. `begin_overlay` ends the scene: the MSAA colour resolves into a single-sampled image and the depth's sample 0 into a
   depth image. It then draws:
   - the ambient occlusion (`shaders/ao.frag`, into an R8 image) when the frame gave `set_ao_view`;
   - the bloom in the same frames (`draw_bloom`): six half-size RGBA16F levels, the 13-tap `shaders/bloom_down.frag`
     down (Karis-weighted on the first level), then `shaders/bloom_up.frag`'s 3 x 3 tent added back up;
   - `shaders/post.frag` over the swapchain image: AO, bloom (at `display.bloom` x 8 %), exposure, tonemap, sRGB
     encode, grade, dither.

   Rendering stays open on the swapchain image (one sample, no depth) for the UI; `ui::ImGuiLayer::render` calls it.
3. `end_frame` presents (and calls `begin_overlay` if nothing did).

#### The frame: viewport rectangle

The 3D view can take a rectangle of the window (/viewport, `VkContext::set_scene_rect`). The scene targets are made at
its size (`FrameInfo::extent`, placed by `FrameInfo::offset`). The post pass draws into the rectangle (its push
constant's offset gives the scene texel under each pixel) and clears the rest to black. The UI still covers the whole
window; the app maps nameplates and mouse picking onto the rectangle (`ClientApp::view_area_`).

A move applies at once. A new size remakes the scene targets (a device wait, every target rebuilt), so it waits until
the size has held for 4 frames, or at most 30 frames after it first changed (`render::SceneResizeDebounce`,
render/swapchain_size.h). Dragging an edge does not stall every frame; the old rectangle is drawn meanwhile.

#### The frame: render scale and FSR 1

The scene can be drawn below the view's size and upscaled (`display.render_scale`, `SceneOptions::render_scale`,
33 .. 100 %, `render::kMinRenderScalePercent`). The scene targets are the view's size times the scale
(`render::scaled_extent`, each side rounded), and so is `FrameInfo::extent`: projection, AO, SSR and bloom all work at
it. The view's rectangle in the window (picking, nameplates) is unchanged.

Below 100 % `begin_overlay` runs AMD FidelityFX FSR 1 (`third_party/fsr1`, MIT; spatial, no motion vectors):

1. `post.frag` draws into a 10-bit image at the scene's size, tonemapped and sRGB-encoded as FSR 1 wants, without the
   dither.
2. `shaders/fsr_easu.frag` (EASU, edge-adaptive) brings it up to the view's size.
3. `shaders/fsr_rcas.frag` (RCAS) sharpens it by `display.sharpness` into the swapchain and adds the dither.

The UI is drawn after, at window size. Textures keep their LOD bias (none yet). With `display.upscaler = taa`
the temporal upscaler replaces FSR 1 (Temporal upscaling, below).

### Worker threads

The frame hands some loops to a worker pool (`core/worker_pool.h`): one fewer than the machine's hardware
threads, at most 6 (`core::default_worker_count`), or `display.worker_threads` (0: none, every loop on the main
thread).

`core::parallel_for` splits a loop into chunks that the workers and the main thread take in turn; without a pool (the
tests, the tools) or inside a worker it runs inline. `core::parallel_for_nested` splits from inside a worker too (a
background task, or a chunk of another loop). The particle loops use it, being mostly reached from the actor effects'
step or a recording lane.

#### Worker threads: characters and particles

- **Posing and skinning** (`render::ActorRenderer::prepare`): each drawn instance's slice of the frame's bones is given
  out on the main thread (culling and bone budget, in order); instances are posed and skinned on the workers, each
  into its slice (`ActorRenderer::pose_instances`); the motion history is kept on the main thread after.
- **Drawn bones** (`drawn_bone`: its effects, name plate, spells on it): a character's skeleton is posed for them at
  most once a frame (`ActorRenderer::drawn_bone`) and kept until the spawn is drawn again
  (`ActorRenderer::cached_bone_world`). Poses of spawns no longer drawn are dropped once a second, not each frame.
- **Particle quads** (`render::ParticleRenderer::draw`), modern: particles in view are gathered on the workers
  (counted per emitter, placed in the emitters' order, then written) and sorted on the calling thread; each has its
  own six vertices in the buffer.
- **Particle quads, classic:** the particles each emitter shows are counted on the workers, emitters' places are given
  out in order up to the buffer's size, then quads are built on the workers.
- A particle that turns out to show nothing after its place was given (a quad past its depth offset) is written as six
  vertices at one point, which draw no pixel.

#### Worker threads: actor effects' step

`app::ActorEmitters::simulate_async`:

1. Once the frame is recorded, each attached emitter's place is read from the poses as drawn (main thread).
2. Both particle systems step on a worker while the frame is submitted and the next begins
   (`game::ParticleSystem::update`). Particles' ends and motion spread over the other workers, emitters a few at a
   time. Attachments and emission stay on the stepping thread in emitter order, so a seed draws the same particles
   however the work is split. Every other call on them waits for the step.
3. The next frame draws the result from the poses the step used (drawn the frame before), the particles' own time one
   frame behind.

Item previews' emitters still step inline; the tests take both ways (the constructor's `async`).

#### Worker threads: scene recording

The scene's command recording (`render::SceneRecorder`, `display.parallel_record`) makes each draw between
`begin_scene` and the scene's end a job in a lane. A renderer's draws share its lane. The characters, spawn boxes,
target ring and isometric markers share one: the markers ask the character renderer's caches, and the spawn boxes take
the set the characters drew.

At `begin_translucent` and before the name plates the lanes are recorded at once:

- A lane's jobs run in order on one thread (`render::SceneLanes`), each into its own secondary command buffer
  (`VkCommandBufferInheritanceRenderingInfo` from `VkContext::scene_inheritance`; viewport and scissor from
  `set_scene_state`).
- The buffers execute in job order, so the draws and their order are unchanged. The scene's renderings then begin with
  `VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT` (`VkContext::set_scene_secondary`).
- Each frame slot keeps a command pool per lane, reset when the slot comes round.

Off (or without workers), jobs run at once into the frame's command buffer. A queued draw runs at the flush, so what
it reads must not change between its `add` and the flush: the frame sets the zone renderer's distant land
(`set_distant`) before queuing the first draw.

The shadow layers use a second `render::SceneRecorder`, made without `scene`, under the same `display.parallel_record`:

- Layers: the sun's cascades, then the cover and water maps when drawn. Each is its own target
  (`VkContext::shadow_inheritance`; viewport, scissor and depth bias from `set_shadow_state`).
- Each renderer (zone, characters, doors and objects) is a lane, so its layers record in order on one thread while the
  others record theirs (`render::SceneLanes` keeps each job's lane and target).
- Each layer's rendering begins with secondary contents (`begin_shadow_cascade`'s `secondary`) and executes its
  buffers in the inline order. The point-light cubes are still drawn inline.

#### Worker threads: movement and zone load

- **Other spawns' movement** (`game::WorldState::smooth`): vehicles' riders step first on the main thread (each reads
  its vehicle), then every other spawn on the workers, each wholly on one thread, so the result does not depend on
  which. Floor, region and contact probes only read the zone and its doors; the contact probe sweeps doors into a
  per-thread buffer (`ZoneMoveWorld::set_contact_probe_per_thread`).
- **Zone load** (`ClientApp::load_zone`): once the zone's data is final, its collision mesh, moving parts, ladders and
  ambient probe build as background tasks while the main thread makes the GPU objects; each is waited for where it was
  built before.

### Temporal upscaling

`display.upscaler = taa` (read at start-up, as `display.msaa`; the viewer's `--upscaler taa`) draws the scene jittered
and resolves it over the frames into a picture at the view's size: anti-aliasing at a render scale of 100, upscaling
below. It is also the groundwork for an outside temporal upscaler (FSR 3.1), proven on our own first (Roadmap 6).
`render/temporal.h` holds the maths, `VkContext::begin_overlay` the passes:

1. **One sample.** Scene targets are single-sampled whatever `display.msaa` says: the resolve smooths edges, and reads
   motion and depth per pixel. Alpha-tested cut-outs lose MSAA's alpha-to-coverage.
2. **Jitter.** Each frame the scene's projection moves by a sub-pixel offset (`VkContext::projection_jitter`,
   `render::jittered`): Halton (2, 3) from its second point, in a cycle of 8 x (view / scene)^2 frames (8 at a render
   scale of 100, 32 at 50; FSR 2's count), so every view pixel sees samples near it.
   - Only scene draws take it. Picking, name plates, the map overlay, the sun's place for the shafts and the culling
     of spawns to pose keep the plain projection.
   - The app gives the plain camera to `VkContext::set_temporal_view`. A frame that does not (no 3D scene) is drawn
     without the temporal path and drops the history.
3. **Prep** (`shaders/temporal_prep.frag`, scene size, two targets):
   - The scene with its AO applied. AO is lighting, so it goes in before the resolve, which smooths its pattern too;
     the post pass does not apply it again. The reactive mask (step 6) goes in the colour's alpha.
   - Each pixel's motion in UV to where it was a frame ago (RG16F). Where a draw wrote its own motion (step 6), that is
     taken. Elsewhere it is rebuilt from depth with `render::reprojection` (last frame's view-projection times this
     frame's inverse), worked in double through the cameras' relative motion so zone coordinates in the tens of
     thousands keep sub-pixel precision.
4. **Resolve** (`shaders/taa_resolve.frag`, view size). Each view pixel gathers the 3 x 3 scene samples round it,
   weighted by a Gaussian fit of Blackman-Harris by jittered distance. It takes the nearest surface's motion among
   them (edges move with what is in front), reads last frame's history there through a 5-tap Catmull-Rom, clips it in
   YCoCg to the samples' mean +- 1.25 standard deviations (inside their extremes), and blends.
   - Colours are compressed by exposed luma while mixed, so one bright pixel cannot dominate.
   - The history's alpha holds its gathered weight. This frame adds how near a sample fell to the pixel's centre in
     view pixels (0 .. 1). The history keeps at most 12 (`kHistoryMaxWeight`; each frame is then at least 1/12 of the
     result), less the further the clip pulled it, and at most 4 once motion passes 32 view pixels a frame
     (resampling softens it).
   - A new, dropped or off-screen history takes the frame as is. Two histories (RGBA16F) alternate.
5. **Post** at view size. `post.frag` reads the history in the scene's place (`post_taa_sets_`); the distance blur and
   shafts read the scene-sized depth through `depth_texel` and UVs. The bloom is still made from the scene at its own
   size. With `display.sharpness` above 0 the post pass draws into a 10-bit image and RCAS sharpens it into the
   swapchain, as under FSR 1; at 0 it draws straight into the swapchain.

6. **The draws' own motion and the reactive mask.** Under a temporal upscaler (with `independentBlend`, which every
   desktop driver has; without it the camera's motion is all there is) the scene has three colour attachments: the
   colour, the object motion (RGBA16F: motion in red and green, cleared to `kMotionUnset`; blue below) and the
   reactive mask (R8, cleared to 0). Every scene pipeline is made for them through `VkContext::scene_rendering` and
   `scene_blends`, which mask off what its shader does not write (`SceneOutput`). Item previews, drawn into their own
   targets, keep the colour alone.

#### Temporal upscaling: per-draw motion

The scene shaders read the temporal block (the shared scene set's binding 5, `shaders/motion.glsl`: this and last
frame's plain view-projection), which `begin_scene` writes into the frame's commands. They write each vertex's place
now and a frame ago as `o_motion`:

- **Zone:** plants' wind sway at last frame's wind clock (`SceneLighting::time.y`, kept per zone renderer). The rest
  stands still, so its motion is the camera's, drawn exactly.
- **Characters:** bones a frame ago, kept per spawn (`ActorDraw::motion_key`), written after this frame's at
  `kMaxBones` in the same buffer. A spawn new this frame, or with a different bone count, moves with the camera. Both
  halves are kept and built on the CPU (`render::BoneHistory`) and copied into the buffer at once: it is write-only
  memory to the CPU, so nothing is read back.
- **Doors, objects, placed items, the placement ghost, missiles:** transforms a frame ago, kept per thing
  (`ObjectRenderer::Instance::motion_key`), written after this frame's at `kMaxInstances`.
- **Flora and the lawn:** sway and wind at last frame's clock (`Push::time.y`); the pushing spawns' bend as it is now.

Opaque passes write the motion; so do the characters' see-through parts:

- **Blended** (a see-through shield, hair: `MotionReactive`): motion over what shows through, with their depth in blue,
  as they write none. Frame generation takes the nearer of it and the depth buffer; otherwise a glass shield's pixels
  follow the background and it breaks up. temporal_prep.frag passes blue on in `motion_`.
- **Additive** (an ice shield's glowing rim, a glow: `MotionReactiveWhereSeen`): the same, only where they add
  something to see (over 0.02 in a channel; the shader's alpha, 0 or 1, blends the motion in). A faint glow past the
  body leaves what is behind alone, while the rim no longer stays behind with the background as the character turns,
  flung off in pieces in the generated frame.

#### Temporal upscaling: reactive mask

Blended and additive draws write the reactive mask (`shaders/reactive.glsl`): the zone's water, glass and glows, the
characters' additive glows, the objects' blended parts, particles, fire, spell bolts, rain and snow, the target ring
and the isometric silhouette. The value is the larger of alpha and brightness, at most 0.9, blended to the largest
over the pixel; water writes a third of that, or its waves would keep no history. The resolve keeps that much less
history. The sky and the spawns' stand-in boxes write neither.

Under FSR 1 the same shaders run with the colour alone; the validation layer then warns once that locations 1 and 2
are not drawn into, which is harmless.

#### Temporal upscaling: history resets

The history is dropped when:

- the scene targets are remade (a resize, a new render scale);
- on zone-in (`VkContext::reset_history`);
- a frame lacks the temporal path;
- the camera jumps between two frames (`render::camera_cut`: over 200 units, or a turn over 0.5 radians: a teleport, a
  camera switch). The chase camera pushed in by a tree and out again (tens of units a frame) is not a cut; the motion
  follows it exactly.

Open (docs/todo.md): a negative texture LOD bias below a render scale of 100; the bloom from the resolved picture;
held items and other attachments drawn as characters without a motion key.

### AMD FSR 3.1

`display.upscaler = fsr3` (the viewer's `--upscaler fsr3`) runs AMD FidelityFX FSR 3.1's upscaler in the resolve's
place, on the same groundwork: one sample, the jitter, `temporal_prep.frag`'s colour (with AO) and motion, the history
dropped as above. The post pass reads its output at view size (`post_fsr3_set_`, as it reads `post_taa_sets_` under
`taa`). `display.sharpness` drives FSR's own RCAS (0 skips it); our RCAS pass does not run.

- **Vendored:** only the upscaler's GLSL (`third_party/fidelityfx`, FidelityFX SDK tag v1.1.4, upscaler 3.1.4, MIT;
  its README lists the source paths and two local changes). SDK 2.x ships the Vulkan upscaler only as signed prebuilt
  DLLs under a binary-only licence, so it is not used.
- **Host side:** the SDK's runtime and Vulkan backend are not used either. `render::Fsr3Upscaler`
  (`src/render/fsr3_upscaler.cpp`) is Hailstone's own, following the SDK's `ffx_fsr3upscaler.cpp`: internal images
  (pairs alternating by frame, the luminance pyramid, the reactive and lock masks), constants, clears and dispatch
  order (prepare inputs, luma pyramid, shading change pyramid, shading change, prepare reactivity, luma instability,
  accumulate, RCAS). Binding tables are the passes' own `FSR3UPSCALER_BIND_*` numbers; descriptor sets are written
  each frame, one per pass per frame in flight.
- **Shaders:** `cmake/Shaders.cmake` (`hailstone_add_fsr3_shaders`) compiles each pass once as compute with glslc or
  glslangValidator, no AMD tool: HDR colour, reverse depth, motion vectors at render size without jitter, 32-bit maths,
  history reprojected with FSR's Lanczos table (the permutation AMD's runtime picks on its own GPUs). The reference
  Lanczos, which it keeps elsewhere, computes a sin() per tap and doubled the accumulate pass on an Intel Iris Xe.
  Accumulate is built twice, with and without sharpening (`fsr3_*.spv`).
- **Inputs:** our jitter (`jitter_px_`) is FSR's `jitterOffset` as is (a texel's unjittered place is
  `t + 0.5 - jitter` in both); its cycle length is ours, FSR's count rounded up (15 against its 14 at a render scale of
  75, the same at 50 and 100). Our motion (UV to last frame's place) goes in with a scale of 1. Near, far and vertical
  field of view are read back from the plain projection. Exposure is FSR's automatic one (its frame info), so nothing
  else is passed; the frame time is measured between FSR frames.
- **Device:** storage images in FSR's formats (R8, R16F, RG16F: `shaderStorageImageExtendedFormats`, enabled when
  present) and quad subgroup operations in compute (`render::Fsr3Upscaler::supported`, `DeviceCaps::fsr3`). Without
  them `fsr3` falls back to `taa` with a log line.
- **Why 32-bit:** AMD's runtime picks a 16-bit permutation (`FFX_HALF=1`) where the device has 16-bit floats, but the
  vendored 3.1.4 gains nothing. The accumulate pass, most of the cost, has no 16-bit code (built with `FFX_HALF=1` it
  holds no 16-bit float). The luma and shading change pyramids' 16-bit SPD callbacks are stubs (`SpdLoadSourceImageH`
  and `SpdLoadH` return 0, `SpdStoreH` stores nothing).
- **16-bit tried:** on an Intel Iris Xe the pyramids looked 13 times faster because
  they did nothing: the picture came out about 9 % darker (exposure from an empty pyramid) and softer. Writing those
  callbacks would save at most about 0.3 ms there.
- **Timing:** with `HAILSTONE_FSR3_TIMING` set, GPU timestamps round the clears and each pass
  (`Fsr3Upscaler::timing_summary`); the viewer's `--frames` logs their means ("FSR 3.1 GPU time"). On an Intel Iris
  Xe (1280x720 from 858x482) FSR 3.1 takes 3.9 ms, the accumulate pass 2.0 of them; on an RX 9060 XT 0.26 ms.
- **Motion and reactive mask:** as the resolve's (Temporal upscaling 6): the draws' own motion in `motion_`, and the
  scene's reactive mask as FSR's (`Fsr3Upscaler::Inputs::reactive`; none, FSR's default of 0, without
  `independentBlend`).

### Upscaler providers

`display.upscaler = <name>` (the viewer's `--upscaler <name>`), where `<name>` is an upscaler add-on in
`upscalers/<name>/` (docs/addons.md "Upscalers"), runs that add-on's temporal upscaler in FSR 3.1's place
(`render::Upscaler::External`).

This brings in upscalers the client cannot ship. Intel XeSS and AMD FSR 4 are closed binaries, so each is an
add-on in its own repository, built against `sdk/hailstone_upscaler.h` (MIT; docs/plugins.md "Upscaler providers" has
the interface) and NOTICE.md's add-on exception. The client never names, links or ships any of them.

- **Loading** (`render/upscaler_provider.*`): at start, before the Vulkan instance, from a copy of its folder in
  `cache/upscalers/<name>-<key>/` (key from its files' names, sizes and times; a box set's windows share one), so the
  add-on manager can replace the folder while the copy is loaded. No server sends one; a server's opted-in add-on
  policy can require, allow or block one, checked against the loaded copy as the character enters (docs/plugins.md
  "Opt-in add-on policies").
- **Device:** the provider names the instance and device extensions and Vulkan 1.0 to 1.3 features it needs, and may
  chain feature structs of its own extensions (`gather_extra_features`). `VkContext` enables them beside its own when
  the loader and GPU have the extensions and core features (`missing_names`, `missing_features`, `add_features`),
  otherwise goes on without the provider. The provider checks its extension structs' features itself, and says no for
  another vendor's GPU.
- **Frame inputs:** as FSR 3.1's (`temporal_prep.frag`'s colour with AO, the depth, motion and reactive mask, at scene
  size, `SHADER_READ_ONLY_OPTIMAL`), the jitter in FSR's convention, the camera's planes and field of view, exposure,
  frame time and a reset on a cut.
- **Frame output:** `fsr3_output_` (RGBA16F at view size, in `GENERAL`), read by the post pass as under FSR 3.1. A
  memory barrier before the call makes the inputs readable by any stage; one after makes the output readable by the
  post pass. The colour also carries `TRANSFER_SRC` and the output `TRANSFER_DST`, for a provider that copies.
- **Frame generation** follows the same motion as under the other temporal upscalers.
- **Sharpening:** a provider that sharpens (`HAILSTONE_UPSCALER_SHARPENS`) gets `display.sharpness`; for any other the
  client's RCAS runs after it, as under `taa`.
- **Falling back:** a provider not installed, not loading, saying no at any step, or lacking something on this GPU is
  dropped with a log line; the client runs FSR 3.1, or `taa` where the GPU cannot. One that fails a frame is dropped
  at the next frame (scene targets remade, device idle), and `taa` runs from then on.
- **The example** (`plugins/example/upscaler/example.c`, built into `build/hailstone/upscalers/example/`) stretches the
  colour with a linear blit: no accumulation, only the plumbing. `--upscaler example` runs it.

### Frame generation

`display.frame_gen = true` (Options > Frame Generation, shown with a temporal upscaler; the viewer's `--frame-gen`)
presents a frame made between each two real ones. It follows the motion (`motion_`: the draws' own and the camera's)
and depth of `taa` or `fsr3`; under `fsr1` it stays off with a log line.

It is Hailstone's own, fed by the engine, not a layer guessing motion from finished images. What to show where the
frames disagree follows kettle-framegen (the same author's BSD-3 Vulkan layer; no code taken).
`render/frame_gen_logic.h` holds the decisions (unit tested), `render::FrameGen` (`src/render/frame_gen.cpp`) the GPU
side. The steps:

1. **Pictures without the UI** (below).
2. **Motion at the in-between time** (`fg_scatter.comp`, two runs).
3. **Holes** (`fg_fill.comp`).
4. **The in-between frame** (`fg_synth.frag`, the view's size).
5. **Presenting.**

#### Frame generation: pictures and motion

The post pass draws into one of two pictures (`pictures_`, `kUpscaleFormat`, the view's size; written in turn), not
the swapchain. The UI draws into its own image (`overlay_`, the swapchain's format, cleared transparent,
premultiplied). The in-between frame uses pictures alone, so text and windows never warp. Each presented image is a
picture with the UI over it (`fg_composite.frag`, which also encodes sRGB for an sRGB swapchain).

Each pixel moves back along its motion to where it stood at t = 0.5 (`kFrameGenTime`). The nearest depth landing on a
place wins (reverse Z, atomic max on the depth's bits) and writes its motion there: what moves in front covers what is
behind. The first run also marks each pixel's place a frame ago, with its depth (the keys at the last frame's places),
so step 4 can tell a last-frame sample that was hidden behind something nearer.

#### Frame generation: holes and cracks

A place nothing landed on shows something this frame does not: it is being covered. It takes the motion and depth of
the farthest mark bordering it, searched in eight directions out to 32 pixels, and is flagged, so the synthesis takes
it from the last frame alone. With no mark near, it is flagged with no motion. (The background's motion: a hole is
half as wide as the background moves against what covers it, tens of pixels as the camera turns past a still
character.)

A crack is not a hole. Same-surface marks (depths within 5 %) a few pixels away on two opposite sides (four pixels
each way, along a row, column or diagonal) mean a surface whose pixels spread apart: a character turning, or nearing
the camera (close up the whole body is a grid of them). A crack takes the nearer side's motion and is blended as
usual. If this frame shows something nearer along that motion, it is background behind a thin figure: a hole after
all.

Taken for holes, cracks read the last frame along the background's motion, tens of pixels away while the camera
turned, and sprinkled the character with specks of whatever was there (its shield, its gloves).

#### Frame generation: synthesis

Each pixel samples this frame (back along the motion) and the last one (forward). The motion is at the scene's size,
so of the 3 x 3 motion texels round the pixel it takes the one along which the frames agree best, nearer ones
favoured, farther held back. (The background's motion agrees through a figure thinner than it moves, a sword, and
would cut it out.) Then:

- **Frames agree:** blend by the time.
- **Flagged:** the last frame. Where the last frame had something nearer there: this frame along the motion, when
  nothing nearer covers it there.
- **In neither frame** (background between a character's legs, behind the other leg in each; a thin sword the
  background passes behind): the nearest background sample of either frame round its own place, out to 64 pixels in
  eight directions. Searching only the last frame, to 16 pixels, gave the gap the other leg's colour: a second leg.
- **Seen through a glass shield:** counts as covered. Its colour is the shield's too, and would lay a second shield
  beside it.
- **Came out from behind:** a last-frame sample landing where something nearer was then (the keys at the last
  frame's places, the 3 x 3 round it) takes this frame, however alike the colours (a dark character against a dark
  world turning behind it).
- **At an edge:** a this-frame sample at the edge of something nearer (this frame's depth round it) takes the last
  frame. Else the anti-aliased character outline, carried along the background's motion, draws a second one beside it.
- **Other disagreement** was uncovered: this frame.
- **Off screen:** the other frame.
- **Reactive mask** (particles, spells, rain: no motion of their own): this frame along the motion, sharp, not two
  copies a little apart. Its pull is left out in a hole, where this frame shows what covers the place (else a
  see-through character copy over water, rain or a spell beside it).

#### Frame generation: presenting

A second swapchain image is acquired (`Frame::generated_available`) and both are submitted together, the in-between
frame first. FIFO is forced whatever `display.vsync` says, and the swapchain gets one image more
(`swapchain_image_count`, min + 2): waiting for the second image holds the game to half the refresh, so presents
alternate evenly. With fewer images (a surface's maximum) the second is taken only when one is free.

`frame_gen_ready` makes no frame across:

- a cut, reset or resize, or the first frame after one;
- a camera snap over 8 units (`kFrameGenMaxStep`: the chase camera pushed in by a tree, or zoomed). Running moves it
  a unit or two a frame; a frame half way through a snap uncovers half the scene.

The real frame is then shown twice (`frame_gen_repeat`, as kettle-framegen shows the nearer real frame), keeping two
images a frame. Alone, it was a hitch on top of the cut.

#### Frame generation: cost and limits

Per real frame: two small compute passes at the scene's size, two full-screen draws at the view's, one more
composite. The game reacts up to one real frame later.

- A moving thing the depth does not hold (glass, water's surface) follows what is behind it.
- Thin edges can leave a pixel-wide trace where they were about to cover the background.
- A thin, fast-swinging part (a sword) can break up as the camera turns.

#### Frame generation: measuring it

In the model viewer an animated character stands in a zone. `--orbit D` circles the camera round it at D degrees a
second (a still character, a turning world, as the player sees their own); `--spin D` turns the models on the spot
(the player turning, camera fixed). With `--frame-dt` both viewers' clocks step exactly and a timed run (`--frames`)
takes no input, so runs agree to the pixel. A run at half the step renders the truth the in-between frame stands for.

Mean error round the character against that truth (floor, two runs of the same frame, in brackets); a plain blend or
the nearer real frame scores 4.8 .. 6:

- 60 fps base, 360 degrees a second: 2.37 to 1.32 (0.98); with crack filling and both-frame search 1.28 (0.96).
- 60 fps base, 180: 1.74 to 1.02 (0.84).
- 30 fps base, 180: 2.29 to 1.44 (1.00); then 1.35 (1.01). At 90: 1.19 (1.14).

The crack and search work's larger gain was live: Hailtest (an ice shield) in the arena at 3076 x 1366, camera
turning, where the specks and second leg were plain to see. With `--spin` at 20 degrees a frame (800 a second at a
40 fps base, FSR 3.1), an ice shield's additive rim had the background's motion and left a second outline: 2.42
(floor 0.86), 1.97 once additive parts carried their motion.

- `--screenshot-generated` saves the in-between frame with the real one after it (`FILE_real`): in the viewer its
  last frame (the one before as `FILE_prev`), in the client every screenshot, the input script's `shot` too.
- The viewer's `--frames` log counts in-between frames presented.
- The performance window shows rates, skips, pacing and GPU time (`VkContext::frame_gen_stats`, docs/dev-ui.md).
  Generated GPU time counts only frames that synthesised one, not those that showed the real frame twice.

#### Swapchain: screenshots and recreation

Screenshots copy the presented swapchain image, so the swapchain asks for `TRANSFER_SRC` when the surface offers it.
If not, a screenshot is refused with a log line (and a chat line for the screenshot key), not an invalid copy.

The readback is 4 bytes a pixel, so a screenshot is also refused unless the swapchain is 8-bit RGBA or BGRA
(`render::screenshot_format_supported`). A surface offering no such format with sRGB colour space gets its first
format, maybe FP16 or 10-bit HDR; one reporting no format fails the swapchain's creation.

The swapchain is remade when the window's framebuffer size changes from the size it was made for, not whenever the
surface's `currentExtent` differs from the window's size.

#### Uploads: no waiting for the GPU

`render::UploadBatch::submit` records its copies (and mip blits) into its own command buffer, submitted on the
frame's queue with its own fence (`VkContext::submit_upload`). Its closing barriers (copy writes to vertex input,
index, vertex and fragment shader reads; images to shader-read layout) make the results visible to every later
submission. So a model, texture or icon sheet put up while a frame is recorded is drawn by that frame.

Staging buffers go once the fence signals, checked at each frame's start and each upload. Past 256 MB of staging in
flight, the oldest uploads are waited for.

A zone's uploads are staged in pieces: its vertex and index buffers are as big as its geometry, and staging one whole
doubled it in host memory. `UploadBatch::buffer_pieces` makes the target once and stages and copies each piece;
`VkContext::release_finished_uploads` frees staging the GPU has read. A long build (the zone's) holds at most a
budget (32 MB), not all of it until the first frame. `VkContext::immediate_submit` stays synchronous, for the
context's own one-off work.

#### Uploads: retiring resources

What a frame in flight (or an upload) may still use goes to `VkContext::retire`, not destroyed after
`vkDeviceWaitIdle`. It is freed once the frame being recorded, every earlier upload and any `frames_ahead` more are
done (`render/retire_queue.h`). The UI's textures take one more frame, as ImGui builds draw lists before their frame
begins. A `ZoneRenderer` is let go this way, so freeing it does not stall the frame.

A recreated swapchain is made with `oldSwapchain`; the old one and its present semaphores are retired two frames on,
as `vkDeviceWaitIdle` does not cover a present's wait.

The ImGui textures (`src/ui/imgui_vk_textures.h`: UI art, icon sheets, browser tabs) come from the client's own
descriptor pools, a new one made when they fill, with a set layout defined as the backend's. `ui::UiTextures` keeps at
most 1024 files (`kMaxFiles`), retiring the least recently asked for.

#### Modern lighting: the translucent split

In the modern lighting the scene is split for the water:

1. The zone's opaque draws (`render::ZonePhase::Opaque`, water held back), placed objects and characters' opaque and
   alpha-tested parts (`render::ActorPhase::Opaque`).
2. `begin_translucent` ends rendering and copies the resolved colour and depth (sample 0) into images `shadow_set()`
   binds as 1 and 2.
3. Drawing continues into the same targets (stored and loaded, multisampled ones too):
   - grass: opaque, but left out of the copies so the target ring lies on the ground under it;
   - characters' blended and additive parts (`ActorPhase::Translucent`, in every lighting): they write no depth, so
     grass drawn after them would cover a glass shield or a glow;
   - `ZonePhase::Translucent`: water, then blended and additive draws.

Being after the copies, a character's blended parts are missing from what the water shows through it.

#### Depth pre-pass

`display.depth_prepass` is off by default. The Options window's Depth Pre-pass (either UI pack) switches it from the
next frame, to compare; the viewer has `--depth-prepass`. The zone's opaque and alpha-tested geometry is then drawn
depth-only (`ZoneRenderer::draw_depth`, `shaders/zone_depth.frag`), then coloured with depth test equal and no depth
write: a pixel covered by several surfaces is shaded once.

- The alpha test is the colour pass's, so alpha-tested edges cover the same samples in both passes.
- `zone.vert`'s position is `invariant`, so both passes give a surface the same depth.
- The pre-pass discards every pixel `zone.frag` does, the isometric camera's cut-away included, or cut walls would
  keep their depth and hide what is behind them.
- Zone geometry only: water stays out (drawn blended after `begin_translucent`); objects, flora and characters keep
  their single pass.
- A zone builds its two pipelines whether on or not, so the switch needs no reload.
- Its shader is optional: without it the zone draws the single pass, with a log warning.

The scene target holds linear colour. The classic lighting computes in display (sRGB) values as the classic renderer
does, converting at the end of each shader; the modern lighting computes in linear HDR.

### Smooth classic lighting

The lighting has three levels, low to high (`render::LightingMode`, the Options window's Lighting choices): Classic,
the original's look; Smooth Classic (`display.lighting = smooth`, `/lighting smooth`, the viewer's `--lighting
smooth`); and Modern. Smooth Classic is Hailstone's own: it keeps the classic values and changes only where two are
worked out. Its maths is in `shaders/classic_light.glsl`, which the CPU tests compile too.

**The region light per pixel.** The original runs the region effects' light (`classic_region_light`: vertex colour,
ambient, special ambient and the sun weighed by the vertex alpha, saturated) per vertex, blended across the triangle.
`zone.frag` runs the same formula per pixel from the interpolated colour and normal, then adds the lights.wld term
(`v_point`) as before.

- Outdoors the sun shades by the pixel's own normal (classic trees' leaves show sunlit and shaded sides).
- Where a face's corners saturate, the middle no longer dims between them (Befallen's red-lit walls by about 3 %).
- A mesh without normals takes no sun, as per vertex.

**Soft highlights** (`render::Tonemap::Soft`, Smooth Classic's automatic tone curve). The original's frame buffer
clips, so what is drawn past white turns flat: at night in the cities, braziers' and torches' flames (North Qeynos and
North Freeport, thousands of pixels a view), seldom a wall. Display values to 0.8 stay exact; above, each channel
rolls off towards white (`soft_highlight`: display 1 to about 0.93, 1.5 to 0.99). That keeps colour in a flame's core
and cuts clipped pixels by about 40 %; whites the original showed at full white are about 7 % dimmer.

Fog was tried per pixel and left per vertex: at no view tried (Qeynos Hills with a near fog, with and without the
smoothed ground) did it change more than a few hundred pixels by a few levels.

The cost is a few instructions per pixel: no pass, texture or buffer. The ZoneRenderer carries the switch in
`SceneLighting::ambient.w` (`ZoneRenderer::set_classic_smooth`); everything else treats Smooth Classic as Classic.

#### Classic lighting: characters

The classic lighting lights characters with the zone's scene light (`render::classic_actor_light`, mirrored in
`shaders/actor.frag`): the region shaders' vertex formula. The vertex colour is the zone's baked light where the
player stands (the modern mode's ambient probe, `render/ambient_probe.h`), its alpha the sky's share; with none, black
with the full share. The result stays between `kClassicActorFloor` (0.25) and 1; a soft key light from above shapes
the body (0.7 .. 1). This is the client's own: how the original lights skinned models is not traced.

It replaced a fixed key light dimmed by the hour (down to 0.3 at night) whatever the zone, which left characters
nearly black against bright baked walls, as in the Bazaar after dark. Doors and ground objects keep that hour factor.

In the modern lighting, things that give off light are drawn past white so they bloom: additive particles (spells,
fires) x `render::kParticleGlow` (2.5), lava's own light x 2.5 (`shaders/zone.frag` `emit`), the sun and moon x 4.

#### Textures: colour mip chains

World textures (zone, objects, characters, sky, particles) upload as `render::TextureUse::Color`, with mip chains
built on the CPU by `assets::build_color_mips`:

- each level is filtered in linear light, colour weighted by alpha;
- transparent texels take opaque neighbours' colour, so palette index 0 (often magenta or black) no longer fringes
  leaves;
- cut-outs keep level 0's coverage on every level, so trees stay full at a distance;
- DDS files shipped without mips get a chain too.

In the ordinary private `UploadBatch::texture` path, a fully opaque image with power-of-two sides needs none of that:
its chain is a plain average, which the GPU builds as the CPU would. It goes up as it is and the GPU builds the levels
(`render::gpu_builds_mips`, `render::prepare_texture_image`). The image is created `R8G8B8A8_SRGB` with
`VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and blitted level by level, each blit decoding to linear light, filtering and
encoding back, as `assets::build_color_mips` does.

The shaders sample an `R8G8B8A8_UNORM` view, reading the same values as from a CPU chain. The two agree within a
rounding step per level (the GPU stores each level in 8 bits before making the next; tests/texture_test.cpp checks
this). These keep the CPU chain:

- a device that cannot blit that format with linear filtering (`DeviceCaps::srgb_mip_blit`);
- an image of another size: a 2:1 linear blit of an odd level reads other texels than the CPU's 2x2 box;
- an image with a cut-out: the GPU's levels would blend the colour key into the edges, bringing back the cyan fringe
  `assets::remove_color_key` takes out.

The saving is host memory: the chain and its staging copy are never held for GPU-built images (the baked terrain
tiles, uncompressed A8R8G8B8 DDS, are opaque where they are a zone's base layer).

A texture shared through the broker ("Shared GPU textures" above) gets its colour chain on the CPU instead
(`render::prepare_immutable_texture_image`) before it is keyed and exported, since the broker's image is UNORM. Data
images keep their ordinary mip recipe; BC formats the device lacks are decoded first.

#### Textures: alpha and colour keys

A material that never reads its texture's alpha (opaque, no alpha test: EQG `Opaque_*` classes and the classic opaque
methods, `MaterialState::alpha_unused`) uploads it as `TextureUse::OpaqueColor` in the actor and object renderers,
made opaque first so the colour under alpha 0 is drawn and filtered as the classic renderer does. A DDS's alpha is not
a colour key: the hero's forge plates (`genericplate*.eqg`, `generic_plate_blue_c.dds`) keep all but their cloth under
alpha 0, drawn by `Opaque_MaxCBSGE1.fx`; `Chroma_MaxCBSGE1.fx` draws the cloth from the same texture.

Some DDS textures were painted on a colour key anyway, their soft edges blended into it. potranquility's trees
(`dstree308/312/317/319.dds`, DXT5) hold cyan (0, 170, 156) under all their alpha 0, and their partly transparent edge
texels a share (1 - alpha) of it, which alpha blending and bilinear filtering draw as a cyan rim.

`assets::remove_color_key` finds such a key: one saturated colour under at least 90% of the alpha 0 texels, not black
or grey, not a normal map's flat blue. 301 of the 61,474 textures in the install have one, from cyan trees and magenta
character parts to blue spell beams. Edge texels get the leaves' own colour:

- mostly clear: their opaque neighbours';
- mostly opaque: their own with the key's share taken out (`(c - (1 - a) key) / a`).

It runs on every `TextureUse::Color` texture before its mips, and on a game DDS with its own mips, whose chain is then
rebuilt. The game's file drawn by an opaque material keeps its colour.

#### Textures: animated

Animated textures (a classic SIMPLESPRITEDEF with more than one frame and a delay, `docs/formats/wld.md` 0x04) show
frame `(t / delay) mod count` (`render::texture_frame`) on the zone renderer's effects clock (seconds since client
start). Every frame is uploaded: in the zone (`ZoneRenderer`), on doors and ground objects (`ObjectRenderer::set_time`,
from `WorldObjects`), on characters and their equipment (`ActorRenderer::set_time`). Every frame stays referenced
through texture eviction and zone handoff; shadow alpha tests select the same frame as the colour pass.

The classic flames are this: `FIRE1_SPRITE` is fire1..fire4 at 200 ms in every campfire, torch and brazier, whether
the zone places it (ecommons' CAMPFIRE) or the server does as a door (ecommons' GRATE beside Gaddius, the bazaar's
IT78 campfire from gequip.s3d).

## Fire

In the modern lighting `render::FireRenderer` draws every flame the zone burns, instead of the animated texture or
particle cloud (the classic lighting keeps those; `/lighting` switches at once).

The flame is procedural (`shaders/fire.frag`): two layers of noise rising through a teardrop, narrow at its foot,
widest a fifth of the way up, drawn out into swaying tongues. Colour runs from deep red edges through orange to a pale
yellow core, bright enough to bloom, with a few embers drifting up. A magic flame (blue, green, violet, red) keeps the
fire's brightness in its own hue.

Each flame is one quad turning about the vertical to face the camera, drawn additively after
`VkContext::begin_translucent`, fading where it meets the scene (the depth copy) and into the haze. A wide fire
(2 units across or more, and wider than half its height: a campfire, a brazier) burns as a middle flame with three
smaller tongues round it (`render::add_fire_sources`).

#### Fire: where flames come from

The flames are found in the zone's data (`assets/zone_fires.h`), in three forms by the zone's era.

**Flame meshes on placed objects and doors** (the old world and Velious): materials `FIRE1`, `BFIRE1`, `BLUEFLAME01`,
`GFIRE1`, `RFIRE1` and `PFLAME01`.

- A mesh's flame triangles whose boxes touch are one flame (a torch's crossed quads; each candle of a candelabra its
  own), taking the box's foot, 0.7 of its width and 1.1 of its height.
- `ZoneRenderer::set_hide_flames` and `ObjectRenderer::set_hide_flames` leave the meshes out;
  `ObjectRenderer::prepared_fires` gives the doors' and ground objects' flames each frame (a campfire door).

**Particle clouds on placed objects** (Luclin onwards): definitions whose sprite is `GENG*.DDS` or `GENPOP*.DDS`.

- `ZoneParticles` measures each kind of cloud (definition and node rotation, `app::measure_flame_cloud`): it burns a
  fresh emitter for one particle lifetime (at least 1 s, at most 8 s), so its oldest particles have risen as far as
  they go, then watches it for a second.
- The flame takes the bright particles' reach, 0.75 of its width and 0.85 of its height. (potranquility's pillars are
  ten 5-second clouds stacked 10 units apart: measured after a fixed second, each was under 10 units and the column
  burnt as separate flames.)
- Flame clouds are drawn only in the classic lighting; the smoke stays.

**An EQG zone's environment emitters**: `<zone>_EnvironmentEmitters.txt` (`Name^EmitterDefIdx^X^Y^Z^[AlwaysVisible^]
Lifespan`, positions in the zone's own frame) over `environmentemittersnew.edd`.

- `assets::load_zone` adds them to the zone's particle fixtures, pointing up, so both lightings draw them (before,
  nothing did): torches, braziers and their smoke, mist and the like.
- A few classic zones have the file too (frozenshadow). A revamped zone loaded from its RoF2 `.s3d`
  (`assets::is_revamped_zone_name`: nektulos, arena...) takes none: the file beside it is the live `.eqg`'s, whose
  torches and campfires would burn in mid-air in the old zone.
- Flames are definitions with a flame sprite (`FlameD.dds`, `new_flame.dds`, `new_fire.dds`, `fire01.dds`,
  `fire01_gray.dds`, `flamegray.dds`) and no "lava" in the name (nor an `mml_instance_flame` one).
- Hue comes from the definition's start colour (vald_bluetorch is FlameD.dds in 10, 10, 255), or for a grey one from
  its name (cob_brazier_green).

Not flames, though named like them: logs, coals and holders (`FIREWOOD`, `COALS`, `TORCHTOP`), fireplace stones,
Takish's red crystal torches (`TAKTORCH01`), `YKFLAME01` (black; its fireplace's flame is the cloud on it), smoke, and
`GENE01` sparkles.

#### Fire: merging and lights

- Flames from the different forms are merged, keeping the bigger where one stands in another's flame (frozenshadow's
  emitters burn on its torches' meshes).
- A flame with no zone light within 30 units gets one of its own, in its hue (EQG zones such as argath and
  commonlands have flames and no lights).
- A flame with a non-finite place or size, or over 1000 units wide or tall (a placement scaled absurdly in a broken
  zone file), is dropped before the merge: its neighbour search would walk billions of grid cells.

Held torches and equipment-backed server objects keep their authored animated materials or WLD particle clouds, drawn
by `ActorEmitters`, not replaced by the zone's procedural flame geometry. Server doors resolved only from the zone's
flattened object meshes still lack per-actor cloud metadata.

## Particles

`render::ParticleRenderer` draws spell particles, actors' emitters and the zone's clouds as quads built on the CPU
from `game::ParticleSystem`. The emitters behave the same in both lightings, as the graphics DLL's update does
(docs/formats/particles.md).

- **Classic lighting**, as the DLL: emitter by emitter, additive or alpha-blended, none writing depth, nothing nearer
  than 2 units or beyond 500 along the view.
- **Modern lighting**: past 500 units too (Hailstone's: its view reaches further). A renderer's particles go in one
  stream, sorted back to front from the camera (the nearest kept when there are too many), without depth writes.
  Premultiplied blending mixes additive (alpha 0) and blended ones in that order. Drawn after
  `VkContext::begin_translucent`, a particle fades where it meets the scene (the depth copy) over a quarter of its
  size, and as it nears the eye.

Alpha-blended particles (smoke, dust) are lit per particle as objects are: sky and ground ambient, 0.3 of the
sun's or moon's light, the special ambient and nearby point lights' pools, at least the light floor. They fade into
the haze's colour with distance.

Additive particles are light: unlit, `render::kParticleGlow` bright, and the haze takes their light away instead of
adding its colour. Additive smoke (a texture with "smoke" in its name, the Luclin chimneys' `CSMOKE.DDS`) is lit as
smoke, so it does not glow at night.

Each renderer sorts only its own particles; the clouds', actors' and spells' renderers draw one after another.

#### Particles: billboard modes

The EDD's billboard mode (+0x70; `render::particle_corners`, as the DLL's quad builder places the corners):

- **0** faces the camera, turned clockwise on screen by a spinning particle's rotation.
- **1** is a streak along the particle's axis, as long as its height, widened on the screen's x only (rays, teleport
  columns, lopingplains' fire pits).
- **2** lies flat on the world's horizontal plane, turned by the rotation (gate spells' rune circles, ice and crocus
  paths, gears, water rings). In the modern lighting it fades into what it lies on over a quarter unit, not its size.

The depth offset (+0xd0) brings a quad that much nearer along the view.

An EQG model's emitters started by an animation (`.prt` trigger 1; docs/formats/particles.md) start each time it
starts or loops on the actor (`ActorRenderer::drawn_playing`), after the entry's delay, and emit for about one play
(at most 1.5 s): run dust at the feet, swings, casts.

#### Particles: actors out of view

Hailstone (not in the original): an actor none of whose particles could show emits none
(`app::ActorEmitters::Actor::in_view`, `display.particle_offscreen_hold`).

- **Test**: its model's bounds, widened by 30 units for drifting particles (a guess: no effect's reach was measured),
  against the view the scene was drawn through last frame. In the classic lighting, past 500 units along the view is
  out too.
- **Out of view**, its emitters run but emit nothing (ticks pass unspent: no burst at one place on return), are not
  placed (its skeleton is not posed for them) and start no animation entries.
- What they emitted already ages and fades as before, at the size and opacity the actor last gave it.
- An emitter made while its actor is out of view keeps its initial burst for when the actor first comes into view.
- Zoning forgets the last view: every actor counts as in view until the new zone's first frame is drawn.
- Turning towards it, its effects fill in over their particles' lifetime, not at once.

`display.particle_offscreen_hold = 0` makes every actor emit wherever it is, as the original.

The Actor Particle Density setting changes running emitters' counts in place
(`ParticleSystem::set_persistent_density`), keeping their particles. Off stops them and makes none until turned on
again, when they are made afresh.

#### Particles: spell stages and range

A spell's stages play for every caster, players and NPCs alike: the cast stage on OP_BeginCast, the impact stage when
OP_Action 231 lands the spell.

Stock EQEmu sends the landing only to players within `Range:SpellMessages` (75 units) of the struck spawn, but the
begin cast within `Range:BeginCast` (200). Between the two, a bystander sees the cast stage and no impact; the original
client gets no more. To show the impact further away, a server owner raises that rule.

#### Particles: effects the server plays

The server can play an effect by its spellsnew.eff number on a spawn (OP_SpellEffect, OP_AddNimbusEffect;
`game/spell_effect_timers.h`). A nimbus (an item's or buff's aura of orbs or flames) is one.

- Its emitters ask for a forced 5,000 s: it stays until OP_RemoveNimbusEffect (EQEmu sends it when the buff fades) or
  the zone changes.
- An effect arriving before its spawn's model has loaded (a server resending nimbuses as you zone in) keeps each
  emitter's point and takes it once the model is drawn, sitting at the spawn's place until then. The original's spawn
  always has its points.
- EQEmu sends a nimbus only to those in the zone when it is cast: a player zoning in later does not see it, on the
  original client either.
- A timed entry's elapsed time stops at 2^32 − 1 ms and the entry is dropped (a deliberate change: the original's
  32-bit sum wraps, so an entry whose last stage starts near 2^32 stays for good).

#### Particles: procs get a cast stage

Hailstone's own: a spell landing with no OP_BeginCast before it gets the cast stage at its source on its first
OP_Action, so an item, weapon or augment proc looks like a cast. Stock EQEmu begins a buff's proc with an
OP_BeginCast (cast time 0) but not those (`Mob::TryWeaponProc`); the original plays only the impact for them
(`EQPlayer::TriggerSpellEffect`).

- An OP_Action counts as a cast's landing for 3 s after its OP_BeginCast's cast time.
- A bard song's pulses get no cast stage.
- The source plays the spell's casting animation, or without one the heal or nuke cast (docs/mechanics.md, option
  `cast_animation_fallback`).
- In the modern lighting the proc's bolt or comet leaves at the landing, as another's spell's does.

#### Particles: spell density by caster

Hailstone's own (the original has one spell density and an "On For" filter, SpellParticleCastFilter, which this
client does not read): the Options window's Particles section has three more Off / Low / Medium / High choices, each
scaling spell particle density for stages played from then on (`game::spell_fx_source`,
`display.spell_particles_self`, `_players`, `_npcs`; High when unset).

Yours are those you cast and those landing on you. Other players' and NPCs' (pets and mercenaries included) go
by the caster's kind, or the struck spawn's when the caster is not known.

## Radial flora

The EQG heightmap zones' grass, flowers and ferns (`assets/flora.h`, placed at load as docs/formats/eqg.md says) are
drawn by `render::FloraRenderer` while ShowGrass is on (/showgrass, the Options window's Radial Flora): the plants
within `kFloraReach` (400 units) of the camera. Each is one quad or two crossed ones on its foot (instanced, built in
`shaders/flora.vert`), cut out at half alpha, its top swaying by its definition's sway.

- **Modern lighting**, as the zone around them. Per vertex: sky over ground, the place's baked light (the ambient
  probe), the zone's point lights. Per pixel: sun or moon through its shadow map (one filtered comparison: grass in a
  tree's shadow darkens with the ground), a little darker at the roots, then the haze.
- **Classic lighting**: the scene's ambient and half its sun.

They take the terrain's colour with `*INHERITVERTEXCOLOR`, write depth in the scene's opaque part, shrink into the
ground over the reach's last fifth, and cast no shadows.

**Bending** (Hailstone's own): a plant's top bends out and down from the feet of spawns in it, more the nearer (`FloraRenderer::set_pushers`): the eight spawns nearest the camera within 150 units, each round itself by 0.7
of its size, 3 to 12 units. A spawn more than 10 units above or below a plant does not bend it.

#### Radial flora: distance and density

Both are Hailstone's, on the Options window's sliders and the gamepad pack's Grass distance and Grass density.

- **Flora Distance** (`display.flora_distance`), the reach: 400 to 1,200 units, 400 by default. Past 400 units a
  share of plants is kept by the square of the distance (`render::flora_keep`: a quarter at 800, a ninth at 1,200),
  the same ones each frame, so the far ring costs about what the near circle does.
- **Grass Density** (`display.grass_density`): 25 to 300 %, 100 by default. It scales the blades each of the lawn's
  three draws gives a patch (`render::lawn_lod_blades`: 49, 16 and 9 at 100 %, 147, 48 and 27 at 300 %; at least
  one). Blades keep their width: less is sparser and cheaper, more thicker and dearer. Other plants are not thinned.

Blades are made in the vertex shader: plant memory is the same at any density, and the lawn's GPU time grows with it (the flora pass in South Karana at 1280 x 720 on a Radeon RX 9060 XT, RADV: 0.54 ms at 100 %, 1.11 ms at 300 %).

Tiles fill nearest first, so when a frame reaches `FloraRenderer::kMaxPlants` (262,144) far plants are left out. A
plant is 40 bytes to the GPU a frame.

## Zone flora

Hailstone's own: classic zones have no flora; this gives them grass without changing their files
(`assets/zone_flora.h`, `display.zone_flora`). Their plants join the same `Zone::flora` the EQG zones' radial flora
fills (/showgrass hides them, Flora Distance sets their reach). The grass is a lawn the client builds itself, no game
asset (below). Donor EQG archives' ferns, flowers and bushes are scattered over it, drawn as radial flora.

#### Zone flora: rules files

A zone takes `flora/<zone>.txt` from the install folder (shipped) when there is one, else `flora/default.txt` when the
server calls it outdoor (OP_NewZone's time type 1, 2 or 5; PEQ marks some dungeons so, which the cover test below
keeps bare). Lines are read in order; the first matching ground line wins.

- `donor <archive>`: an EQG archive in the player's game folder whose `RadialFloraDefs.rfd` and textures give the
  scattered plants' shapes and pictures. Donor lines add up. Nothing is shipped: a kind whose donor (or picture) is
  missing is left out. The lawn needs none (the default names no donor).
- `ground <glob> <density> [+green|+greenish] [lawn] [<kind>[:<weight>]...]`: on ground textures the glob matches,
  the lawn if the line says `lawn`, and `<density>` plants per 100 square units of the kinds by weight (the first
  donor's definition that has it, or one donor's as `<archive stem>/<name>`). Density 0 and no lawn keeps a texture
  bare.
- `+green`: classic ground textures blend grass into cliff, sand or path in one picture, so this keeps a plant only
  where the game's texel under it is clearly green (`is_green_texel`: not water's teal, mud's olive or a dungeon's
  near-black). `+greenish` also takes dry and dark grass, for textures named as grass.
- `max_slope` (degrees); `scale <min> <max>` (scattered plants' sizes); `lawn_height <min> <max>` (lawn blades, 1.8
  to 3 units by default; the grasslands' grow 2.4 to 4).
- `include <name>`: another file's lines in place. A zone file puts its exceptions first, then `include default`;
  East Commonlands adds desert plants from `commonlands.eqg` on its sand that way.

The shared files go in this order:

1. Turn down built and wet surfaces by name (`bare.txt`: roofs, walls, cobbles, tiles, bridges, hedges, ship decks,
   water, lava).
2. Take grass-named textures with `+greenish`.
3. Turn down floors, rock, stone and cliffs (`stone.txt`; after the grass lines, so a grass and rock blend still grows
   on its grass).
4. Take anything else with `+green`. A `+green` texture with next to no green texels (`kMinGreenShare`) grows nothing.

#### Zone flora: families

Most zones take the lawn alone (`default.txt`). A zone whose climate has plants of its own includes a family file:
the same lines, plus donor kinds scattered sparsely over the lawn (about 0.4 to 1 plant per 100 square units).

| Family | Zones | Added, from |
|---|---|---|
| `forest` | the Faydarks, Kithicor, Nektulos, the Rathe Mountains, Misty Thicket, the Qeynos Hills, Jaggedpine, Toxxulia, Butcherblock, the Wakening Land, the Wakening's woods | ferns and small pines, `oldkithicor.eqg` |
| `swamp` | Innothule, the Swamp of No Hope | ferns, `innothuleb.eqg`; cattails and swamp grass, `moors.eqg` |
| `leafwoods` | the Feerrott | leafy bushes and ferns, `feerrott2.eqg` |
| `jungle` | the Emerald Jungle, Trakanon's Teeth, Firiona Vie, Stonebrunt, the Burning Woods | jungle ferns, `arcstone.eqg`; bushes, `feerrott2.eqg` |
| `plains` | the Karanas, the Commonlands, Lake Rathetear | flowering grass and wheat, `moors.eqg` |
| `kunark` | the Field of Bone, the Overthere, the Frontier Mountains, the Dreadlands, Lake of Ill Omen | flowers and shrubs, `oldfieldofboneb.eqg` |
| `luclin` | the Twilight Sea, the Grimling Forest, Tenebrous, Shadeweaver's, Hollowshade, Dawnshroud, the Maiden's Eye, Shar Vahl, Katta Castellum, Sanctus Seru | shrubs, plants and shade grass, `mistythicket.eqg` |
| `growth` | the Plane of Growth | tall flowering grass and ferns, `arcstone.eqg`; flowering grass, `moors.eqg` |

#### Zone flora: the lawn's patches

The lawn is an even carpet. The ground is cut into world-aligned squares of `kLawnCell` (3.2
units, 40 to a tile). Each square whose centre lies over lawn-growing ground (the tests below, at that centre) holds
one patch of blades at the ground's height and slope there.

- `+green` goes by the majority of five texels (the centre's and four half a square out), so a texture blending grass
  into dirt texel by texel grows no speckle.
- Each square goes to exactly one ground triangle: no gaps or overlaps.
- A patch takes the colour of the ground texel at its centre. Its height wanders slowly over the zone between
  `lawn_height`'s two values (smooth value noise on a 48-unit lattice, `assets::lawn_height_noise`), not patch by
  patch.
- Beside bare ground (a path, a road, a texture `+green` turns down) the lawn tapers over three squares
  (`lawn_edge_factor`: 0.55 of the height beside it), not ending in a wall along a texture's straight border.
- It also frays there: a patch keeps a share of its blades, its own draw between a quarter and 85% beside bare ground,
  all of them three squares in (`lawn_edge_cover`, carried to `lawn.vert` in the instance's flags).
- A patch with fewer than three of its eight neighbours grown (a lone tuft) is dropped. A tile grows its neighbours'
  squares within three of its edge to see them, then drops them.
- Under cover (a platform, a bridge, a roof) a square asks whether it is covered not at its centre but at a spot moved
  off it by smooth noise of two scales, up to three squares either way (`lawn_cover_offset`), on the ground's plane
  there and a little over it. Bare ground under a straight platform wobbles in and out rather than tracing its
  outline.

#### Zone flora: the lawn's blades

Blades are built in `shaders/lawn.vert`, untextured (`render::FloraRenderer`'s second pipeline). A patch's
square holds a grid of blades, one jittered in each cell, so cover is even.

A blade is a tapered strip of a few pieces ending in a point, standing on the ground across the patch (its slope),
curving over as it rises, a little taller or shorter. It:

- is combed: it leans 0.4 to 0.65 of its height the way a slowly turning field over the zone points (broad swathes
  lying together), a little off it, its flat side turned its own way (so wide faces do not all line up);
- leans with the scene's wind as gusts roll across the zone (the zone's plants' wind, `SceneLighting::wind`: gentle
  in fair weather, hard in a storm), fluttering a little;
- bends away from spawns walking through it, like other plants.

`shaders/lawn.frag` gives it the ground's hue with brightness held within a band (a pale or dark texel makes no pale
or dark patch), darker at the roots, brighter and warmer towards the tip. In the modern lighting it is lit as radial
flora, its face's normal turned most of the way up (lit like the ground under it, not blade by blade), with some light
through it when the sun is behind. It casts no shadow.

#### Zone flora: the lawn's distance levels

No lawn change happens at a line as the camera moves (`render::kLawnLods`, `lawn_blades`).

- A patch's blades come in a fixed order, placed by a low-discrepancy sequence shifted per patch, so any first few
  cover its square evenly.
- All 49 stand within 40 units. Past that the last ones shrink away one by one (each over three places in the order)
  to 16 at 90 units and 9 at 250; the rest widen by the square root of how many fewer there are, keeping the cover.
- The three draws (49 blades of three pieces within 90 units, 16 of three to 250, 9 single triangles past that) each
  take the first blades of the one before, so a switch removes only blades already gone.
- Past 400 units every second patch each way fades out over the 120 units before it, the kept patches widening to
  take their squares; past 800, every fourth (`lawn_patch_fade`, `lawn_patch_side`). A grown tile keeps its patches by
  the coarsest stride that keeps them, so a far tile walks only those it draws.
- It shrinks into the ground over the reach's last fifth, with the other plants.

A blade is narrow at the root (0.55 of its width), widest a third of the way up, and pointed. In South Karana, on the
AMD card (RADV) it was tuned on, the lawn costs about 0.4 ms a frame at the default reach.

#### Zone flora: where it grows

It grows on level ground (45 degrees by default) of a drawn, opaque or cut-out surface: not invisible collision
surfaces, nor translucent water.

- Not in water or lava, nor on the water's surface (the region just under the foot).
- It grows in a zone line's region. Up to 0.7 it did not, leaving bare the whole field north of North Qeynos, whose
  line to Qeynos Hills is a box over it.
- Not where the zone's own drawn terrain lies more than a quarter unit overhead (a dungeon's ceiling, a roof, a bridge,
  a path's tiles laid over the grass; the ground's own triangle is not cover). Outdoor zones' invisible collision
  ceiling and cloud layers do not count.

In a zone the server calls outdoor, terrain covers only within 20 units overhead (`assets::kOutdoorTerrainCover`, set
with the rules by `load_zone_flora_rules`, so florascan places grass as the client does). A hut's roof or a low bridge
keeps the ground bare; a cliff's overhang, an arch or Kelethin's platforms high over Greater Faydark do not.

A known cost: classic towns' thick walls are hollow boxes over the ground, so grass grows unseen inside where their
tops lie over 20 units up (North Qeynos's city wall some 21 units, Misty Thicket's wall some 33), and under raised
floors 20 to 40 units up (the Plane of Knowledge).

A lawn square asks about terrain at its spot off the centre (above), and also at its centre for a floor lower than its
blades are tall (`lawn_max`). So a floor laid a unit over the grass (the Plane of Knowledge's brick tiles over its
grass texture) shows no blades through it.

#### Zone flora: objects keep it off

Placed objects, the zone's and the server's, keep grass off the ground under them: doors, ground objects
(dropped items, tradeskill containers), placed real estate items (`WorldObjects::flora_cover`) and campfire spawns
(race 567).

- An object covers a spot with its opaque surfaces 0.25 to 16 units over it (`assets::kObjectCoverClearance`,
  `kObjectCoverReach`): grass grows up to a boulder's base, under a tree's crown and its cut-out leaves.
- A plant (`assets::is_plant`: cut-out leaves and a plant's name) covers nothing.
- A fire (a flame material, or fire, camp or coal in its name; `assets::is_fire_object`) small and low enough (within
  12 units of its middle and 16 tall) covers a disc over its whole footprint and half a lawn square round it, so the
  middle of a ring of logs is bare too. A campfire spawn covers a disc of its size (3 to 10 units, plus the half
  square) round its feet.
- A lawn square asks about objects at its centre, not the spot off it, so a small object's bare patch is not
  scattered.
- Server objects are asked as each tile grows (`FloraRenderer::set_cover`); tiles round one that came, went or moved
  grow again (`FloraRenderer::regrow`; a door swinging is not counted).

Each plant takes the colour of the ground texel it stands on (classic outdoor vertices carry none).

#### Zone flora: past the zone's edge

In the modern lighting the land round an outdoor zone (Horizon hills) grows the zone's flora too, out to 300 units
past the zone's terrain (`render::horizon_lawn_ground`: its ground there, not rock, sand, the sea, nor what lies tucked
under the zone), so the lawn does not stop in a line at the zone's edge; the fog past it takes over.
Its triangles are the zone's ground of the land's ground texture, with the land's own texture coordinates
(`assets::FloraExtraGround`).

The land is built after the zone loads. When it is ready the zone's flora is set up again with it
(`assets::add_zone_flora_ground`, some 25 ms on the frame's thread once a zone) and the grass renderer lets go of its
grown tiles (`FloraRenderer::restart`). Only for a zone whose flora Hailstone's rules grow, not an EQG zone's own.

#### Zone flora: growing tiles

The zone keeps the ground triangles that may grow plants, split into pieces of at most a quarter tile, binned by
128-unit tile (`assets::ZoneFloraGrower`). The renderer grows tiles as they come within reach, nearest first, on a
background thread of its own (`render::FloraGrowth`), and lets go of those well past it.

- A few tiles are queued at a time, so the queue follows the camera.
- A tile keeps its old plants while it grows again.
- A tile whose growing fails is logged once and not retried until the cover there changes or it leaves reach.

Each piece's plants come from its own seeded sequence, so a tile grows the same plants every time and a zone costs
memory by the reach, not its size (South Karana would hold some 6.5 million plants at once). Setting a zone up takes
some 10 to 30 ms at load.

A damaged mesh is bounded:

- A triangle with a non-finite vertex or one over 100,000 units out (zones lie within some 20,000) neither grows
  plants nor covers any.
- The ground is cut into at most 2 million pieces.
- The cover test holds at most 16 million cell references; a triangle over more than 1,024 of its 64-unit cells (at
  most 256 such) is tested at every point instead. A triangle past those bounds is no cover.
- The split bounds a piece's area, not its length, so tiles are bounded too: a piece whose bounds touch more than
  1,024 tiles (only a damaged mesh's sliver) grows nothing, and tiles hold at most 16 million piece references in all.

Plants are looks only: collision, line of sight, the navigation mesh and picking ignore them.

`tools/florascan GAME_DIR flora ZONE...` lists per zone the rules it takes and the plants on each ground texture: the
way to check a rules change over every zone. `--masks DIR` writes each texture with what `+green` turns down darkened;
`--sample N` writes some plants' feet for the viewer's `--pos`.

## Spell bolts and comets

In the modern lighting a damaging spell is thrown, not just played on the target (`game/spell_projectiles.h`,
`render/spell_fx_renderer.h`, `app/spell_bolts.h`). Which spells, by the spell file: detrimental, instant direct
damage (a current-HP effect below zero, no duration).

- On one target (target types 1, 5, 9, 10, 11) it is a **bolt** from the caster's hands, or a **comet** that falls
  on the target out of the sky when its name says so (comet, meteor, star, hail...).
- On an area round a target (type 8) it is a **rain**: a comet on each target it hits, and two more round it.
- Heals, buffs, damage over time, lifetaps and point-blank areas have none.

#### Spell bolts: elements and looks

Its **element** is its resist type: fire, cold, magic, poison, disease, chromatic or prismatic, physical (earth),
corruption. A name with lightning, thunder, static, spark and the like is lightning, whatever the resist. Each element
has its own colours and motion:

- a turbulent fireball shedding rising embers;
- a hard white-blue core with a frost star in a misty trail, shedding falling shards;
- swirling violet rings;
- a crackling ball at the end of a forked zigzag arc that jumps 24 times a second;
- a bubbling green or yellow glob shedding drips;
- a dark red rim round a dim heart;
- colours turning;
- a glowing rock in grit.

It ends in a burst (a flash, a ball swelling and tearing apart, sparks thrown out); a comet's adds a shockwave racing
over the ground. Every piece is light added to the scene, soft where it meets the scene and fading with the haze, as
fire is. Each projectile lights what it passes, and its burst flashes (up to 6 lights).

#### Spell bolts: timing

**The timing stays the server's.** The damage, the messages and the landing (OP_Action 231) come when the server sends
them. Most of these spells have no projectile in the classic client at all.

- The player's own cast is known from its start, with its target. The projectile leaves in time to arrive as the cast
  ends (a bolt flies at 160 units a second, 0.18 to 0.55 s; a comet falls for 0.7 s) and bursts when the landing
  comes. Arriving first, it waits at the target up to 0.7 s, then fizzles. An interrupted cast's projectile does not
  leave.
- Another's cast names no target until it lands, so its projectile leaves then, and the spell's impact particles wait
  for it to arrive (`SpellEffectCue::delay`).
- A spell the server flies itself (target type 1, the magician's bolts: an item type 20 missile, `game/missiles.h`)
  is drawn following that missile, in place of its model, and bursts where the missile ends.

Cast and impact particles still play. Viewer: `--spell-bolt ID` throws a spell's projectile across the
view every 2.5 s (`--lighting modern`).

## The target ring

The ring at the current target's feet (`render/target_ring_renderer.h`, `shaders/target_ring.frag`; rules in
`game/target_indicator.h`). Which ring shows, its colour and alpha (the default UI's TargetIndicator.ini section for
the target's con) and its radius (unless Max Size caps it, below) follow the original. How it is drawn and moves are
Hailstone's own (the original turns four UV-animated circles).

#### Target ring: drawing on the ground

It is a decal on the scene's depth, drawn after `VkContext::begin_translucent` (the classic lighting, with no other use
for the depth copy, takes it for the ring when there is one). The copy is taken before the grass is drawn, so the ring
lies on the ground, drawn over the blades; traced over the blades themselves it broke into a coil.

A box round the ring, from 1 + 0.6 radii below the feet to as far above, is drawn without culling or depth test. The
fragment shader keeps its far faces and rebuilds each pixel's scene point from the depth copy.

A surface less than 60 degrees from level within that height of the feet takes the ring, so it follows ground, slopes
and steps, and anything in front of it hides it: a leg or a wall is not level, a head is out of range.

Elsewhere the ring is level at the feet, shown where it lies in front of the scene (a target swimming or in the air,
the part reaching over a ledge). Behind the scene it shows only with **Target Ring Through Models**
(`display.target_ring_xray`, off by default), at a fifth of its strength.

#### Target ring: look and options

- **Band**: soft (half width 6 % of the radius, 0.15 to 0.5 units), with a wider halo. Its core goes past 1 by the
  **Glow** (`display.target_ring_glow`, 0 .. 100, default 50: up to 1.4 past, 0.7 by default) so the bloom finds it.
- **Dashes** on an inner track, about one per unit and a half of radius (12 to 48), turning slowly.
- **Chevron** outside the band where the target faces.
- **Target Ring Cues** (`display.target_ring_cues`, on by default): a second ring sweeps in from 1.6 radii over
  0.35 s when the target changes; while auto attack is on, the dashes turn about four times as fast and the band
  pulses at 1.25 Hz.
- The haze fades it with distance, as it does the spell bolts.

Options > Display "Target Ring" also sets each con's colour, Gray to Red, in the colour picker
(`display.target_ring_color.<Section>`, RRGGBB, the ini's section names; Default Colors erases them and the ini's
colours return). The free-target sections are not the player's to colour (`game::apply_ring_colors`). **Opacity** is
25 .. 200 % of the section's alpha (`display.target_ring_opacity`, at most opaque).

**Max Size** (`display.target_ring_max_size`, 4 .. 40 units, 0 off, the default) is a deliberate difference. It caps
the radius so a giant's or dragon's ring (AvatarHeight times the model's scale, tens of units across) does not fill
the screen. A ring smaller than the cap keeps its size; band, dashes and chevron follow the capped radius
(`game::cap_ring_radius`). Off, every ring is the original's size.

## Loading screen

The classic client leaves its loading screen only when everything it shows is built. Here the zone, its objects and
doors, the sky and the flora are built before the zone-in ends (`load_zone`, on the main thread while the session's
thread keeps acknowledging). Some things are built only as the world is first drawn:

- spawns' appearances, built on a worker thread and put up a few milliseconds' worth a frame
  (`ActorRenderer::kBuildBudgetMs`), drawn undressed until then;
- the light shadow cubes, one a frame;
- the UI pack's in-game windows, and the data the server sends with the zone-in (buffs, stats, chat).

A look no spawn has worn for five minutes is freed: its mesh, and the GPU buffers and texture slots no surviving model
references (`render/look_eviction.h`), so a hub's comings and goings do not keep every look until the zone changes.

Leaving at the zone-in showed these arriving over the first second or three.

#### Loading screen: settling behind it

So after the zone-in (`OP_ClientReady` sent; the server's side is done) the game runs and draws as usual behind the
loading screen (`app/zone_settle.h`, `ui::loading_overlay`: an opaque black backdrop and the loading panel over every
window in the foreground draw list) until all of these hold, or at most 15 s, so a model that never comes cannot keep
the player out:

- at least 3 frames drawn with the UI pack's frame;
- no spawn in view waiting for its appearance (100 ms of them a frame meanwhile);
- no shadow cube built that frame.

Meanwhile keys, pad and mouse reach neither world nor windows. The zone-in clocks (the input script's, the
test drivers') start when it ends. The bar runs over the network steps to the zone loaded (half), then over the
settle. Each zone-in logs `zone-in: loading screen up N ms (...)`.

#### Loading screen: plugin consent question

The foreground list would also hide the server plugins' consent question. While it waits, the loading screen (the
zone-in's and the settling's alike) is drawn instead as a window of its own over the viewport, after every other
window of the frame, and the question's windows (the client's prompt, or the built-in pack's that asked
`hs.PluginConsent`) are raised above it. The world, its names and the other windows stay hidden beneath it, and it
takes their mouse presses.

During zone settling the windows get the mouse again for that question, unless `/safelock` is on; the world gets none
either way. Whether the question waits is read once as the frame's UI begins, so the answer leaves no frame without
the loading screen; from the next frame the foreground overlay is back.

#### Loading screen: measured

Zone connect to the first frame shown, on a dev server:

| Zone | Before | After |
|---|---|---|
| poknowledge | 2.2 s | 3.0-3.3 s |
| qeynos2 | 1.6 s | 2.0 s |
| commonlands (EQG) | 4.6 s | 4.9 s |

Before, the first frames showed spawns undressed for up to 3 s in poknowledge, a large EQG object missing for a frame
in commonlands, and the HUD without the character's buffs and stats. After, the first frame shown matches those a few
seconds later.

#### Loading screen: zone build log

The zone build logs its cost as it goes, so a heavy zone shows in the log.

- **Debug level**: `memory checkpoint: zone load: <phase>: resident <N> MiB` (plus private commit on Windows, and the
  GPU heaps) after the zone data, the flora, the zone renderer, the flora renderer, the collision, the navigation and
  the ambient probe; and `zone build: <phase>: <N> MB resident (+<T>s)` through the zone renderer's own build, timed
  from the start of that build.
- **Info level**, once per zone: `zone renderer prepared: N textures (X MB), N vertices (X MB), N indices (X MB)` for
  what the zone's meshes and textures took, and the settled zone-in's
  `memory checkpoint: zone-in: settled appearances` lines.

The numbers are the log's only; nothing is kept of them.

## Culling

`render::ZoneRenderer` merges the zone into one vertex and index buffer, one draw per material. At build time it cuts
each draw into chunks with a bounding box (`Chunk`, `Draw::first_chunk`):

- **Opaque draws:** triangles are sorted by the 128-unit XY cell of their centre, one chunk a cell. Order never
  shows; the depth test settles it.
- **Blended or additive draws** keep their triangles' order, which shows where blended triangles cross. Chunks are runs
  of neighbouring triangles, restarted when the next triangle would stretch the box past 128 units across. A placement's
  triangles are appended together, so a run is mostly one object.
- **Plants:** vertices count with the room the wind can take them (`sway_reach`: at most 2.5 x the bend plus 1.5 x the
  flutter, from the bounds of `wind_at` and `wind_offset`).

Each pass draws only chunks whose box can reach its view (`render/culling.h`). A run of neighbouring kept chunks is
one `vkCmdDrawIndexed`; a draw with nothing to show sets no state. Draw order (by pass, then material), the water's place in
`ZonePhase` and the push constants (animated frames, effects, liquid flags) are unchanged.

- **The scene** (`draw`): the camera's frustum: the six planes of `view_proj`'s clip volume (`frustum_from`).
- **The sun's cascades** (`draw_shadow`): each cascade's clip volume. Its box already reaches `kShadowCasterReach`
  past its slice towards the light, and the pipeline clips at its ends (no depth clamp), so the volume is exactly what
  can land in the layer.
- **The cover and water maps**: their square of the zone round the camera, through their depth range (`cover_box`).
- **The lights' cubes** (`draw_point_shadow`): the opaque chunks in the light's sphere, on the face's side of it.

#### Culling: cover and water map redraws

Their layers keep their depths between frames (nothing else draws into them), so each map is redrawn only when one of
these changes (`cover_map_stale`):

- its view: x and y snap to its texels, its top to `kCoverStep` (32 units), the depth range that much longer so it
  still reaches `kCoverAbove` over the camera and `kCoverBelow` under it;
- the zone (`ZoneRenderer::id`) or the shadow map (`VkContext::shadow_generation`);
- the doors and objects in its box and where they stand (`ObjectRenderer::cover_signature`; cover map only);
- the map was off last frame.

Plants with swaying solid parts in its box (`ZoneRenderer::map_moves`, with wind) force a redraw every frame.
Standing still in Halas in the snow, this saves 5 of 44 shadow calls a frame.

#### Culling: merged shadow chunks

The shadow passes need a texture only for alpha-tested materials (leaves, fences): `zone_shadow.frag` and
`zone_point_shadow.frag` sample it only then. So opaque draws that are neither alpha-tested nor flames
(`casts_untextured`, all the cover map draws) are merged again at build time, one chunk a cell whatever the material
(`render/shadow_chunks.h`). Their indices are copied, cell by cell in the same x-major order, to the end of the index
buffer; each merged chunk's box holds its materials' boxes.

The cascades, the cover map and the lights' cubes draw these first, a run of neighbouring cells per call, then the
alpha-tested draws by material. The water map is unchanged.

- **Gain:** a cascade costs about one call per column of cells in view, not one per material and column (the
  Plane of Knowledge from the Library's door: 689 shadow calls a frame before, 84 after).
- **Cost:** a cell's box is the union of its materials', so a few more triangles reach the GPU to be clipped. Each
  texel keeps the same depth (shadow passes keep the nearest whatever the order). The copies cost the opaque
  draws' indices once more in memory.

#### Culling: spawns, objects and particles

Other casters are culled by their bounding spheres (as boxes, `sphere_box`) with the same tests; each used to draw
into every cascade.

- **Spawns** (`ActorRenderer::draw_shadow`): each cascade's clip volume, within the shadow reach; for the lights' cubes,
  the sphere round the model's origin.
- **Doors and ground objects** (`ObjectRenderer::draw_shadow`): each cascade's clip volume, the cover map's box.
- **Spawns in the scene pass** are drawn only when the sphere can reach the frustum (`ActorRenderer::record`). One is
  posed at all (bones sampled, blended and composed, `prepare`) only when it can show: in the view, or within
  the shadow reach while the sun's or a light's shadows can take it (`actor_needs_pose`, from `set_pose_view`). Its
  animation runs either way (`prepare_spawns`), as the mechanics read it.
- **Particles** (`ParticleRenderer::draw`) are lit, sorted and built only when the sphere a quad may turn in (its
  half-diagonal round its centre) can reach the camera's frustum (`particle_in_view`). Modern particle lighting takes
  the frame's part once (`ParticleLight`, also the plants').

The tests are conservative: a box is dropped only when wholly outside one of the volume's planes. Frames with and
without culling are identical to the pixel (the viewer's `--frame-dt` holds the clock to the frame count, so wind,
water and sky match). The viewer's `--no-vsync` with `--frames N` logs the mean frame time and the last frame's zone
triangles and draw calls.

## Sky

The sky stack (with its blended clouds and additive bodies) and the underwater/haze backdrop test equal against the
scene's reverse-Z clear depth, 0, without writing depth. They test the original multisampled depth attachment, so a
cut-out or geometry edge keeps the sky in every uncovered sample.

Where the stack is drawn:

- **Modern zone viewer:** after the opaque zone and flora, before the water colour/depth copy and all translucent draws.
- **Client:** after the opaque zone (including the distant land) but before the objects, whose single call includes
  blended parts. Object/actor/silhouette order and the grass-after-copy rule are unchanged, so those later opaque draws
  do not yet save sky shading.
- **Classic lighting:** background first, because the zone draws its opaque and blended parts together.
- **Zones with WLD detail layers:** background first too; the layers blend in the opaque group, potentially over a base that
  leaves depth clear.
- Painted skies keep their internal layer order. The model viewer has no sky/backdrop call and is unchanged.

#### Sky: atmosphere

In the modern lighting the plain skies (sky 1 normal, 2 desert, 4 cottony; `SkyRenderer::has_atmosphere`) draw an
atmosphere instead of their painted dome: `shaders/atmosphere.frag`, a full-screen triangle at the far plane, with

- single Rayleigh and Mie scattering, 16 steps along the view ray and 6 towards the sun;
- the sun on the lighting's path (`render::Daylight::to_sun`), more haze in the desert;
- ozone's absorption with the Rayleigh density (`kOzone`); without it single scattering left a green-yellow band at the
  horizon and in the twilight sky;
- by night a dark blue floor and stars on a cube-face grid (2 % of cells, about a pixel across, twinkling), turning
  about the sky's pole;
- near and below the horizon, haze.

**The sky's path** (this client's own; the original's is not traced): sun, moon and stars turn about a pole 35 degrees
over the northern horizon (`render::sky_pole`, world +X is north, -Y east; `game/coords.h`). The sun rises due east at
6:00, stands `render::kSkyNoonElevation` (55 degrees) over the south at noon and sets due west at 18:00. Day and night
keep the classic hours, but noon's shadows fall north, not straight down, and the light models shapes all day.
The moon keeps its place relative to the sun (below), a little poleward.

**The sky's light** (`render/atmosphere.h` `sky_light`): the same scattering model runs on the CPU (48 directions,
cached while the sun moves less than a thousandth). It feeds:

- the modern sky ambient (`Daylight::sky`): the cosine-weighted hemisphere, a quarter desaturated for the clouds and
  the multiple scattering the model leaves out;
- the haze's colour: the sky a few degrees over the horizon (below).

The ambient is deep blue at noon, paler and dimmer with a low sun, and gone after dusk, when the night's floor (stars,
the moon's sky) takes over. The land fades into the colour of the sky behind it.

The modern cloud layer and cloud patches thin out towards the horizon (the layer from 0.3 down to 0.03 of the sky's
height, where its painting stretches into streaks), so the atmosphere's own horizon shows there.

#### Sky: sun and moon

The atmosphere draws its own sun and moon instead of the sky's sprites:

- **Sun:** a 0.9-degree disc, darker towards its edge, as bright as the sunlight getting through the air (it reddens
  at the horizon and blooms).
- **Moon:** a 1.6-degree disc, grey with darker seas, phased by the server's calendar
  (`game::SkyClock::moon_phase`: twelve months of 28 days, one lunar month a calendar month, new on the 1st, full on
  the 15th).
  - It stands where the sun stood phase x 24 hours earlier, a little off the sun's path (`render::moon_direction`): a
    waxing moon is in the evening sky, the full moon rises at sunset, a new moon passes beside the sun.
  - The shader lights the sphere's sunward side, so the phase follows from where it stands. The dark side is faint
    with earthshine.
  - It reddens near the horizon, hides the stars behind it and shows pale by day; a faint halo grows with the lit part.
  - The viewer and the client take `--moon P` (0 new, 0.5 full) instead of the calendar.
- **Blood moon:** the full moon of every third month (3, 6, 9, 12; `game::SkyClock::blood_moon`).
  - Deep red with a wider red halo; the night floor turns purple-red and the moonlight red (`render::kBloodMoon`).
  - Wholly red for the day and a half about full, fading over the half day either side.
  - It follows `OP_TimeOfDay`'s month, so everyone on a server sees it on the same nights (a game month is 33.6
    real hours: one about every four real days).
  - When it comes the chat says "The moon turns the color of blood." `--blood-moon B` (0..1) sets it instead.

#### Sky: northern lights and shooting stars

- **Northern lights:** on some nights (`game::SkyClock::aurora`: each calendar night, noon to noon, draws its strength from
  its day and month; none on three nights in five, else 0.3..1).
  - Green curtains, violet at the top, hang over the northern sky (+X, about which the stars turn), folding and
    drifting slowly, striped by rays, brightest at their sharp lower edge.
  - The shader marches the view ray through 16 layers of a sheet of air, each pixel starting at its own offset.
  - By night only, behind the cloud layer; they fade under a storm.
  - `--aurora A` sets them. The push block is full, so the value rides in the atmosphere's haze `a` (0 off, else
    1 + aurora).
- **Shooting stars:** every 3.5 s a 55 % chance of one in the upper night sky, streaking about 12 degrees in 0.7 s, a
  bright head and a fading tail a pixel or two wide, hidden by the moon and the clouds.

#### Sky: god rays and the cloud layer

God rays: `VkContext::set_sun_shafts` (`render::sun_shafts_for` places the sun on the screen) lets `shaders/post.frag`
march 40 steps from each pixel toward the sun over the depth. It adds the open sky's light near the sun, fading each
step, so trees and walls before the sun leave dark gaps between bright shafts. Strongest with the sun on screen and
above the horizon, they fade over half a screen past its edge and go with a storm.

The cloud layer is blended over it, its clouds keyed out of the painted sky by the red channel and lit by
`render::cloud_light` (the sky's ambient and the sun): white by day, warm at sunrise and sunset, dark by night. The
procedural cloud patches take the same light; the sun and moon sprites stay. The other skies (the planes', Luclin's,
red, grey) keep their painted layers.

### Volumetric clouds

Over the atmosphere (the plain skies in the modern lighting) the clouds are a layer of air 1.5 to 4 km up over a round
planet, marched along each view ray (`render/cloud_renderer.h`, `shaders/clouds.comp`), replacing the painted cloud
layer and the `weather.ini` cloud patches. This is Hailstone's own look; the original has only painted clouds.

`display.volumetric_clouds` (Options, Volumetric Clouds; off on the Low tier) turns them off; the viewer takes
`--no-clouds`. The patches keep moving while hidden, so turning the option off shows them in place.

- **Shape** (after Schneider, "The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn", SIGGRAPH 2015):
  - a weather map (30 km across) sets where cloud grows and its type;
  - a height profile keeps flat clouds to the layer's bottom third and lets towering ones fill it;
  - Perlin-Worley noise (7 km across) gives the billows; finer Worley noise (0.9 km) wears the edges, wispy at the
    bottom, billowing higher up;
  - the noise is drawn on the GPU when the clouds are first shown (`shaders/cloud_noise.comp`: a 128³ shape, a 32³
    detail and a 512² weather map, all tiling). No noise file ships.
- **Cover** (`render::cloud_cover`): fair-weather cumulus over the normal sky (0.36 of the map), a few over the desert
  (0.18), heaped over the cottony sky (0.55). A storm (`WeatherLook::storm`) eases every sky to overcast, thicker
  (2.2 times), flatter and dark at its base, as it gathers and clears (about 36 s in, 75 s out).
- **Wind** (`render::cloud_wind_step`): the field drifts from the west-south-west at 12 m/s, twice that in a full storm.
  It is summed frame by frame (so a storm speeds the clouds up without a jump) and wrapped every 600 km, a whole number
  of every texture's tiles (a wrap throws the history away once).
- **Light** (after Hillaire, "Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite", SIGGRAPH 2016):
  - the sun by day, the moon by night (`Daylight::to_light`, `light`, after `apply_storm`), marched six steps towards
    the light from each sample;
  - multiple scattering is approximated by three octaves of a dimmer, deeper-reaching, wider lobe (Wrenninge et al.
    2013), so a cloud's shadowed side is not black and its edge towards the sun glows;
  - ambient comes from the open sky (`Daylight::sky`), less at the cloud's base;
  - each step adds light in Hillaire's energy-conserving form; the march stops once under 1 % of the sky shows.

#### Volumetric clouds: cost and compositing

- **Cost:** the clouds are marched at half the scene's size on each side (`render::cloud_extent`).
  - Each frame marches one pixel of every 2x2 block (`render::cloud_update_slot`, in a Bayer order) at 40 steps
    overhead to 64 near the horizon, each start jittered (interleaved gradient noise).
  - The other three are carried over from last frame, turned to this view: the clouds are so far away that only the
    camera's turn moves them on screen.
  - A freshly marched pixel is blended in at a fifth, so the jitter averages out over about a
    second. A pixel whose last place was off screen is marched at once.
  - The atmosphere reads the image through a 3x3 tent filter (four bilinear taps), so the 2x2 blocks do not show at
    the clouds' edges as the view turns.
  - The history is thrown away (`render::cloud_history_stale`) when the sun or moon jumps more than 0.02 radians, the
    storm by more than 0.1, or the wind by more than 0.5 km.
- **Into the sky:** the compute pass runs before the scene (`SkyRenderer::prepare`).
  - `shaders/atmosphere.frag` reads its image (binding 2: rgb the clouds' light, a the sky still showing) and puts the
    clouds before the sky, stars, sun and moon: after the storm greys the sky, before the horizon haze.
  - Far clouds thin out (exp(-distance / 45 km)), so the atmosphere's own horizon shows under them.
  - Lightning lights the clouds twice as much as the clear air.
  - Without clouds the atmosphere reads a clear 1x1 image, and the sky is unchanged.
- **Measured** on an RX 9060 XT (RADV) at 1280x720, in the viewer over Qeynos Hills: about 0.5 ms a frame over the
  painted clouds (10.6 against 10.1 ms). The viewer's `--pan D` turns the camera D degrees a second, to watch the
  carried-over history.
- **Not yet:** the clouds cast no shadows on the ground, and the god rays do not see them (a storm already turns the
  god rays off). The isometric camera's orthographic view has no history, so every pixel is marched each frame.

## Weather

`OP_Weather` {kind: 0 rain, 1 snow, 0xff stop; intensity} plays out as in the classic client (`game::Weather` over
`game/weather_state.h`):

1. Each frame, in the open (time types 1, 2, 5) with the weather on, the weather start runs, then the update starts a
   pending message, so a message waits for the end of its frame.
2. The amount moves toward intensity x 0.01 at 0.25 a second; a different kind replaces the current one at once.
3. `ApplyWeatherState` fades the weather in over 64 frames (4 a frame to 0x100) or out over 43 (6 a frame).
4. The next frame shows the start message (rain 0x21d, 0x21c in zone 0x48; snow 0x21e) and turns the rain or wind
   sound on, or off.
5. Once faded in, rain strikes lightning at once, then every 10 to 20 seconds, with a 30 % chance of a second strike
   within a second. Snow turns the wind on at those times instead.

Each strike plays its thunder with the flash: thunder1 or thunder2 (sounds 0x8f, 0x90) at random, a second strike the
other one (`Weather::thunder`). The fades count frames, as in the classic client; their fog band is not applied.

#### Weather: precipitation

Falling rain and snow are the classic precipitation manager (`game/precipitation.h`), driven by `game::Weather` (the
effect's kind, wind (0, 0, -1) and intensity) every frame, in every zone type, with or without a sky.

- **Definitions:** `Resources/Precipitation/default.ini` (`<name>.ini` with `[Settings] Valid=1`, else default):
  - `[Settings] <Kind>Falling<n>` / `<Kind>Splashing<n>` (n = 1..3; kinds Rain, Snow, Hail, Ice, Blood, Slime, Fire,
    OP_Weather's kind indexing them) name `[Falling<i>]` (MaxSpeed, Acceleration, Width, Height, Rate, Frames,
    Orientation, File) and `[Splashing<i>]` (Life, Chance, Width, Height, Rate, Frames, Orientation, File).
  - The default file: snow falls at 8.04 units a second (accelerating at 32.15) as 0.15-unit `snowflake02` flakes
    tumbling through 16 frames at 16 a second, facing the camera; rain at 64.3 as 1-unit `rain_drop02` streaks. A tenth
    of the drops leave a flat splash (`snow_hit` 0.12 units, `Rain_Hit` 1 unit) for a second.
- **The particles:** a pool of 7500. Each update (`0x10081860`) adds round(7500 x seconds x p): at most 10 new, the
  rest restarting from landed ones' columns (40 to 140 units above their own ground, at their
  first speed).
  - A new one starts 40 + 100 r1 r3 above the camera and 200 r1 away along the camera's heading turned by up to
    128/512 (three quarters of them) or 128 to 256/512 (the rest), at (1 - r1)^2 of MaxSpeed.
  - p is the server's intensity x 0.01 (1..10 gives 0.01..0.10), moving at 0.25 a second, so intensity 5 is 375 drops
    a second.
  - Landed columns over 200 units from the camera are let go.
- **Where a drop lands:** the first surface a line from 1200 units above it to 500 below meets
  (`CollisionMesh::first_hit`: transparent surfaces ignored, passable ones hit, back faces skipped), so drops stop on
  roofs and tree tops.
  - Unlike the classic client, back faces below the drop are hit too, or a roof with nothing over it (the East
    Commonlands tunnel's, under a mountain with no top) would let rain through to the floor.
  - Only below it: a zone's ceiling facing down over the sky (East Commonlands' at z 719) would otherwise be every
    drop's ground: each would land as it started, and no rain or snow would show.
  - A drop that lands on ground whose normal z is at most 0.8 never splashes again.
- **Colour:** each particle a random blend of the sky's two precipitation colours for its kind (CSky +0x38: the current
  colour map's rows 4 + 2 kind and 5 + 2 kind, column 31, alpha included; `game::SkyState` from `Resources/Sky`), so
  snow is white by day and grey by night.

#### Weather: drawing the precipitation

- **Classic** (`render/weather_renderer.h`, `shaders/precipitation.vert`), built as the classic renderer does:
  - only beyond 2.5 units in front of the camera, at least a pixel wide;
  - orientation 0 facing the camera, 1 a streak standing up from the drop, 2 flat;
  - 16 frames a 4 x 4 atlas, 4 a 2 x 2, frame trunc(Rate x age);
  - texture x colour, alpha blended, depth tested, no depth write. The lightning's flash brightens them.
- **Modern** (`render::kModernPrecipitation`, not the original's look). The original's rain (1-unit streaks a pixel wide,
  a few hundred within 200 units) is hard to see at today's resolutions, though the ground shows the rain plainly.
  Classic lighting keeps the original's. Modern lighting:
  - stretches each upright streak over the distance its drop falls in 0.08 s (about 5 units at full speed, like a
    camera's motion blur) and draws it at least 1.5 pixels wide;
  - has the precipitation manager start three times as many drops (its spawn rate; the 7500-particle pool and the
    column reuse are the original's);
  - draws a facing particle at least 2.5 pixels across both ways, and below 8 pixels of its own size fades it into a
    soft round dot (fully a dot at 4), so falling snow shows as specks. Otherwise the snowflakes (0.15 units across,
    each atlas frame a thin outline about 8 % opaque) are a pixel or two a few units off, where the texture's small
    mips average the outline away.

#### Weather: the world (modern)

The modern lighting's `WeatherLook`:

- **Transitions:** the original's precipitation reaches the server's intensity within a second; the look eases it.
  - The storm (`WeatherLook::storm`, the precipitation x 10 eased: about 36 s to gather, 75 s to clear) dims the sun
    by up to 70 %, greys the sky light and the atmosphere, spreads the cloud layer over the sky and
    brings in the weather's fog band (Haze, below).
  - Drawn drops thicken and thin over about 18 s either way (`WeatherLook::precipitation`, given to the
    precipitation manager as `Frame::eased_intensity` in place of its own p; classic lighting keeps the original's).
  - Rain to snow takes a few seconds (`WeatherLook::snowing`).
  - Viewer: `--weather-age S` shows the weather S seconds after it began, `--weather-stopped S` S seconds after it
    stopped.
- **Wet:** the ground wets over 30 s of rain and dries over 2 minutes: darker, with a Fresnel sky reflection, most on
  level surfaces. Doors and ground objects wet the same way (`shaders/object.frag`); characters get a
  lighter sheen (`shaders/actor.frag`).
- **Snow** lies on level surfaces in noisy drifts, covering over 2 minutes and melting over 5: on the ground and the
  doors' and objects' roofs and ledges. Not on characters: drifts fixed in the world would slide over them.
- **Cut-outs:** an alpha-tested zone surface (leaves, fronds, grates) gets no puddles or snow and a third of the wet
  look: its normals, a crown's or a card's, mostly face up and would paint a generated tree's crown
  with the sky's reflection or turn it white.
- **Shaders:** all of it is in `shaders/weather.glsl` (`weather_surface`, and `puddle_surface` on the zone's ground),
  fed `SceneLighting::weather` (`render::set_weather`: wet, snow, the rain falling, the puddles).
- **Per zone:** entering another zone restarts the look dry with a clear sky (`WeatherLook::update`'s zone id); a
  storm's snow, wet ground and sky do not follow the player.
- **Lightning:** a strike (`render::lightning_flash`) flickers: a flash, a dimmer one 70 ms on and a third at 190 ms,
  each gone in a few frames, over a faint afterglow of half a second. It lights the sky, the clouds, the ambient and
  the haze (`lightning_haze`).
- **Viewer:** `--weather rain|snow N` shows it already settled, the particles falling for the last 30 s.

#### Weather: puddles

Puddles fill the flat ground's low spots over 90 s of rain and dry over 4 minutes. Warped noise picks them, a finer one
frays their shores; a long storm covers up to about a quarter of the flat ground.

- **Flat only:** both the smoothed normal and the triangle's own normal (screen derivatives of the position; the
  smoothed one reads level at a slope's foot or on a rounded crest) must be within about 11 degrees of level, full
  puddles within about 6. Ground nearer that limit needs a deeper spot to hold water.
- **Surface:** at the shore the shallow water shows the soaked ground. Further in, its sky reflection takes over by Schlick's Fresnel term (water's 2 %), so it reads dark from above and a mirror at a low angle.
- **Rim and glints:** a damp, darker rim rings it, and the sun or moon glints in it, shadowed (`puddle_surface`; the
  cover is `shaders/puddle_cover.glsl`, which `tests/lighting_test.cpp` also compiles as C++).
- **Ripples:** while it rains, drops ring them (`rain_ripples`: jittered cells about a unit across, more drops the
  harder it rains), fading out where a pixel spans more than a tenth of a unit.

Known gaps (wet ground, snow, puddles):

- Without shadows (`--shadows 0`) there is no cover map, and the ground under roofs gets wet and snowy.
- The cover map reaches 256 units round the camera. Beyond it all is open sky, so ground under a farther roof gets wet
  and snowy, and that edge moves with the camera.
- Trees give the ground no shelter (the falling drops do stop on them).

## Wind

In the modern lighting placed plants move in the wind (`render::wind_at`, `SceneLighting::wind`): a breeze veering
slowly about the south-west in fair weather, harder and gustier with the storm.

`ZoneRenderer::build` marks a placed object as a plant when it has a masked material and its name, or a material's or
texture's, says so (tree, pine, palm, bush, fern, grass and the like). Each vertex gets a sway (`GpuVertex::sway`): how
far it bends (the square of its height above the plant's foot, up to 4 % of the plant's height) and how much its
leaves (the masked parts) flutter.

`wind_offset` in `shaders/lighting.glsl` leans the plant with the wind, rocks it as gusts roll across the zone
downwind, and shivers the leaves. Both the zone and shadow vertex shaders apply it, so shadows move with the
leaves. An object with its own animation (below) gets no sway: its animation is how it moves.

## Animated zone objects

A placed object whose skeleton plays its own animation (docs/formats/wld.md, "Placed-object animation") is drawn where
the animation has it. `ZoneRenderer::build` draws its still triangles per placement like any object's, and keeps its
moving triangles as one instanced model per definition (the instancing of "Model overrides"):

- Each vertex names its node in `GpuVertex::point_light.x` (its index among the definition's moving nodes + 1; 0
  still). Point lights reach only regions, so a placed object has no other use for it.
- Each placement's instance names the definition's first motion in `GpuInstance::normal_row[1].w`.

A motion is one moving node's matrix. Each frame `animate_instances` poses each definition once on the zone's
animation clock (`set_animation_time`, milliseconds since the zone loaded, the clock the collision uses) and writes
every motion's rows, now and a frame ago, into the frame slot's motion buffer (set 1, binding 4).

`zone.vert` and `zone_shadow.vert` apply a vertex's motion before its placement. The motion pass's previous place uses
the rows a frame ago, so the temporal upscaler and frame generation see the parts move.

- **Cost:** the upload is a few matrices however many placements. Emerald Jungle's 22,174 animated trees draw
  in fewer calls than their per-placement copies did (0.43 ms against 0.58 ms a frame classic, 2.36 against 2.60
  modern, `--frames` at the same view).
- **Bounds:** the instances' boxes reach as far as the animation takes the parts.
- **Light:** vertices carry the first placement's baked light; another placement's instance tints them by its own over
  that one's, channel by channel, so its gradient is the first one's.
- **Shadows:** the point lights' shadow cubes (drawn once and kept) and the cover map keep the parts in their first
  pose.

## Zone lights

In the modern lighting the zone's lights (lights.wld) light zone geometry per pixel (`shaders/zone.frag`
`zone_point_lights`). `render::build_light_grid` sorts them into a grid of 64-unit cells over the world's XY plane (at
most 24 lights a cell, the nearest to its centre). The zone renderer binds lights, cells and indices as storage buffers
(set 1, bindings 1 to 3). Each light gives two terms:

- **MPL term:** half the MPL vertex term (`game::point_light_term`, colour x 0.5, each light saturating), added to the
  baked vertex colour in display values as before. Interiors keep the balance their builders gave them (scaled with
  the baked colour's lamp share by night, see Modern lighting).
- **Pool:** in linear light, colour x 3 x facing x `light_pool` (`shaders/lighting.glsl`): the inverse square past a
  core of a fifth of the radius (8 to 16 units), windowed to zero at the radius. Bright at the centre: a torch lights
  the ground and walls round it, and a big city light (70 to 200 units) pools round its lamp instead of lighting a
  whole square evenly.

Characters and objects take the zone's lights with the same falloff, and the nearest from the scene's light list. Warm lights (torches, fires) flicker by up to 12 %.

## Light shadows

Modern scene lighting gives every visible carrier its light (`game::carrier_light_value`):

- **The player's** comes from its own inventory (the brightest worn or general-slot item). Stock EQEmu sets a client's
  spawn light only when building the spawn packet, ignores the client's own SpawnAppearance type 5, and
  sends type 5 only for NPCs, corpses, bots and mercenaries.
- **Every other carrier** uses its spawn record's light, changed by SpawnAppearance type 5. It follows the displayed
  body position and joins the nearby point-light list.
- The existing carried-light palette sets the color and radius; dynamic lights remain switchable. The local player's
  carried light keeps its dedicated shadow slot.

The zone's lights, the player's carried light and the spells' cast shadows in the modern lighting
(`render/point_shadows.h`), so a torch no longer lights the far side of a wall. The classic renderer has no shadows at
all.

- **Which lights:** each frame `PointShadows::update` picks the lights within 360 units of the camera (to their sphere)
  that light most round it, by brightness x reach over distance: up to `display.light_shadows` (8), less the slots
  kept for moving lights (below). A light keeps its slot unless another scores half again better, so neighbours do not
  trade places. The shadow fades in over a quarter second once drawn and out over the last quarter of the range.
- **The cubes:** each slot has a cube of six depth faces (`VkContext::begin_point_shadow`, a second depth array image,
  `display.light_shadow_size` texels a side), holding each caster's distance from the light over its radius.
  - The face mapping is this client's own (+x, -x, +y, -y, +z, -z; the other two axes over the distance along the
    face's), covering a little more than 90 degrees so the filter's taps stay on the face.
  - The shaders that draw and read it share `point_shadow_face` and `point_shadow_clip` in `shaders/lighting.glsl`.
  - Casters within 3 units of the light (its lantern, the torch's head) are left out.

#### Light shadows: static cubes and placed objects

- **Drawn once:** the zone and its lights stay put, so a slot's zone cube is drawn when the light gets the slot, and kept.
  - At most one a frame (`kPointShadowBuildsPerFrame`): a cube is six passes over the zone round the light, the
    frame's longest part when lights come into range. It was two, so the last of a group of new lights now shows its
    shadow a few frames later, under the quarter-second fade-in.
  - `ZoneRenderer::draw_point_shadow` draws the opaque geometry's 128-unit chunks ("Culling" above) that reach the
    light's sphere and the face's side of it, the plants at rest.
  - With characters in a light's reach, the cache is copied into the slot's live cube each frame and they are drawn
    over it (`ActorRenderer::draw_point_shadow`); otherwise the shaders read the cache.
- **Placed objects:** doors and ground objects swing and come and go, so they are drawn like characters.
  - When one's bounding sphere (round its vertices, grown by its matrix's largest scale; `render/caster_bounds.h`)
    reaches a shadowed light's sphere, the slot goes live.
  - `ObjectRenderer::draw_point_shadow` then draws over the cache the opaque and alpha-tested parts of those within
    reach and on the face's side (`render/point_shadow_pass.h` takes the object renderer beside the actor renderer).
  - In a town this keeps most slots near doors live: a cube copy and a few small draws each a frame.

#### Light shadows: reading the cubes

The shaders find a light's shadow without searching (`point_shadow_at` in `shaders/lighting.glsl`):

- Each scene light carries its entry among the shadowed lights in `color.w` (`render::tag_shadowed_lights`, run by
  `PointShadows::apply`, matching on position and reach within a tenth of a unit and a hundredth).
- Each zone light carries a shadow key (`render::point_shadow_keys`: 1 + the first light in the same place, so stacked
  lights share a cube), which its entry also holds (`slot.z`). Only the current zone's renderer stores keys
  (`ZoneRenderOptions::light_shadow_keys`).
- The shadow is looked up only where the light reaches and faces the pixel; every term it scales is zero elsewhere.
- The lookup is a normal offset that grows as the surface turns from the light, then the 3 x 3 hardware-filtered
  comparisons in four lookups (see Shadows under Modern lighting).
- In the zone a blocked light loses its pool and MPL share. The baked vertex colour counts every light as unblocked,
  so it gives back what the light would have added (`game::point_light_term`'s weight), never below a third of
  it.

#### Light shadows: lights that move

The player's carried light and the spells' brightest have slots of their own (`render::dynamic_shadow_slots`):

- one for the carried light from 3 slots up, one for spells from 5, two from 7. The default 8 leaves 5 for the zone's
  lights; fewer slots all go to the zone.
- They are kept even when nothing shines, so a spell coming and going does not evict a zone light's cache.
- Each frame `PointShadows::set_dynamic` takes them exactly as the scene's light list has them (the shaders match by
  position): the carried light at chest height, then the spells in range scored as the zone's lights are.

With no cache, their live cube is drawn whole every frame (zone chunks in reach and characters): they cost the most. The zone's chunks in the light's sphere are found once a frame for its six faces
(`ZoneRenderer::point_reach`), and each face tests only those on its side; a static cube's build does the same.

The player is left out of its own light's cube (its body surrounds it). A spell's caster is unknown where the light is
made, so a spell's cube instead leaves out every character whose bounds hold the light (the caster's hands, the
target's body). The zone's dynamic-light loop, characters and objects read them like any shadowed light.

Known gaps: plants' shadows from the lights stand still. Steady cost is about a quarter millisecond a frame with eight
lights (RX 9060 XT, 1280 x 720).

## Water and lava

The modern water (`shaders/zone.frag` `modern_water`) draws on every water surface:

- **Classic zones:** a level, passable triangle (WLD 0x10, as every classic water and lava surface is) with a water
  region (WT, SL, VW, W2, W3) just under it and not just over it. A beach's sand or a dock just under the water's top
  is solid and stays ground.
- **Classic, cut by a dry region:** the same material at the same height where a dry region cuts into the water: a
  zone line's region over the sea (North Ro's line to Oasis), the strip by a zone's edge
  (`render::classic_liquid_triangles`, by the BSP regions).
- **EQG zones:** the Water effect.

#### Water: the surface

- **Colour:** the texture's average (its last mip, with 30 % of the texture itself), or the EQG effect's two colours
  (both black, as bloodfalls' blood: the diffuse map's average).
- **Through the water:** the floor shows through by the depth copy, each colour absorbed with depth (more for the
  colours the water is not), the view bent by the waves.
- **Reflection:** a screen-space march over the depth copy (40 steps and a 5-step refine), fading at the screen's
  edges. The EQG reflection colour tints it; Fresnel (0.02 at normal incidence) mixes it in. Where it hits nothing:
  - the sky as the frame drew it in the reflected direction, if on screen (`screen_sky`: clouds, sun, stars, aurora
    mirrored);
  - else the haze's colour at the horizon deepening overhead to the sky light's hue, with the sun's glow
    (`sky_reflection`);
  - an EQG water whose environment map is a "noswap" cube (kept by the original instead of its sky cube: a cave's, a
    volcano's, a day sky; about 100 zones) reflects that map instead of the sky, laid out at load as one octahedral 2D map by
    `assets::decode_cube_as_octahedral` (the game's cubes stand +Y up).
- **Waves:** twelve waves from 18 units down to half a unit move the normal, spread up to about 70 degrees either
  side of the wind (a light breeze without one; the wind's strength raises them), a few across it. Each has its own
  phase, all about as steep, so no one direction rules.
  - Slow noise warps their plane by up to ten units, so crests bend and break up rather than run in ruled lines.
  - Short ripples gather in drifting gust patches, calm between; two layers of drifting noise add the fine chop.
  - Each fades once under four pixels long. The sun's glint blooms and breaks into glitter rather than a grid.
- **Foam:** within 4 units of depth where the floor rises towards the surface (a beach, a bank, the foot of a wall; a
  shallow pool with a level floor stays clear), broken by drifting noise, gone by 160 units away.
- **Falls:** an EQG water surface steeper than about 53 degrees (bloodfalls' falls are RegionWater meshes) is a thin
  sheet running down (`falling_water`): rippled by the effect's two scrolling normal samples, streaked by its diffuse
  map, reflecting by Fresnel in the material's tint, a little of what is behind showing through.
- **Edges and below:** a soft edge over the last half unit of depth. From below, the sky and a bright sun show
  through Snell's window overhead (a cone about 97 degrees across); outside it the surface mirrors the water.

#### Water: under the surface

Modern, with shadows on:

- **Water map:** for a zone with water, `render/cover_map.h` draws the water surfaces alone from straight above into
  the shadow map's sixth layer (`kWaterLayer`), in the cover map's view, whenever the view changes ("Culling"). The
  shaders read its depths (the shadow set's binding 3, `water_depth` in `shaders/lighting.glsl`) to know how deep any
  point lies.
- **Caustics:** the sun's light is absorbed with depth and gathered into a drifting web of bright lines
  (`underwater_light`: two ridged noise fields over a warped plane, taken where the light entered the surface): 0.3
  between the lines, up to about 4 times on them. The sky's light fades with depth too. Covers zone geometry,
  characters and objects, and shows through the surface from above.
- **The water round the camera:** with the camera in a water region, the haze becomes the water
  (`render::set_underwater`): the daylight's sky and sun in a blue-green medium, as thick at every height, reaching
  220 units, absorbing red first, then green, then blue. The scene clears to that colour, and
  `ZoneRenderer::draw_backdrop` fills the open water behind everything (`shaders/underwater.frag`), carrying the
  surface overhead on past its polygons.
- **Shafts:** `underwater_shafts` marches 8 steps along the view (jittered per pixel). Each step is lit as much as a
  slow pattern on the surface above lets the sun in along the ray to the sun, fading with depth and distance,
  strongest towards the sun. Past the water map's reach the surface is taken to go on level.

#### Lava and waterfalls

Lava (`modern_lava`) draws on:

- the classic LA surfaces;
- EQG RegionLava (and Lava2) whose texture is fire-coloured or whose name says lava, magma or molten (the class also
  draws energy, slime and mist, which keep their own colours and glow);
- harbingers' RegionLavaH walls (its Fresnel bias of 1 makes the original opaque; the diffuse map is molten rock).

Heat comes from the texture's brightness, churned by two layers of slow noise drifting with the flow: dark basalt
plates break and close over molten pools, hot seams run between, and the whole throbs slowly out of step. Molten
parts glow deep red through orange to yellow, past white so they bloom; the crust is lit as rock, warmed from below.
A RegionLava class's crust mask (Diffuse0's alpha) still marks its molten layer.

Waterfalls (RegionWaterFall, modern): the class reads colour and alpha at two scroll rates, so its colour often fell
where the texture is clear (black) under a sheet the alpha says is there. The colour is taken where the texture shows (each sample weighted by its own alpha, else the texture's average), a little brighter where the
sheet is thick, with a faint sky sheen at grazing angles.

EQG textures with a `<texture>.txt` sidecar (frame count, ms a frame, the frames) animate on the zone's clock, as
classic ones do (`eqg::parse_texture_animation`). A heightmap tile with no DAT layers (scorchedwoods has
one) takes the base ecosystem of the nearest painted tile, laid by its own heights and slopes; the original leaves a
hole there.

## EQG surface maps

Held EQG equipment keeps its authored normal, specular and environment maps in modern lighting, through armour's
actor surface path (the cleric epic shield's pearl environment map and strength travel with its mesh). `AddAlpha`
actor and object materials keep their authored unlit, unfogged state in both lighting modes.

#### EQG surface maps: zone materials

EQG zone materials of the opaque, Alpha and Chroma classes name a tangent-space normal map (`e_TextureNormal0`,
uncompressed A8R8G8B8 `_n.dds` files) and, by their shader, a specular or reflective term. The classic renderer binds
the normal map only with bump mapping on, and bumps only its multi-pass lights. Modern lighting always uses them (`shaders/zone.frag`
`mapped_normal`, `surface_shine`):

- **Normal map:** uploaded as data (mips filtered linearly, not as colour). With no tangents in the meshes, the frame
  comes from the screen derivatives of position and texture coordinates. Green runs along +v, as in Direct3D's (and
  3ds Max's) maps. The bumped normal feeds the whole modern light: the sun and its shadow, the sky
  hemisphere, the zone's and the dynamic lights.
- **Specular** (shader names holding `CBS`, `SB`, `GB` or `Full`, and the packed `CBGG1` class when its map
  loads): a Blinn-Phong highlight of the sun at `e_fShininess0` (40 when unset), as strong as `e_fGloss0` (0.2), in
  the vertex's share of the open sky.
- **Reflection** (`CE1`, `CBSGE1` with `e_fEnvMapStrength0` > 0, read doubled): the sky (`sky_reflection`) in place
  of the material's environment map, weighted by Fresnel.

#### EQG surface maps: packed maps

The `CBGG1` / `CBSG1` classes use a packed surface map; `CBSGE1` adds environment reflection. The player's compiled
`SkinMeshCBSG1` and `SkinMeshCBSGE1` shaders confirm the channels, which the actor and zone renderers use separately:

| Channel | Use |
|---|---|
| RG | tangent normal's direction, Z fixed at 0.9 before normalization |
| B | added to ambient illumination (glow mask) |
| A | replaces `e_fGloss0` for the specular term |

- The diffuse colour times the glow mask is added after the weather, independent of baked light, sky share and
  shadows. Modern lighting gives it the same HDR headroom as additive particles (`render::kParticleGlow`, 2.5);
  classic uses intensity 1.
- A missing surface map supplies no packed normal, glow or gloss.
- Native `CB1` and `CBS1` shaders keep full RGB normals, even where an item shares one map across packed and ordinary
  materials.
- The legacy `CBGGE1`, `CBE1` and `CBSE1` names select `CBSGE1`'s native render class and so its packed
  illumination and environment path; the material metadata keeps the original name.

#### EQG surface maps: equipment environment maps

Equipment environment maps, cubemaps or ordinary 2D sphere maps, become octahedral textures for the actor renderer. Sphere-map coordinates follow the native `SkinMeshCBSGE1` vertex shader: reflect the surface-to-eye
direction, project its X/Z axes around the +Y pole, then scale into texture coordinates. The cleric epic shield's
pearl map is a 2D sphere map. A malformed cubemap is rejected without treating its first face as a sphere map.

#### EQG surface maps: held MPL, waterfall and lava materials

- **MPL:** keep their separately scaled coverage texture and modulate diffuse RGB component by
  component (`saturate(2 * diffuse * coverage)`); the material's alpha stays separate. A missing optional coverage
  texture keeps the material's diffuse colour.
- **WaterFall:** colour and alpha scroll independently.
- **Lava:** keeps both scrolling diffuse layers and the shared normal map sampled at both coordinates; the primary
  alpha blends its lit colour against the bump-lit secondary.
- **Lava2** is drawn as Lava, as the zone draws it (its own formula is not settled, docs/unknowns.md).
- Over an MPL coverage map a lava material keeps its second layer, the two sharing one slot
  (`render::actor_lava_surface`, `actor_takes_coverage`).

In the classic lighting the native surface map is read only for a glowing part. These actor effects use the display
clock, wrapped at 100 seconds as the native preshader does. Hailstone's world light, shadows and colour space still
determine the final lighting.

#### EQG surface maps: additive zone surfaces

The crystal's beam and radial rays in Forgotten Halls are additive model triangles; the zone's geode particle
fixtures supply the floating motes, not the rays.

- Modern lighting multiplies every EQG `AddAlpha` zone surface, in every EQG zone, by the same 2.5 for bloom;
  classic keeps their texture-direct additive intensity.
- The full-bright view (`--lighting none`, `VertexLighting::None`) skips the gain: the zone renderer clears its bit
  there, as the shader sees every surface unlit in that view and `AddAlpha` surfaces always are.
- Opaque EQG materials and WLD additive materials are not brightened.

Apart from that packed glow mask, classic lighting keeps its diffuse-by-lit-colour surface path.

#### EQG surface maps: Hero's Forge and attachments

Hero's Forge and held attachments carry their own EQG surface properties, supplied layer overrides included, into the
actor renderer.

- Native maps use the RGB or packed tangent reading their material class selects, with EQG zones' +v convention.
- Texture-pack normals keep their separate BC5 RG reading and remain the fallback when a native map is absent.
- Reflection uses the material's environment DDS, converted from cube or sphere; an absent or invalid map supplies
  none.
- The base shader's known specular/reflective classes select the effect; a layer's parameters do not invent a new
  shader class.
- Native surface records share the actor's lighting buffer: one bounded table per frame, shared by a mesh's
  instances. Push constants stay 128 bytes; no descriptor bindings are added.
- Ordinary WLD and Luclin surfaces keep their diffuse path, animated texture frames and attached WLD clouds.

#### EQG surface maps: alpha-to-coverage

Alpha-tested materials (trees, grass, fences, Chroma/Masked) draw in the opaque pass with alpha-to-coverage when the
scene is multisampled. The pipelines set `shaders/coverage.glsl`'s `kAlphaToCoverage`, and the alpha test becomes a
ramp about one pixel wide around the reference, capped at 0.05 of alpha so far minified textures leave no specks
against the sky.

- **Zone colour and depth shaders** turn that ramp into a sample mask, intersected with the LOD fade's mask. Their
  pipelines leave fixed-function alpha-to-coverage off: with shader sample-mask output and fragment discard it made
  terrain and buildings disappear on Radeon RX 9070 XT. Both passes share the pixel noise and coverage helper, so
  cut-out edges keep the same samples with the depth pre-pass enabled.
- **Actor and object world pipelines** keep hardware alpha-to-coverage. The world shader contains no sample-mask
  output.
- **Item previews** compile a separate actor shader with the shared explicit coverage mask and alpha 1 on accepted
  samples, to avoid squaring coverage when the transparent preview is resolved and unpremultiplied for ImGui.

## Modern lighting

`render/scene_lighting.h` (`daylight`, `make_scene_lighting`) and `shaders/zone.frag`, `actor.frag`, `object.frag`:

- **Daylight**: the sun follows the sky's path (Sky: up at 6:00 in the east, 55 degrees over the south at noon, down
  at 18:00 in the west), its colour from the elevation through an air-mass model (white overhead, orange to red
  low), up to 3.2 at noon.
  - By night the moon (`render::moon_direction`) gives `render::kFullMoon` (0.2-0.36) when full and up, falling
    steeply with the phase (0.125 + 0.875 x the lit part squared: an eighth at the new moon), red under the blood
    moon. It also lights the night sky's ambient by as much.
  - With the moon down: no direct light, more starlight in the ambient (a moonless night stays barely readable).
  - A sky/ground hemisphere ambient: by day the sky model's own light (Sky), by night a dark blue floor.
  - Indoor zones (time types other than 1, 2, 5): no sun, a dim neutral ambient.

#### Modern lighting: zone geometry and characters

- **Zone geometry**: the WLD vertex colour, the special ambient and the per-vertex lights.wld sum (the classic MPL
  term) add in display values, converted after the sum, so interiors keep their builders' brightness. The vertex
  alpha (the share of sky light the classic region shader gives the vertex) weighs the hemisphere ambient and
  the sun.
- **EQG terrain sidecars:** binary EQG terrain also takes its mesh's `.lit` sidecar, including that alpha. Previously
  only object placements' sidecars were read, leaving older TER meshes opaque white: no authored indoor light, and the full sun
  wherever the shadow map allowed. This asset-loading correction applies in both lighting modes and changes no
  daylight or shadow formula. Older outdoor EQG zones with terrain sidecars regain their authored lighting; zones
  without them keep their terrain colours.
- **Also:** alpha-tested plants wrap the sun's light; water gets a sun highlight; the dynamic lights (the player's
  carried light, the spell effects') are added per pixel, shadowed (Light shadows).
- **Characters and placed objects**: the hemisphere ambient, the sun (wrapped for bodies), the special ambient and
  the zone's lights near the camera per pixel, never below a small floor, plus the light of the place they stand in
  (the ambient probe, below).
- **Character select and creation**: the model stands in clz's tree shade at noon and read nearly black, so modern
  lighting adds a key light (`game::char_select_key_light`): a warm, unshadowed point light on the camera's
  side, above the head and to the left, as far out as the race's camera. Not the original's (the sun alone).

#### Modern lighting: baked colour in linear light

- **Tinted baked light** (`baked_to_linear` in `shaders/lighting.glsl`): the zone's vertex colours, special ambient,
  MPL term and lights' colours were tinted by their builders on a display. Converted channel by channel they grow far
  more saturated in linear light (befallen's grey-blue walls went a vivid pure blue), so 70 % of the conversion scales
  the colour by its grey's (the balance as seen) and 30 % converts it as is.
- **Uncoloured meshes**: a zone mesh whose vertices are all the loader's opaque white (an EQG placement without
  lighting data) takes no baked light in the modern lighting, only the sky, the sun and the lights, as doors do; its
  white had added a full display white, glaring beside the ground.

#### Modern lighting: night and the baked colour

The vertex colour holds the builders' ambient, sun and lamps; left whole (as the classic renderer leaves it) it lights
every surface evenly at any hour. The modern light splits it per pixel:

- **Lamps' part:** what the zone's lights give the pixel as `game::point_light_term` weighs them (less what their
  shadows block).
- **The rest:** the remainder, never below a third of it.

`Daylight::baked` and `baked_lamps` (the scene block's `sky.w` and `ground.w`) scale the two:

| When | Rest (`baked`) | Lamps (`baked_lamps`) |
|---|---|---|
| Day | 1 | 1 (the day looks as baked) |
| Night (falling from the sun 15 degrees up to 6 below the horizon) | 0.3 (0.45 under a high full moon) | 0.35 |
| Indoors (the baked colour is the only light), any hour | 0.5 | 0.7 (moody, still playable) |

The half MPL term falls with the lamps' part, so no lamp counts three times (baked, MPL, pool). The pools
carry the light round the lamps, the moon and the sky the rest. A storm takes up to a quarter more off the rest.

#### Modern lighting: the ambient probe

`render/ambient_probe.h` averages the zone's vertex colours round the player (24-unit cells, two cells each way,
nearest first, eased over half a second; `SceneLighting::probe`). Characters and objects take the zone's baked light
as the zone keeps it (`Daylight::baked`), their sky light scaled by its share there (at least a quarter). Before, a
character inside a building or dungeon took the open sky's light and stood out bright against the walls.

One probe serves every character and door in view (the player's place); distant ones in another light are in the
haze. At character select and creation the probe is the model's place, not the camera's.

#### Modern lighting: indoor follow-up

Restoring EQG terrain lighting is the first correction from the Forgotten Halls comparison.

- Fixed-camera offline captures confirm the restored terrain file removes the broad sunlit bands and restores
  localized lamp pools, but the hall is darker overall.
- The crystal and statues also use packed `CBGG1` maps (EQG surface maps). Correct channel decoding restores their
  outward normals and the crystal's authored glow (the beam and rays: additive zone surfaces, above).
- The indoor baked-light balance still needs visual tuning.
- An outdoor `qeynos2` control is unchanged; shade in the older EQG `draniksscar` becomes darker with its terrain
  sidecar restored. The loader correction needs that broader visual review before it is a finished lighting pass.
- A reference player's carried light and vision can brighten the floor too; before changing those formulas, a
  comparison needs the same position, hour, lighting options and player-light state.

#### Modern lighting: spell light

`game::particle_lights`: the glowing spell effects light what is round them, up to 8 lights of the 32.

- **Which emitters glow:** additive ones, unless the texture is smoke, dust, rock, cloud, water or darkness;
  alpha-blended ones whose texture is fire or energy (flame, fire, flare, explosion, electric, spark, glow and the
  like).
- **Position and weight:** each glowing emitter is one light at the centre of its particles, weighted by their
  opacity and size, so sparks under 3 units count less than a sheet of flame. Emitters within 12 units merge.
- **Colour:** their tint times its texture's average colour (`ParticleRenderer::texture_color`).
- **Strength and reach:** follows how much of the effect is alive; 40 to 100 units as the particles spread.
- **Actor effects** use the same calculation, with up to four EDD lights and four WLD cloud lights before the shared
  scene budget is selected. These lights follow the posed model or item and obey `/dynamiclights`.

#### Modern lighting: shadows

`render/shadows.h`, `render/vk_context.h`: four cascades out to 1200 units (mostly logarithmic splits). Each is an
orthographic view of its slice's bounding sphere, snapped to whole texels, in a depth array image (D32), drawn by:

- `ZoneRenderer::draw_shadow`: opaque and alpha-tested draws, no face culling, the chunks in the cascade's volume,
  slope-scaled bias;
- `ActorRenderer::draw_shadow`: the spawns `prepare_spawns` posed before the cascades (`draw_prepared` draws them in
  the scene);
- `ObjectRenderer::draw_shadow`: the doors and ground objects `prepare` placed, opaque and alpha-tested parts, no
  culling (`shaders/object_shadow.*`).

Sampled with a 1.5-texel normal offset and 3 x 3 hardware-filtered comparisons, fading out over the last 15 %, it
multiplies the sun term of the zone, character and object shaders. The nine comparisons are taken as four
bilinear lookups with the same texel weights (`shaders/shadow_filter.glsl`, after Castaño 2013). The light cubes use
the same filter. `tests/lighting_test.cpp` checks it against the nine on a model of the hardware's filter.

#### Modern lighting: ambient occlusion

`shaders/ao.frag`, `shaders/ao_kernel.glsl`, `render/vk_context.h` AoView: after McGuire et al.'s scalable AO, on the
resolved depth.

- View positions from the reverse-Z projection terms; the normal from the nearer neighbour on each axis.
- 12 samples on a spiral within 4 world units (more smears the ground under characters; at most a tenth of the
  screen height), turned by a 4 x 4 ordered pattern.
- Each sample takes the bounded angular kernel (cubic distance falloff times the sample's horizon above the surface).
  Close up, the kernel's world radius shrinks to the pixel cap's own world size, so near samples cannot darken out of
  proportion.
- An angular bias of 0.1 keeps almost coplanar samples from shadowing themselves.
- Within 12 units of the camera the AO fades out, to none at 4 (`ao_close_fade`): there the capped kernel, about a
  tenth of the depth across, finds each fold of a low-poly face (an erudite's brow groove, the
  cheeks and eye sockets) that its smoothed normals hide, drawing dark lines over a face up close. Contact shading
  within arm's reach of the camera is lost with them.
- AoView's intensity 12 over the 12 samples scales their shade. The bounded kernel weighs a mid-range sample less
  than the old one (whose weight grew as 1/|v|, which blotched the near ones black), and 12 keeps a
  contact corner's shade near the old depth (tests/lighting_test.cpp).
- The post pass averages the 4 x 4 block weighted by depth and darkens the scene by it (strength 0.85) before the
  exposure. As it darkens the whole scene colour, every lighting term included, it is kept to contact shadows.

Hidden zero-height lawn blades stay collapsed at their root with finite current and previous motion positions
(`shaders/lawn.vert`, `blade_at`); before, their rise divided 0 by 0 and sent NaN to the depth AO reads. Grass keeps
writing depth; AO, distance blur and shafts use the final depth.

With MSAA and AO active, a global memory barrier from colour-output writes to fragment sampled reads also follows the
final depth image transition (`VkContext::begin_overlay`). It repeats that transition's own scopes, so by the Vulkan
spec it does nothing: an unconfirmed workaround, kept because the grass AO flicker on Radeon RX 9070 XT was
reported gone with the change that brought both. See [World rendering](unknowns.md#world-rendering) for what would
settle it.

#### Modern lighting: view distance and haze

`render::modern_view`, `render::zone_haze_length`, `set_haze_linear`, `apply_haze` in `shaders/lighting.glsl`:

- **Far plane:** the classic far plane x `display.view_distance`, the fog distances moving with it, so characters
  are drawn out to it. Where the sky flag is off (no sky, under water, in lava or a thick-fog region) the classic
  one stays. Mouse picking keeps the classic yon.
- **Haze instead of the exp2 fog wall:** optical depth 3.5 (d / length)^2, thinner above the camera (scale 600 units),
  towards the zone's fog colour with a glow around the sun. The sky blends into it within about 7 degrees of the horizon.
- **Distance blur:** the post pass blurs up to 1.75 pixels from a quarter of the way out to the far plane.

#### Modern lighting: haze details

- **As thick as the zone's fog**: the haze's length is set so that it is half clear where the original's fog,
  exp(-(10 density (d - start) / (end - start))^2), is: at start + sqrt(ln 2) (end - start) / (10 density), the
  length sqrt(3.5 / ln 2) times that.
  - Never longer than the far plane. With the fog off (no density): the far plane, or the fog's end where the view is
    not drawn further.
  - A zone keeps its builders' murk; the longer view only lets what lies past the original's far plane fade, not cut
    off.
  - Density 0.1 (innothule, nektulos version 0, the commons) is a light fog, a third clear at its end. 0.33 (most
    zones, nektulos version 1) is a thick one, nearly opaque by about 60 % of the way.
  - Before, the haze spanned the whole extended distance (nektulos version 1, served from Rain of Fear on, showed no
    fog: half clear at 1670 units against the original's 390) or, unextended, the fog's end at any density
    (innothule's light fog about 3.5 times as thick, the dark swamp near the camera washed into the fog's bright
    colour).
- **The glow** is sunlight the haze scatters, so no brighter than the haze's own colour allows
  (`render::haze_glow`): full in a day's fog (linear luminance 0.05 and up), less in a darker one. The fog colour
  darkens by the classic hour tables, which lag this sun: at 5:50 nektulos's fog is 19/14/10 while the sun is
  14 degrees up at 2.8. Undimmed, the glow turned everything in the haze towards the sun one flat tan against the
  near-black fog behind.
- **Its colour outdoors** (`render::haze_target`, `haze_sky_color`): the zone's fog colour drawn towards the sky's own
  colour a few degrees over the horizon (the sky's light, above).
  - 70 % under a drawn sky and 35 % without one (gfaydark, toxxulia), less as the zone's fog thickens (none with the
    fog ending at 400 units, all at 1200), so nektulos's murk stays its builders'.
  - Never more than 2.5 times the fog's brightness, so a zone with a dim fog by day gets no glare.
  - Before, every outdoor zone's distance was one flat lilac grey (200, 200, 220) under a blue sky, hard-edged where
    the sky's horizon met it.
- **Between places** (`render::HazeBlend`): the haze's colour, length and reach (`extend`: the original's far plane or
  the extended one, `modern_view(view, multiplier, extend)` for the part way) ease over about 1.5 s, so walking into
  lava or a thick-fog region, or a weather change, does not pop. A zone-in starts afresh, and coming out of the
  water the haze is back at once.
  - The original's area fog (AFG region boxes over `weather.ini` `[AreaFog-<zone>-<name>]`, `game::area_fog_blend`)
    is not applied: neither the RoF2 nor the live install has an `[AreaFogSettings]` entry.
- **The weather's fog band** (`render::weather_haze`): the storm pulls the haze in by up to half (more for snow), greys
  it (cooler and a quarter darker under rain, whiter under snow), and the lightning lights it.
- **Where no sky is drawn** (sky 0, as nektulos, or SetFog's sky flag off, and indoors), the haze also fills the
  background (`ZoneRenderer::draw_backdrop`, `shaders/underwater.frag`) with its colour and glow at an infinite
  distance, so the gaps between leaves and the world past the far plane match the hazed trees in front. Clearing to
  the plain fog colour had left hard, stair-stepped leaf cut-outs against the glowing haze.

Textures are decoded from sRGB in the shaders (after filtering, a small error accepted for now).

## Colour grade

The post pass grades the modern lighting's frame (`render::color_grade`, `VkContext::set_color_grade`,
`shaders/post.frag` `mood`): a tint of the linear scene before the tonemap (luminance kept at 1, so the exposure does
not move) and a saturation after it. Subtle by design:

- outdoors, warm (1.07, 1, 0.9) round sunrise and sunset, cool (0.9, 0.97, 1.1) and a fifth less colourful by night,
  5 % more colourful by day;
- indoors, a little less colour;
- a touch (7 %) of the zone's fog hue: unrest's purple, feerrott's green, lavastorm's red;
- a storm greyer (up to 22 % less colour) and a little cooler.

`display.color_grade` (the modern Options window's Lighting section, Colour Grade) turns it off; the viewer takes
`--no-grade`. The classic lighting is never graded.

## Horizon hills

In the modern lighting, outdoors, land stands round the zone out to about the modern view's reach, so the world does
not end in a blank haze; not round a zone floating in the sky (Sky zones, below). Hailstone's own: the classic client
drew nothing past a zone's geometry.

It is real geometry, built while `ZoneRenderer::prepare` builds the zone (`render::horizon_mesh`, `render/horizon.h`)
and drawn with the zone's own materials through `zone.frag`. It takes the zone's textures (a pack's photo materials
and normal maps too), light, sun and haze exactly as the zone's ground does; the depth test puts it behind anything
nearer. Scenery only: nothing collides with it.

It is made from the current zone alone; the zones past its lines are neither drawn nor shape it. Up to 0.7 the
neighbour zones were drawn past their lines, then copies of the zones visited on the server; they showed walls,
filler and seams at the lines that no general rule fixed across the game's zones, and were removed.

#### Horizon hills: what the zone says

`render::horizon_land` reads the terrain's opaque triangles and its water and lava surfaces. The level ones (normal's
z at least 0.8, either way up) give:

- the level ground's extent (2 % of its area cut each side);
- its median height (the land's base);
- its spread from the 10th to the 90th percentile, 40 to 1200 units (the relief, which makes rugged zones' land
  taller).

A zone with less than 250,000 square units of level ground gets none.

#### Horizon hills: the grid

The terrain (less geometry more than 4000 units past the level ground) is laid on 64-unit squares. Terrain anywhere
in a square covers it, with its lowest and highest point; a water surface marks it wet.

Each empty square takes, from the nearest covered square, its distance and the zone's edge there, smoothed over a few
squares: the highest terrain (a wall's top, the open ground at a zone line) or, where water reaches the edge, the
water's surface. The edge is a shore where water lies straight in from it within 192 units, behind a quay, averaged
over 1024 units of edge, so a river's mouth is no shore.

#### Horizon hills: the land

A grid of vertices covers the zone's squares and 2500 units past them, 64 units apart (more for a zone wider than
256 squares with that), then wider by 12 % a step out to 10000 units. Vertices are tucked down so the land meets the
zone's edge without a gap and stays under its ground:

- in a covered square, 40 units under its lowest terrain;
- in the empty square beside one, 5 units under the lowest terrain beside it (a wall's foot), or 40 under the zone's
  water where only water is beside it.

The squares under the zone are drawn too; they show only through the back of its walls, which have only inner faces.

Past the edge the land starts at the edge's height, rising gently by the zone, with the steep ground far off behind
the fog past the edge:

- over 2500 units it turns into rolling hills, 200 to 650 units high with the relief, growing from 300 to 3000 units
  out;
- from 2500 to 8000 units out, ridged mountains, 900 to 2250 units high, up to half again where a broad mass lifts
  them.

(Up to 0.7 the hills were full height 800 units out and the mountains rose from there to 5000, steep at the zone
line.) All of it is seeded by the zone's short name (`render::horizon_seed`), so it is the same every visit.

The hills are warped noise, their lines bending and branching (the coordinates pushed up to 900 units by a noise of
their own first), not a value noise's grid of blobs as up to 0.7. A pocket the zone's terrain closes round (a gap
between its walls, a hole in it) is a flat lid just under its rim.

#### Horizon hills: the sea

Where the edge is a shore, the floor falls to 250 units under the zone's water level (the median of its water
surfaces) within 800 units and stays there: open sea with no coast, since nothing says where the next coast is
(docs/unknowns.md; up to 0.7 coasts and islands rose 2500 units out). Where a shore ends, the coast wanders in
headlands, bays and the odd island (the shore's share pushed by a noise 700 units across), not the edge's blur.

The surface is drawn at the water level with the zone's water material, from the zone's own water's edge, over every
land square the sea reaches at a corner, so the coast runs where the land rises through the water, not along the
64-unit squares (up to 0.7 all four corners had to be sea, drawing the coast as steps). Not over the zone's own
terrain or water, nor a square whose corners' seas stand at different levels.

#### Horizon hills: wear

`render::horizon_terrain` (Hailstone's own) wears the hills by water and slumping, so from afar they read as ranges
with valleys:

1. A grid over the land (64-unit nodes, at most 512 a side) takes the land's heights.
2. As many drops of water as it has nodes run downhill on it, taking ground up where they speed and dropping it where
   they slow: Hans Theobald Beyer's particle erosion ("Implementation of a method for hydraulic erosion",
   2015): inertia 0.08, at most 48 nodes each.
3. Six passes of slumping move ground off slopes steeper than it holds.

A node is worn only as far as it is free: not on the zone's squares, by its edge or under the sea; fully 1500 units
past the edge; at most 384 units down and 192 up. The wear is added to the land's heights (`HorizonTerrain::delta`);
where the most water ran (`wet`) the woods gather and the ground is darker.

It is deterministic: the drops come from a generator seeded by the zone, the first pass's rows are shared among up to
eight threads, and its layers are kept to an eighth of a unit and a 255th.

**Cached** per zone and server (`cache/terrain/<server>/<zone>.bin`, `render::write_horizon_terrain`,
docs/layout.md), keyed by what it was worn from (`horizon_terrain_key`: the zone's grid and heights, the generator's
version, 2 since the neighbours no longer shape it), so changing any of them wears it again. Wearing takes 182 to
236 ms of the zone's build (Qeynos Hills, Butcherblock, North and South Karana, 400 to 513 nodes a side), the cache
1 to 2 ms; a file is 0.5 to 0.7 MB.

Under the sea the land darkens with depth, to 0.3 of its light 150 units down (the water map that darkens a zone's
own sea floor leaves the land out).

#### Horizon hills: the look

Of the zone's plain materials (no shader effect, no scrolling), the one covering the most level ground draws the
land, the one on the most slopes its rock, and the most used water material the sea.

Ground and rock are counted by the edge of the zone's box (within 400 units, or 12% of the box across), where the land
meets the zone, if anything plain lies there, else over the whole zone. Not counted:

- level ground under water (a water surface over it, or the point just over it in a water region);
- a slope standing more than 40 units over the lowest surface of its 16-unit square (a roof, an overhang);
- for the rock or the sand, a texture named as building material (roof, brick, wall, tile, floor, plank, wood,
  shingle).

(Counted over the whole zone, Freeport East's harbour floor drew its land teal, North Ro's its sea bed, and North
Qeynos's roofs its peaks pink.)

Each is mapped at its own texture scale (the zone's UV area over area) and wound as the zone winds its own. The land
is one draw (`zone.frag` `kLandSplat`): each vertex says how much rock (slopes under 0.8 up, the mountains' upper
half) and sand (a few units over the sea) cover it (`HorizonMesh::splat`), the edges broken up by the textures' own
light and dark.

The rock is mapped from all three axes, blended by facing, at half the zone's scale; twice (the second turned and at
another scale, blended by a slow pattern over the world), and pulled half way to its average colour. Otherwise a
banded rock texture (Qeynos Hills' `HHDROCKGRASS.BMP`) drew the same bands down every slope, as stripes.

The sand is the zone's level texture named so (`sand`, `beach`), else its ground made sandy (grey, then warmed). The
ground's repeat is broken up by itself at a fifteenth of its scale.

The sea is drawn from both faces: a classic water surface is two sheets, one facing each way, and says nothing of
which is up (up to 0.7 the facing taken from it culled the whole sea from above, leaving bare floor: Butcherblock's
and Lake Rathetear's).

The vertex colour is the level ground's median baked light, darker in woods-like patches on the level land, in the
valleys and in broad patches that hide the texture's repeat.

The log says what it used: qeytoqrg 36892 triangles, ground `XGRASSDIRT2.BMP`, rock `HHDROCKGRASS.BMP`; butcher with
10108 water triangles.

#### Horizon hills: when it shows

Outdoors (time types 1, 2, 5), under an open sky (1, 2, 4) or none (gfaydark, everfrost); not under the planes' and
Luclin's painted skies. A thick fog, lava, a thick-fog region, a storm and the night hide it by the same haze that
hides the zone.

#### Horizon hills: cost

- Qeynos Hills: about 99,000 triangles in 256-unit chunks (44,000 before its squares were kept fine out to 2500 units
  past the zone), culled with the zone's.
- No shadows: neither cast nor drawn into the sun's cascades or the cover and water maps.
- Looking out over it: 9,000 more triangles and 70 more draw calls; the frame time did not move.
- The splatted look, at 1600 x 900 with the vsync off: west over Qeynos Hills' coast 1.00 ms a frame against 0.96
  with the plain land; north over Butcherblock's woods 1.26 to 1.30 against 1.17. Rock and sand are sampled only
  where a vertex has some (sampled everywhere: 1.19 and 1.43).

#### Horizon hills: trees

The zone's own trees stand on the land's woods (`render::horizon_trees`, `render/tree_impostor.h`).

**Which:** the zone's commonest placed plants at least 20 units tall (`render::kLandTreeMinHeight`), at most three
kinds, each placed at least a quarter as often as the commonest and at most twice its height. (North Karana walls
its edges with RPINE1, 304 units tall by its 91-unit TREE5, which across the land stood as a wall of giants.)

**How drawn:** each is drawn once at load, on the CPU, into a picture from two sides (its own textures, the crown's far
side a little darker, 2 x 2 samples a texel, cover in alpha) on two crossed cards, cut out and two-sided, normals up
so they take the land's light. Thousands are drawn instanced, 4 triangles each, tinted by the median of the zone's
light on that tree's placements.

**Where:**

- from 256 units past the zone's edge out to 7000;
- as far apart as the zone's own trees stand over its level ground: a third of that in the land's woods (the darker
  patches its shade paints), one and a half times it elsewhere;
- fewer past 2500 (as 2500 over the distance);
- on the land's surface as its mesh draws it, 2 units sunk;
- not on the sea, the mountains' rock, slopes steeper than 0.85 or in the zone's pockets;
- at most 40,000, the nearest kept.

They cast no shadows and go with the land (`set_distant`).

**Measured** in the zone viewer (up to 0.7, with the neighbours' ground): Qeynos Hills 2,610 trees, Butcherblock
4,823, South Karana 13,949; placing takes 16 to 58 ms of the build. South Karana at 2560 x 1440: 1.29 ms a frame with
them, 1.22 without (178 more draw calls).

#### Horizon hills: grass and switches

Out to 300 units past the zone's terrain it grows the zone's grass (Zone flora, "Past the zone's edge").

`display.horizon_hills` (Options, Horizon Hills) turns it off, its trees with it; the viewer takes `--no-horizon`, and
`--no-land-trees` for the land without its trees.

## Sky zones

A zone that floats in the sky, with open sky under its ground (`render::horizon_open_below`: Plane of Sky `airplane`,
Plane of Air `poair`), gets in the modern lighting:

- **No land round it and no fog past its edge**: they would be ground where there is none.
- **Its own painted sky dome**, not the atmosphere (which goes into the haze below the horizon). The dome and its
  cloud layers are drawn again mirrored under the horizon, behind themselves (`SkyFrame::mirror_below`), so the sky
  reaches the nadir; past that the background is the open sky's colour straight up (`render::open_sky_below`), not
  the zone's fog.
- **The haze in the sky's own colour** at the horizon (`render::haze_target`, `in_the_sky`), whatever the zone's fog.
  PEQ's zone table gives Plane of Sky a black fog, which turned its far islands, and everything under them, black.

They are listed by name (docs/unknowns.md, World rendering): their sky is the plain one (sky 1 in PEQ's zone table),
so the server sends nothing that marks them, and nor does their shape: the share of open air between the zone's
pieces of ground was 14 % in Plane of Sky but 3 % in Plane of Air, against up to 6 % in the ordinary outdoor zones measured (and 27 to 35 % in the box round East Karana's, North Ro's and
Trakanon's level ground).

## Zone edge fog

In the modern lighting, outdoors and out of the water, a fog bank lies on the ground past the zone's terrain. At a
zone line the world fades into mist instead of ending, and the land round the zone (Horizon hills, above) is not seen
up close; its taller hills and mountains rise out of the bank far off.

Hailstone's own: the original drew nothing past a zone's geometry and hid that in its fog wall. It replaces drawing
the zones past the lines (up to 0.7), which showed walls, filler and seams there.

#### Zone edge fog: where

`render::zone_fog_field` (`render/zone_fog.h`) works on the land's field (`render::HorizonField`, its 64-unit
squares):

- each open square's distance to the zone's terrain;
- as the bank's floor, the zone's lowest ground by its edge: the nearest covered square's lowest terrain, or its
  water's surface over that, smoothed, and never over the edge's height the land starts from.

Not the edge's highest terrain: East Freeport's walls and cliffs lifted the bank hundreds of units over the open
ground and into the sky; now walls and cliff tops stand out of it. None on the zone's own squares or in a pocket its
terrain closes round. Packed into an RGBA8 texture (distance in steps of 16 units, floor over the zone's range of
floors); past the grid the distance goes on growing.

#### Zone edge fog: thickness

Density 0.0025 a unit (1000 units leave 8 % of the light; 0.006 until found a little strong), times:

- a smooth ramp from 0 at the zone's terrain to 1 at 1400 units past it;
- a top 250 units over its floor, thinning out over 100 either side of it.

Across open ground at a zone line it thickens until nothing shows; looking up the sky is clear (30 degrees up,
under 10 % hidden); from a hill one looks down on the bank's top.

#### Zone edge fog: never round the player

The camera is always in the zone, whatever the field says: no fog within 300 units of it, full by 900. Where the
field puts the camera past the zone's terrain (it is made from the terrain mesh alone, and East Freeport's ground by
its North Ro gate is placed objects), the fog is pushed out by that much and its floor kept under the camera's feet;
before, the camera stood in the bank and the sky above it was fogged.

#### Zone edge fog: broken up

Only in `shaders/zone_fog.frag` (the CPU functions give the mean).

The top rises and falls by up to 100 units over swells about 900 units across, and the thickness varies in patches
(0.45 to 1.55 times, about 360 across). Both drift slowly (6 and 2.5 units a second); the clock wraps every 4096 s,
when the mist shifts once.

#### Zone edge fog: drawing

`VkContext::set_zone_fog_field` and `set_zone_fog` drive a full-screen pass right after the scene, before the AO,
bloom and upscalers, so they all see the fogged picture.

- Each pixel's line of sight is marched from the camera to the depth (the sky's to 16000 units, at most the far
  plane) in 40 steps, closer together near the camera (t = length s^2), with a 4 x 4 ordered dither.
  It stops where under 0.1 % shows through.
- The colour is the haze's 1.35 times as bright (`kZoneFogLift`: the haze's own read a little dark as a mist close
  by), with its sun glow (as `apply_haze`), blended over the scene by what shows through.
- The field is uploaded when the zone's land is built (a new zone), the old one retired with the frames using it.

In North Karana at 1280 x 720 on an RX 9060 XT the pass took 0.21 ms of the GPU (the AO 0.26).

`display.zone_fog` (Options, Zone Edge Fog) turns it off; the viewer takes `--no-zone-fog`, and logs the field and the
distance past the zone's terrain at the camera.

## Smoothed ground

A classic zone's ground is drawn with smooth normals and curved, subdivided triangles instead of the file's flat
facets (`assets/ground_refine.h`): a deliberate difference from the original, the look only, in both lightings.

`Zone::terrain` stays as loaded for collision, the regions, the liquids' classification, the ambient probe and
everything else that must agree with the server's geometry. The refined copy goes into `Zone::draw_terrain` (with
`draw_terrain_source`, the terrain triangle each drawn one was cut from), used by the zone renderer and the zone
flora: the lawn and plants stand on it, and the flora's cover test sees it.

- **Ground:** the solid, opaque triangles of the classic (WLD) terrain meshes. Not cut-outs, translucent or invisible
  surfaces, passable ones (water), heightmap tiles or EQG meshes.
- **Normals:** corners are welded by position (to 1/128 unit) across meshes and materials; each corner of a triangle
  takes the angle-weighted average of the faces round it within 45 degrees of its own face. Hills turn smooth; a
  wall's foot, a cliff's edge or a building's corner stays sharp.
- **Shape:** each triangle becomes a cubic PN triangle over its corner normals (Vlachos, Peters, Boyd and Mitchell,
  "Curved PN Triangles", 2001); see below.
- **Offset:** an edge's control points are held within 1 unit of the flat triangle, so the curve stays within 1 unit
  of the ground that collision, feet and the server's spawn heights are measured on (Qeynos Hills: at most 0.88).
  Hills' outlines change little; the shading changes most (flat light and dark planes on hills and rock become smooth
  gradients).
- **Attributes:** UVs, both vertex colours (the baked light) and the detail passes are interpolated over the original
  triangle.
- **Cost:** about 40 ms more on Qeynos Hills' load. In the viewer at 1280 x 720 on the RX 9060 XT, a view over its
  hills went from 1.52 to 1.60 ms a frame to 1.74 to 1.77 ms (65,000 zone triangles drawn instead of 18,000, most of
  the difference in the shadow cascades).

`display.smooth_ground` (on by default, read as each zone loads) turns it off; the viewer takes
`--no-smooth-ground`. The log says how many triangles were refined into how many.

#### Smoothed ground: curved edges

- An edge is curved only where exactly two ground triangles meet on it and agree on both corners' normals.
- A crease, the ground's border and a seam with a wall or a cut-out stay the file's straight edge, uncut, so the
  refined ground meets the rest of the zone exactly.
- A curved edge is cut into 2, 4 or 8 segments, so a segment cuts through the curve by at most about 0.05 units.
- A triangle takes the most of its edges' counts (or what its own bulge needs), S. Points whose weights are all at
  least 1/S form a regular inner grid; each edge's samples are stitched to the grid's side along it.
- An edge's samples are counted and evaluated from its lower welded corner, so both triangles on it make the same
  points bit for bit and no crack opens where they cut it differently (Qeynos Hills: 9,655 ground triangles
  drawn as 127,293, the open edges the same 509 as the file's).

## Texture overrides

Replacement textures, such as upscaled packs, go in a pack folder under `textures/` of the Hailstone folder
(`docs/layout.md`):

`textures/default/` when `display.texture_folder` is empty, else the pack it names, or any folder by absolute path
(`off` for none; the viewer's `--textures DIR`). `assets/texture_overrides.h` indexes it at start.

A file stands in for the game texture of the same name whatever the extension or case (`KBARKD.BMP` <-
`kbarkd.png`). Which file wins:

- a file in a subfolder named after a zone (`textures/gfaydark/kbarkd.png`) is used in that zone first;
- elsewhere a file at the top of the folder wins over one in a subfolder;
- between files at the same depth, the first by path wins.

Any size works (the UVs are fractions of the texture).

- **Formats:** PNG (`assets/png_read.h`: every colour type and bit depth, tRNS, interlaced; zlib, no new library;
  at most 16384 on a side and 8192 x 8192 pixels in all, and refused when the header asks more than 1032 times the
  compressed data, so a forged header cannot make it allocate gigabytes), DDS, BMP and TGA.
- **Where:** zone geometry and placed objects, characters (including the layered textures), doors and ground items,
  the sky and spell particles. Not icons or the UI.
- **Orientation:** an override is taken as upright, as in an image viewer (`texdump` writes them so).
  `decode_game_texture` turns it the way the game's rows go: a real BMP's bottom-origin rows for the WLD meshes, a
  DDS's top-origin rows. The game's own BMPs are turned the same way, once, for every WLD mesh: zone, sky,
  characters, a zone's placed objects and the equipment archives' items (ground items, and `IT<n>` doors such as the
  bazaar campfire IT78 from gequip.s3d). The EQG models and zones use DDS only, whatever the name; their few real
  BMPs are the terrain's `e_TexturePalette0` maps, which are not drawn.
- **Cut-outs:** a masked material's override keeps its own alpha. A fully opaque one takes the game's transparent
  texels instead, scaled up.
- **Mips:** built from the override as for the game's textures.

An upscale costs video memory by its area: 4x on each side is 16 times the texels, uncompressed. The zone renderer
logs how many textures it replaced.

### Built packs (tools/texpack)

`texpack` and `tools/texpack/upscale.py` build an HD pack of every texture in the player's own asset folder. The
pack derives from the game's files, so it is built on each machine and never shipped.

Hailstone Texture Studio (projecthailstone/hailstone-texstudio, a tool add-on: docs/addons.md "Tools") does the same
in one windowed program, on any GPU through ncnn's Vulkan compute, with no Python. The rules below are its too:
`src/texpack` is shared by both (the scan, the upscale rules behind `texpack::Model`, the three-stage build and the
pack's files).

1. `texpack export ASSETDIR WORK` writes each distinct texture once, keyed by `texture_content_hash` of its file:
   61,474 in the RoF2 install (from 2,709 archives, in about a minute).
   - A member named `.bmp`, `.dds` or `.tga` is read whole; any other only when its first four bytes
     (`PfsArchive::read_prefix`, which inflates no more) are a DDS or BMP magic, so the scan does not inflate every
     mesh, WLD and sound.
   - Output: `WORK/src/<hash>.png` upright, with `<hash>.m.png` (the masked variant of an 8-bit BMP whose palette
     index 0 is used) and `<hash>.k.png` (the variant without its colour key of a texture painted on one,
     `remove_color_key`).
   - `WORK/manifest.jsonl` holds the names, archives, size, stored format and alpha kind.
   - A work folder exported before the keyed variants (2026-10-26) gets them by exporting into it again (the other
     files come out the same), then running `upscale.py` (it makes only what is missing) and `texpack build`.
2. `upscale.py WORK MODELS` (PyTorch through spandrel, on ROCm or CUDA) produces `WORK/up/`:
   - the colour at 2x: PBRify's 4x SPAN upscaler (CC0, made for 2000s game textures; it also removes DXT1 blocks
     and dithering), its result Lanczos-filtered down to 2x;
   - `<hash>.n.png` (PBRify NormalV3) and `<hash>.r.png` (PBRify RoughnessV2) from that colour.

   Details:
   - Tiling textures are wrapped by 16 texels before every model and cropped after, so no seam shows; character
     skins (`_chr` archives) are reflected instead.
   - The masked and keyed variants' transparent texels get their neighbours' colour first. A DDS's colour under
     alpha 0 is upscaled as stored, since opaque materials draw it. Cut-out alpha is scaled with Lanczos and
     thresholded.
   - Textures of 16 texels or less, the game's own normal maps and EQG data maps (coverage, palettes, masks) are
     only Lanczos-scaled and get no maps.
   - The built-in upscaler (`texpack::upscale_texture`) refuses a texture whose output, or its colour model's padded
     output, would pass 8192 x 8192 texels (`kMaxUpscaleTexels`): its float planes take 16 bytes a texel. The job
     notes the error and goes on.
   - A stopped run resumes.
   - `--redo-alpha STAMP` remakes the textures with an alpha channel whose outputs are older than the file STAMP, for
     a work folder upscaled while a DDS's colour under alpha 0 was still overwritten (packs built before 2026-10-25's
     fix show the hero's forge plates, `generic_plate_*_c.dds`, as flat dark colour). Touch STAMP once, run, then
     `texpack build` again; it re-encodes only the changed files.
   - On an RX 9060 XT the whole set takes about 7 to 11 hours (the DAT upscaler, a little sharper, is ten times
     slower).
3. `texpack build WORK PACK` writes `PACK/by-hash/<hash>[.m|.n|.r].dds` as DX10 DDS files with their full mip chains
   (bc7enc_rdo):
   - the colour as BC7, its mips from `build_color_mips` (level 0's colour under alpha 0 grown over only in the
     masked and keyed variants). `decode_game_texture` takes the keyed variant (`<hash>.k.dds`) for a material that
     reads the texture's alpha (`TextureOptions::reads_alpha`: the zone, object, sky and flora renderers), the plain
     file for an opaque one;
   - normals as BC5, the mips averaged and renormalised;
   - roughness as BC4.

   Each file is stored in the rows the WLD meshes read: a BMP source's are flipped, and its normal map's green
   inverted with them, so every pack normal map has green toward +v. PBRify's normals are DirectX's way, green
   down the picture.

Set up once (Linux, AMD):

    uv venv -p 3.12 venv && uv pip install -p venv/bin/python --index-url https://download.pytorch.org/whl/rocm7.2 torch torchvision
    uv pip install -p venv/bin/python spandrel pillow numpy

The models come from github.com/Kim2091/PBRify_Remix: `4x-PBRify_UpscalerSPANV4.pth`, `1x-PBRify_NormalV3.pth` and
`1x-PBRify_RoughnessV2.pth`, from its ComfyUI release zip.

At run time:
- A pack file with its own mips is used as stored (`decode_game_texture`), keeping its compression on the GPU.
- `by-hash/` files win over names: `w1` is a different image in each city. A server's changed file misses its
  hash, so it keeps its own look.
- A masked material takes `.m`.
- The maps (`decode_texture_map`) shade the classic effect of zones, placed objects and characters in the modern
  lighting:
  - the normal map on a cotangent frame from screen derivatives (the meshes have no tangents), with the surface's
    own normal kept for the shadows;
  - the sun's and moon's GGX highlight from the roughness map, taken into 0.5..1.
- `display.material_maps` (on by default) and the viewer's `--no-material-maps` turn the maps off.
- Every viewer, the model viewer too, takes `--textures`.

### Photo materials

A built pack can rebuild a classic zone's hand-painted ground from tiling photo textures (Poly Haven's, CC0) instead
of upscaling it (`texpack/materials.h`; Hailstone Texture Studio's "Photo ground materials"). The rules are data the
studio ships:

- `materials.txt` names each material: Poly Haven asset, metres one repeat shows, magnification, detail strength,
  how much of the original's painted shading it keeps, and its colour in the art.
- A file per zone (`qeytoqrg.txt`) gives each of the zone's textures one material or two
  (`hhdrockgrass1 = grass greyrock`; `sharp` for small shapes such as a path's flagstones).
- A zone file may set a material's colour in that zone's art (`colour grass = 0.30 0.27 0.12`), for its own tiles
  only: one material serves zones whose art differs (Qeynos Hills' green grass, Everfrost's dark tundra), and its
  photos are loaded once.

- **Scale:** each texture's world units per repeat are measured on the zone's ground (the area-weighted median over
  its triangles). A material repeats a power of two times across a tile, so Qeynos Hills' 56-unit grass tile and its
  108-unit transitions show the grass at one scale. An EQ unit is taken as a foot.
- **Two materials:** the original is split by a 2-means on its blurred chromaticity, seeded with the two materials'
  colours; brightness barely counts, so the art's painted shadows are not taken for grass. The photos are blended
  along that split, the edge moved by their displacement maps so it follows stones and soil.
- **Detail transfer:** the result keeps the original's colour above about 6 of its texels (1.5 for `sharp`) and takes
  only the photos' luminance detail below that, with part of the original's own painted shading. A rebuilt tile so
  keeps the zone's palette and still meets the tiles round it, rebuilt or not; blurs and resizes wrap at the edges.
- **Files:** the colour, normal and roughness maps go through the pack's encoder (BC7, BC5, BC4 with mips), at 512 x
  the pack's scale on a side (1024 for a 2x pack). A texture with alpha or a variant, or not square, is upscaled.
- **Resuming:** `PACK/materials.txt` records the rules each tile was made by; a build makes a tile again when its
  rules change, and gives a texture back to the upscaler when the rules drop it.
- **Fetching:** the 1K JPEG diffuse, `nor_dx`, rough and disp maps of each asset from Poly Haven's download host,
  once, into the studio's cache.

The studio has rules for the original game's 34 outdoor zones (not the expansions', not the revamped ones; in cities
the ground only). A texture that is also drawn with a cut-out somewhere (three of North Ro's) stays with the upscaler.
Seen in the zone viewer, not in a live session: the layout and seams hold; the photos' repeat still shows on large
flat areas.

### Model overrides

A texture pack can replace a zone's placed objects: `PACK/models/<zone>/<object>.glb`, a binary glTF 2.0 file named
after the object (`TREE1_ACTORDEF` or `TREE1`, any case), is drawn in its place (`assets/model_overrides.h`, read
with cgltf). The pack folder is the one `display.texture_folder` (the viewer's `--textures`) chooses; files are read
as each zone loads (`Zone::draw_objects`).

A deliberate difference from the original, and the look only: collision, picking, ladders and the ambient probe keep
the game's mesh, so where a player can walk does not change.

- **Frame:** glTF's own, Y up, in EQ units; glTF (x, y, z) is the zone's (x, -z, y). Front faces are
  counter-clockwise. Node transforms are applied, made top-down as the tree is walked. Primitives sharing accessors
  under one transform share vertices (found in a hash map), so load time is linear in nodes and primitives.
- **Materials:** a base colour texture is named by its image's URI, looked up as a replacement texture is (the
  zone's subfolder first, so a model's textures can sit beside it), any extension, with its `.n` and `.r` maps. `MASK` is drawn as a cut-out, `BLEND` at 50 %, `doubleSided` without culling. Images
  inside the file and buffers outside it are refused.
- **Made for a mesh:** a file whose `asset.extras.hailstone_source` holds the hash of the game mesh it was made for
  (`object_mesh_hash`, 16 hex digits) is used only on that mesh, so a game update that changes the object turns it
  off. One without (made by hand) is used on any object of that name in the zone.
- **Instanced:** each replacement mesh is kept once, in its own space, and drawn at every placement with a
  per-instance transform (`GpuInstance`: the transform's rows, the normal matrix's, the sway's scale and a tint). The
  buffer's first entry is the identity, used by the zone's own geometry, so one set of pipelines draws both. Each
  placement of each level is its own chunk; neighbouring placements in view go in one call. Qeynos Hills with the generated trees: 52,000 vertices and 146,000 indices on the GPU, from 3.7 million and
  8.3 million (266 MB of vertices) when every placement was a copy.
- **Light:** a placement's baked light (one colour per vertex of the game's mesh) is averaged into the instance's
  tint, which multiplies the replacement's own vertex colours (a generated tree's shade inside its crown).
- **Plants:** a replacement sways in the wind as the game's object would (`is_plant`): its cut-out parts flutter.
  One with levels of detail (a generated tree) does not flutter; its branches sway whole (`render::plant_sway`,
  `assets::Mesh::sway_heights`).
  - The generator stores each vertex's sway height in the second UV set (glTF TEXCOORD_1, asset extras
    `hailstone_sway`): a branch and its needle sprays get its root's height on the trunk; the trunk and far cards
    their own.
  - A sway by each vertex's own height, or a flutter, moved a branch's crossing cards apart, so up close its leaves
    seemed to flip back and forth (a third of the bend without the flutter still did).
  - A generated tree from a pack built before stands still.
  - Generated trees stood still for a while because a pine still flickered by its swaying branches. The cause was
    the upright card of the clump once at each branch's end (Styles, below); without the clumps the sway is calm.
- **Levels of detail:** a node's extras `{"hailstone_lod": [near, far]}` draw its meshes only from `near` to `far`
  world units from the camera (`MeshPart::lod_near`, `lod_far`). Each band's draws are kept apart, a chunk per
  placement, chosen by the camera's distance to the placement (`render/lod_bands.h`), so a whole tree changes level
  together. A model's levels are its bands in order of their near ends.
  - The scene and the depth pre-pass fade one level into the next across 20 % of the distance where they meet,
    centred on it. Both are drawn there; each of a pixel's samples shows exactly one
    (`coverage.glsl lod_fade_samples`: a sample mask by the placement's distance from `zone.vert` and a per-pixel
    noise), more of the next level the further the tree, so it dissolves instead of jumping. With 8x MSAA that
    is nine steps a pixel; without MSAA, a dither.
  - Shadows need less detail: the sun's two nearest cascades draw at least the second level (the camera's where
    coarser), the two further ones the last; a light's shadow cube, drawn once and kept, always the second.
  - Shadows do not fade; they take the camera's position of the frame before. A banded draw is not merged into the
    untextured shadow casters.
  - `display.tree_detail` (Options, Tree Detail Distance, 50 to 200 %) scales every band's distances.

### Generated trees

Hailstone Texture Studio's "Generated trees" regrows a classic zone's trees (`texpack/trees.h`) as model overrides
with their textures (BC7 with mips; the bark's normal and roughness maps as `.n` and `.r`). The
game's trees are a trunk of a few solid prisms under crossed cut-out cards. Each new tree is fitted to the game's:

- **Trunk:** the prisms' rings, found by hop count along the prism edges from each piece's bottom, make the
  skeleton; a limb that is a prism of its own, as TREETRY's second one, hangs from the ring nearest its root.
  The bark is a tube along it at 0.8 x the prism corners' distance: between the prism's faces and its corners, so a
  player stopped by the collision stands at the bark.
- **Leaves:** points spread over the cards, kept where the texture is opaque, give the crown's shape; turned round
  the trunk, they fill it. Colours are picked from the texture's opaque texels, then scaled to its mean, so an autumn
  tree keeps its yellows among its greens (shimmer fixes: below).
- **Styles:**
  - A pine (a single upright trunk up into leaves widest low down) grows whorls of drooping branches with two
    crossed needle sprays up a leader: whorls three units apart or more (9 to 18), four to six branches each, so the
    crown shows gaps. No clump of crossed cards at a branch's end: its upright card showed as a flat slab from beside.
  - A broadleaf tree grows branches into the crown by space colonisation (Runions, Lane and Prusinkiewicz 2007),
    with leaf clusters at their tips.
  - A willow hangs strands from its upper branches.
  - A dead tree (`dead`: Everfrost's and Dagnor's Cauldron's bare pines) grows a few long limbs from its upper trunk,
    rising and forking once, with sprays of drawn bare twigs at their ends.
  - A palm (`palm`) carries its trunk on up to near the top of the game's leaves (its solid prisms stop halfway; the
    cards paint the rest), then springs two dozen arching fronds: drawn narrow leaflets along a rib.
  - The rules may name the style (`tree tree71 = willow bark`); `auto` takes pine or broadleaf by shape.
- **Bark:** a Poly Haven photo material scaled to the game bark's mean colour, or a drawn one.
- **Levels of detail:**
  - Near: the tree, to twice its height (at least 200 units).
  - Middle: the same leaf cards, each one straight quad; about half the sides, no thin branches. Leaves do not change
    where levels meet: fewer cards, grown to keep the cover, flickered over a forest as the camera moved.
  - Far, from eight times its height (at least 600 units): three crossed cards and a level one, with a picture of
    the near tree from the side and from above (rendered by the generator: a small rasteriser, twice the size and
    averaged down).
  - The near and middle levels are built together, so they have one shape.
- **Cards cut to their leaves:** each quad of a leaf card strip is clipped, in texture space, to its texture's
  outline (`card_outline`: the convex hull of the texels that are not transparent, cut down to six corners
  by the cheapest cuts, six texels out for the smaller mips). A pine's card keeps about 42 % of its area, a broadleaf
  cluster's 74 %. It costs triangles (a quad is a fan of up to four) and saves pixels the alpha test would discard.

#### Generated trees: leaf shimmer

Each fix stops something that shimmered as the camera moved or a spray swayed:

- Gaps up to a few dozen texels inside a spray or cluster (3.5 % of the picture's side for needles, 3 % for leaves)
  are filled with a shadowed base (a closing of the cut-out), so a spray is cut out along its outer edge only. Thin
  holes showed the dark or the sky behind.
- A needle spray's two layers are close in shade; its twigs a darker shade of the needles, not brown. Stark stripes
  and bright twig lines read as streaks from afar.
- Each needle's shade goes only 0.4 of the way from the spray's mean towards its palette colour (the game textures'
  palettes reach near white); gaps are filled at 0.8 of the mean. Needles a few texels wide in stark light and dark
  changed every pixel they covered: a flicker by a tree, a shimmer through a forest.

In the zone viewer (frames 4 ms apart, pixels changing by more than 12): a flight over Greater Faydark went from 11 %
to 5 % (the game's own trees 3.5 %); standing by Lake Rathe's pines in the wind, 0.67 % to 0.14 %. Shadows, MSAA,
anisotropic filtering and levels of detail made little difference.

#### Generated trees: cost

Triangles a tree: about 1,600 to 7,200 at the near level (the tenth to the ninetieth percentile of the 129 classic
trees; 12,000 to 20,000 for the few biggest broadleaf trees), 1,000 to 3,000 at the middle, 8 at the far. Each kind
of tree is stored once (instanced), so the triangles cost no memory per tree.

In Greater Faydark at 3124 x 1366 with 8x MSAA and 4096 shadow maps, a forest view took 8.5 ms, 700,000 triangles a
frame from 1.1 million (10.0 ms with the denser pines and fewer middle cards before; 7.5 with the game's trees; zone
viewer, offscreen).

Figures from before that change:

- At the Qeynos Hills test spot the zone drew 56,000 triangles a frame (3,400 with the game's trees; the trees in a
  fade draw two levels), 35,000 looking over the zone from above, and 9,300 into the shadow map (7,300 with the
  game's trees).
- Cutting the cards doubled the leaf triangles and took about 5 % off the frame at 2560 x 1440 with 8x MSAA in a
  view full of trees (3.29 ms from 3.47; zone viewer, offscreen).

#### Generated trees: rules

Rules are lines of the studio's zone files: `tree <object> = <style> [<bark material>]`, the object a name of
letters, digits, `_` and `-` (it names the tree's files). The 34 zones with ground rules have them (129 trees in
the 30 that have trees). A tree comes out the same on every build (its seed is its zone and name).

Seen in the zone viewer, not in a live session. Open: leaves thin out in the smaller mips of a cut-out.

## The isometric camera's cut

In the isometric camera (`docs/isometric.md`), `zone.frag`, `object.frag` and `flora.frag` drop the pixels of what
stands between camera and player: `reveal_discard` in `shaders/lighting.glsl`, from
`SceneLighting`'s `reveal_origin`, `reveal_axis` and `reveal_shape` (`game/iso_occlusion.h`). Off, as in every other
camera, when `reveal_origin.w` and `reveal_roof.w` are 0.

Against a 4x4 ordered dither, it cuts either:

- a cone round the camera-to-player line, above the player's feet and nearer than them; or
- (`reveal_roof`) everything over a height within a flat radius of the player.

The shadow passes' shaders do not. That camera's click marker and hover ring are a second and third
`TargetRingRenderer`. The player's silhouette is a fourth actor pipeline (`shaders/actor_silhouette.frag`:
depth test LESS, no depth write, premultiplied blend, the opaque parts only), drawn before the actors.

## Name plates

#### Name plates: colours

- **NPCs:** the spawn's consider colour (`game::con_rgb` of `game::con_level`, as Hailstone's target plate), not
  this client's yellow (255, 220, 120). A deliberate difference: option `con_name_colour`, Options > Display,
  `hailstone.ini` `display.con_name_colour`, default on; off restores the yellow.
- **Mercenaries:** the original's amber (`#e1b511`, `game::mercenary_name_colour`).
- **Corpses:** grey. **Other players:** light blue.
- **Target:** its consider colour in both settings.
- **GMs:** green (`#1da64b`) as the original draws them (`game::gm_name_colour`, after
  PlayerClient::SetNameSpriteTint @0x58bf00), with no option. That is a spawn with the gm flag that is not a live NPC
  (type 1) and is neither anonymous nor roleplaying (the spawn's Anon field 0; `/anon` and `/roleplay` both drop
  it), the player's own GM character included.
  - A corpse is never green, a deliberate difference: the original keeps the green on a corpse that carries the
    flag. Stock EQEmu sends corpses without it, so only a server that sends it would show the difference.
  - A targeted GM keeps the green, this client's choice; the original fades the target's tint toward white.

The other colours are not the original's. PlayerClient::SetNameSpriteTint gives every NPC but a mercenary one fixed
tint, teal (`#17b8ad`), and only flashes the target's toward white. The con-coloured target plate is Hailstone's look
too (`draw_nameplates`).

#### Name plates: the tint's byte order

SetNameSpriteTint hands three bytes to the actor's `SetStringSpriteTint(const RGB*)` (CActorInterface vtable
+0x190, CHierarchicalActor::vf100 @0x10040f00 in EQGraphicsDX9.dll), in the order blue, green, red:

- eqlib declares the slot with its `RGB` type, whose bytes are Blue, Green, Red, Alpha (a little-endian 0xAARRGGBB,
  the D3D colour).
- MacroQuest2's caption colours call the same slot with con colours written as 0xAARRGGBB values (red 0xFFFF0000),
  and those names show in their con colours.
- The original's own tints agree: PvP names, bytes 14 14 a6, come out red; the PvP-team scheme makes the evil races'
  names orange-red (00 80 ff) and the elves' yellow (00 ff ff).

So the bytes 4b a6 1d are the green `#1da64b`, a mercenary's 11 b5 e1 are amber `#e1b511`, and the NPC tint ad b8 17
is teal `#17b8ad` (this client once read them red first and drew mercenaries light blue). The rof2decomp rewrite's
`set(r, g, b)` names the bytes in memory order, not red first. Which name sprites are drawn,
and their parts: docs/mechanics.md "Name sprites".

#### Name plates: hidden by geometry

The plates are a 2D overlay. Each name goes through a pipeline of ours (`app/nameplate_layer.h`,
`shaders/plate_depth.*`) that tests its pixels against the frame's scene depth (`VkContext::overlay_depth_view`, the
depth the AO reads): text behind a surface more than 3 units nearer than the plate's head is not drawn. This depth
test is Hailstone's own rendering rule.

- Walls, objects, doors and characters can occlude a name; a pole or wall edge affects only the covered pixels.
- The target's name is tested too, unless `target_plate_through_walls` (Options > Display "Target Name Through
  Walls", `display.target_plate_through_walls`, default off) is on. Then it shows through any geometry, a sight the
  original does not give.
- Hidden pixels are dropped; there is no ghost in the client. The viewer's `--plate-test-ghost A` can still draw the
  test names behind the scene with an alpha A (the shader's `extra.x`).

It replaced a CPU line of sight from the camera to the head point through the zone's collision mesh (terrain and
placed objects only), which hid a whole name, the target's too, when that point was blocked, and drew it over
everything when not.

#### Name plates: position

Every name stays at its projected head position: the plate's bottom edge 1.5 units above its model's top, so a
larger name or more lines grow upward. A rider's head is where it is drawn: its model's top in its mount's seat bone
frame (`ActorRenderer::rider_top`), not its spawn's place
on the ground. There is no stacking, distance fade, height timer or runtime tuning command in this version.

#### Name plates: text style

The text's look is Hailstone's choice (the original's name sprites have one fixed font), set in Options > Display
"Name Plates" (`game/nameplate_style.h`, hailstone.ini keys `display.plate_*`, kept per character
like the other display options). The defaults match the look before these options:

| Option | Key | Default | What it does |
|---|---|---|---|
| `plate_font_face` | `display.plate_font_face` | empty | A system font family (as /fontface lists them); empty or not found: the UI's font. |
| `plate_font_size` | `display.plate_font_size` | 15 | Points at 100 % UI scale, 8 to 48. |
| `plate_font_bold` | `display.plate_font_bold` | 0 | The face's bold file; a face with none is struck twice, a pixel apart. |
| `plate_edge` | `display.plate_edge` | 0 | 0 drop shadow one pixel down and right, 1 dark outline all round, 2 none. |
| `plate_far_scale` | `display.plate_far_scale` | 100 | A name's size at the plates' range (250 units), in %, 25 to 100. |
| `plate_tag_scale` | `display.plate_tag_scale` | 100 | The lines under the name (a guild, an NPC's owner line) in % of the name's size, 50 to 100. |

- `plate_font_size`: the names get a font of their own in the atlas at this size, so a large name is not an enlarged
  small one.
- `plate_edge` 1: the outline is thicker for larger names.
- `plate_far_scale`: a name's size falls evenly with the head's distance from the camera
  (`game::plate_distance_factor`). 100 keeps every name one size.
- A change rebuilds the font atlas at the next frame, as /fontface does.

#### Name plates: overlap

Overlapping names are drawn farthest first, so the nearer lies over the farther (a deliberate difference; the
original's name sprites sort by depth alone). The fade measures each name's box at its drawn size.

- A covered name fades to `game::kPlateCoveredAlpha` (0.1), by the largest share of its box one nearer box covers: nothing under `game::kPlateFadeFrom` (10 %), all of it from `game::kPlateFadeTo` (20 %), smoothstep
  between (`game/nameplate_overlap.h`).
- The alpha follows its goal smoothly (tau 0.3 s).
- The target is never faded and stays at its projected head at full alpha.
- Option `plate_overlap_fade` (Options > Display "Fade Overlapping Names",
  `display.plate_overlap_fade`, default on). Off, every name is drawn at full alpha, still nearer over farther.

The target's highlight (dark plate, edge in its colour, bobbing marker) is option `target_plate_highlight` (Options > Display "Highlight Target Name", `display.target_plate_highlight`, default on).
Off, the target's name is plain in its colour. If the pipeline cannot be made (a missing shader), the log says so and
the names are drawn over the scene.

#### Name plates: offline check

This uses the zone viewer, no server. It needs a GPU, a display and the game folder, so it is not in
`hailstone_tests`. `tools/probe_test.py --hailstone build/hailstone/hailstone --game-dir DIR` runs the viewer twice
in the Bazaar:

1. `--plate-test` draws test names (magenta) through the client's `NameplateLayer`, each placed by CPU rays into the
   zone (`app/plate_test.h`) so the scene's expected effect is known: in front of
   everything it covers, behind it all, or in between.
2. `--plate-test-no-depth` draws the same names with no depth test.

The script counts each name's magenta pixels in both screenshots: a front name must keep 95 %, a behind name lose
all but 2 %, a partial one lose some. The CPU side is in `tests/plate_test_test.cpp`.

## Detected settings

The first start fits the graphics to the computer (`render/graphics_tier.h`), so a weak machine does not start at
the highest settings. Before the settings are first read, the GPU in use and the CPU are sorted into a tier:

| | Tier |
|---|---|
| Discrete GPU, 6 GiB of its own memory or more (its largest device-local heap) | High |
| Discrete GPU, 3 to 6 GiB | Medium |
| Discrete GPU under 3 GiB | Low |
| Integrated GPU: Apple's | Medium |
| Integrated GPU: any other (Intel, AMD); a software renderer or a virtual GPU | Low |
| Then the CPU: under 4 threads | Low at most |
| Under 6 threads | Medium at most |

Low and Medium are the Options window's presets of those names (Graphics > Preset: shadows, anti-aliasing, ambient
occlusion, distance blur, view distance) plus a few costlier settings. High leaves all at default, so never picks
Ultra.

Low also sets classic lighting and no radial flora, and turns upscaling and frame generation off (their default), so
Detect turns off an FSR 3.1 the player picked. Each costs a fixed share of the frame at output size, whatever the
render scale; on an Intel Iris Xe tablet FSR 3.1 was far slower than FSR 1. Medium and High keep the player's choice
for those four (kept). On a Low computer, Options > Graphics notes that FSR 3.1 and frame generation are slow.

| Key | Low | Medium |
|---|---|---|
| `display.shadows` | 0 | 1024 |
| `display.msaa` | 1 | 2 |
| `display.ao` | 0 | 0 |
| `display.distance_blur` | 0 | (default) |
| `display.view_distance` | 1.5 | 2.0 |
| `display.light_shadows` | 0 | 4 |
| `display.render_scale` | (default) | (default) |
| `display.horizon_hills` | 0 | (default) |
| `display.volumetric_clouds` | 0 | (default) |
| `display.grass_density` | 50 | 75 |
| `display.tree_detail` | 50 | (default) |
| `display.upscaler` | off | (kept) |
| `display.frame_gen` | 0 | (kept) |
| `display.lighting` | classic | (kept) |
| `display.grass` | 0 (Radial Flora off) | (kept) |

The tier is written only to a new install's `hailstone.ini` (one with no `display.` key yet; the asset folder the
picker saves first does not count). An existing install keeps its settings. Either way the tier is saved as
`display.gpu_tier`, and detection does not run again while that key exists. The log names the GPU, its memory,
the CPU's threads and the tier. A character's settings file starts as a copy of `hailstone.ini`'s keys, so later
characters start at the tier too.

Options > Graphics > Detect (gamepad: Display > Graphics for this computer) detects again and applies the tier,
erasing each key it leaves at default. Anti-aliasing and the upscaler change at the next
start, as always.

The thresholds are a guess, not a measurement: a GPU's memory stands in for its speed, and an integrated GPU's heap
(the system's memory) says nothing, so every integrated GPU but Apple's is Low.

## Settings

Settings live in `hailstone.ini` and the Options window (`ui/modern/options.lua`, this client's own window: its
Display and Graphics tabs). The client's options by name are in `app/client_app_options.cpp`, the graphics ones
`display_option`.

- **Viewer flags** (`--zone`, `--model`): `--msaa`, `--tonemap`, `--exposure`, `--render-scale`, `--sharpness`,
  `--upscaler`, `--frame-gen`, `--screenshot-generated`.
- **Client flags:** `--lighting` (`classic`, `smooth`, `modern`; `none` is the viewer's alone), `--msaa`, `--tonemap`,
  `--exposure`, `--shadows`, `--light-shadows`, `--no-ao`, `--depth-prepass`, `--bloom`, `--render-scale`,
  `--sharpness`, `--upscaler`, `--frame-gen`, `--view-distance`. Each sets its `display.*` key (`app/display_flags.h`)
  for that run only, over hailstone.ini's and the character's values, never saved (`Config::set_run_override`). A
  change in the Options window takes over and is saved as usual.

"Start-up": read at start-up; "zone load": read as each zone loads. Bracketed names are doc sections or Options
labels.

| Key | Values | Default | |
|---|---|---|---|
| `display.msaa` | 1, 2, 4 | 4 | samples per pixel, at most 4 (`render::kMaxMsaa`; 8 and up drawn at 4), cut to what the GPU supports; off under `display.upscaler = taa`; start-up |
| `display.render_scale` | 33 .. 100 | 100 | % of the 3D view's size the scene is drawn at (note 1) |
| `display.upscaler` | off, fsr1, taa, fsr3 | off | note 2; start-up |
| `display.frame_gen` | true, false | false | a frame made between each two real ones (Frame generation); only with `display.upscaler` taa or fsr3; forces vertical sync, holds the game to half the refresh; applied at once |
| `display.sharpness` | 0 .. 1 | 0.5 | RCAS sharpening: FSR 1's below a render scale of 100, the temporal upscaler's or FSR 3.1's own at every scale; 0 none |
| `display.tonemap` | auto, none, agx, aces, soft | auto | auto: AgX (a little extra saturation) for modern, soft for smooth classic (Smooth classic lighting), none for classic |
| `display.exposure` | EV | 0 | scene scaled by 2^EV before the tonemap |
| `display.shadows` | 0, 256 .. 8192 | 2048 | texels per side of each shadow cascade; 0 off; modern only |
| `display.light_shadows` | 0 .. 8 | 8 | lights that cast shadows (Light shadows: the zone's, the carried light, spells); 0 off; modern only |
| `display.light_shadow_size` | 128 .. 1024 | 512 | texels per side of each face of a light's shadow cube |
| `display.view_distance` | multiplier | 2.5 | modern draws this many times further than the classic client (at most 12000 units), into haze; picking keeps the classic distance |
| `display.clip_plane` | 0 .. 20 | 15 | the original's ClipPlane: classic view distance (`game::compute_view_distance`), applied at once; `display.view_distance` multiplies it |
| `display.actor_clip_plane` | 0 and up | 50 | the original's ActorClipPlane: spawns past min((n x 5 + 25) x 2, the far plane) not drawn (`game::actor_clip_distance`) |
| `display.shadow_clip_plane` | 0 .. 100 | 50 | the original's ShadowClipPlane (`/shadows on <n>`) |
| `display.spell_effects` | 0, 1 | 1 | spell particles (`/showspelleffects`) |
| `display.dynamic_lights` | 0, 1 | 1 | spawns' own lights (`/dynamiclights`); zone load |
| `display.ao` | 0, 1 | 1 | screen-space ambient occlusion; modern only; viewer `--no-ao` |
| `display.depth_prepass` | 0, 1 | 0 | zone's opaque geometry depth-only first, then coloured with depth test equal (Depth Pre-pass); from the next frame (`ZoneRenderer::set_depth_prepass`; a zone builds both pipelines either way); modern only; viewer `--depth-prepass` |
| `display.distance_blur` | 0, 1 | 1 | far distance's slight blur; modern only |
| `display.horizon_hills` | 0, 1 | 1 | land round an outdoor zone past its edges (Horizon hills); modern only; viewer `--no-horizon` |
| `display.zone_fog` | 0, 1 | 1 | fog bank on the ground past an outdoor zone's edge (Zone edge fog, Zone Edge Fog); modern only; viewer `--no-zone-fog` |
| `display.volumetric_clouds` | 0, 1 | 1 | clouds marched through the air over plain skies, replacing the painted cloud layer and patches (Volumetric Clouds); modern only; viewer `--no-clouds` |
| `display.worker_threads` | -1, 0 .. 16 | -1 | frame's worker threads (Worker threads); -1 one fewer than hardware threads, at most 6; 0 none; start-up |
| `display.particle_offscreen_hold` | 0, 1 | 1 | an actor out of view (or past 500 units in classic lighting) emits no particles of its own (Particles; a deliberate difference); 0 emits everywhere, as the original; start-up |
| `display.parallel_record` | 0, 1 | 1 | scene and shadow layer draws recorded on worker threads into secondary command buffers (Worker threads); 0 on the main thread; start-up |
| `display.smooth_ground` | 0, 1 | 1 | classic zone's ground drawn smooth and curved (Smoothed ground); zone load; viewer `--no-smooth-ground` |
| `display.tree_detail` | 50 to 200 | 100 | how far replacement models (generated trees) keep nearer levels of detail, % of their own distances (Tree Detail Distance); viewer `--tree-detail` |
| `display.target_ring_cues` | 0, 1 | 1 | target ring's sweep on a new target, pulse while attacking (Target Ring Cues; The target ring) |
| `display.find_trail_style` | 0, 1 | 0 | Find paths: 0 gold ground line, 1 rainbow strands; Options > Display (gamepad: Interface), per character, immediate; docs/navigation.md "The trail" |
| `display.target_ring_xray` | 0, 1 | 0 | target ring's hidden part faintly through what stands in front (Target Ring Through Models) |
| `display.target_ring_color.<Section>` | RRGGBB | (TargetIndicator.ini's) | target ring's colour for a con, Trivial .. Deadly (Target Ring) |
| `display.target_ring_opacity` | 25 .. 200 | 100 | target ring's alpha, % of its con's |
| `display.target_ring_glow` | 0 .. 100 | 50 | how bright the target ring's core shines |
| `display.target_ring_max_size` | 0, 4 .. 40 | 0 | target ring's largest radius in world units, 0 off (Max Size; a deliberate difference) |
| `display.bloom` | 0 .. 1 | 0.5 | bright light spreading into its surroundings; modern only; viewer `--bloom N` |
| `display.brightness` | -0.2 .. 0.2 | 0 | added after the tonemap, in display values (the UI is not graded) |
| `display.contrast` | 0.5 .. 1.5 | 1 | around mid grey, after the tonemap |
| `display.gamma` | 0.5 .. 1.5 | 1 | after the tonemap; above 1 lifts the shadows |
| `display.color_grade` | 0, 1 | 1 | modern lighting's colour grade by time of day, weather and zone (Colour grade); viewer `--no-grade` |
| `display.lighting` | classic, smooth, modern | modern | low to high (Smooth classic lighting); `render/scene_lighting.h`; `/lighting` switches; `enhanced` read as modern |
| `display.window_mode` | windowed, borderless, fullscreen | windowed | borderless: a window over the whole display at the desktop's mode; fullscreen: exclusive at `display.resolution` |
| `display.resolution` | WxH | the desktop's | fullscreen mode (the display's closest); in a window, the Options window's Resolution sets the window's size |
| `display.vsync` | off, on, adaptive | on | present mode: immediate (else mailbox), FIFO, FIFO relaxed; FIFO when the surface lacks one |
| `display.max_fps` | -1, 0, 10 .. 1000 | 0 | frame cap; 0 none, -1 Auto (note 3) |
| `display.background_fps` | -1, 0, 1 .. 1000 | 0 | frame cap while another window has the focus (note 4) |
| `display.flora_distance` | units, 400 .. 1200 | 400 | how far flora is drawn (Radial flora) |
| `display.grass_density` | percent, 25 .. 300 | 100 | how thick Hailstone's lawn grows (`render::lawn_lod_blades`) |
| `display.zone_flora` | on, off | on | Hailstone's flora for zones with a rules file (Zone flora); zone load |
| `display.extra_shields` | on, off | on | a deliberate difference: shields RoF2 does not list (`game::is_shield_model`, docs/formats/characters.md "Held items") go on the shield point, not in the hand; start-up |
| `display.texture_folder` | pack name, absolute folder, off | `textures/default` | replacement textures (Texture overrides; note 5) |
| `display.fov` | fov units | none | field of view in 512ths of a circle (none: the classic 90, 63.3 degrees across below an aspect of 2); Options shows degrees, 45 .. 120 |
| `display.gpu_tier` | low, medium, high | (detected) | tier the first start found (Detected settings); while present, detection does not run; kept in `hailstone.ini` with a character's settings |
| `ui.scale` | percent, 0 | 0 | UI scale (0: the display's); start-up; `--ui-scale` takes precedence |

#### Settings: notes

1. `display.render_scale`: below 100 the upscaler (`display.upscaler`) brings it up (The frame); the UI stays sharp.
   Options > Upscale Quality's presets (`render::UpscaleQuality`): Native AA 100, Quality 67, Balanced 59,
   Performance 50, Ultra Performance 33 (FSR's ratios, 1.5x .. 3x); any other value shows as Custom.
2. `display.upscaler`:
   - off (the default on every computer): no upscaling or frame generation; the view is drawn at full size whatever
     `display.render_scale` and `display.frame_gen` say (both kept for later). Applied at once; a temporal upscaler
     already running stays as anti-aliasing until the next start.
   - fsr1: FSR 1 below a render scale of 100.
   - taa: temporal anti-aliasing and upscaling at every render scale, MSAA off (Temporal upscaling).
   - fsr3: the same with AMD FSR 3.1's upscaler (AMD FSR 3.1); taa where the GPU lacks what it needs.
3. `display.max_fps`:
   - On the login, server select, character select/creation and loading screens, including the last stretch of a
     zone-in, an unlimited cap, or Auto with no refresh rate, counts as 240. A set cap is followed exactly.
   - `--max-fps` takes precedence (`--max-fps auto` for -1).
   - -1 (Hailstone's own, the Options window's Auto) caps just under the refresh rate of the window's current display
     (`platform::Window::refresh_rate`, read each frame so it follows the window): refresh - refresh² / 3600, rounded
     down (`app::auto_frame_cap`: 59 at 60 Hz, 116 at 120, 138 at 144, 224 at 240). This keeps a variable-refresh
     display's frames inside its range, so vsync's queue never fills. No cap when the system gives no refresh rate.
   - Each frame starts one period after the last one's deadline, or at once when a long frame has passed it
     (`app::next_frame_deadline`). A late frame is not made up by running the next ones back to back, which made a
     later frame wait on the GPU.
   - The wait is `platform::precise_sleep_until`: the system's sleep (on Windows a high-resolution waitable timer) to
     1 ms before the deadline (0.5 ms on POSIX; 16 ms on Windows without such timers, before 10 1803), then a spin
     that yields between looks at the clock.
4. `display.background_fps`: 0 as `display.max_fps`; -1 nothing drawn (the Options window's "0"). Below 10 the loop
   runs on at 10 a second (the game clock catches up at most 250 ms a frame) and draws only that many frames, or none
   (`app/frame_pace.h`); a frame not drawn is run as a minimised window's is.
5. `display.texture_folder`: indexed at start, its files read as each zone loads. Set by the Options window's
   Textures section, the login screen's More options (both packs; from the next start) and Hailstone Texture Studio.

## Roadmap

1. **Frame foundation** (done): HDR scene target, MSAA with alpha-to-coverage, post pass with tonemap and dither.
2. **Lighting**:
   - done: (a) linear scene, the modern lighting below, AgX by default; (b) cascaded shadow maps for the sun and
     moon, cast by the zone's geometry and the characters; (c) screen-space ambient occlusion;
   - done: the zone's lights per pixel from a light grid, warm ones flickering; spells' light on everything round
     them; shadows from the zone's lights; placed objects (doors) as casters; world textures' mips built in linear
     light with alpha-weighted colour and cut-out coverage kept;
   - later maybe sRGB formats (filtering between texels in linear).
3. **Sky and atmosphere** (done):
   - a longer view distance with haze instead of the fog wall, the sky blended into it at the horizon, a slight
     distance blur, a physically based sky with stars and lit clouds;
   - weather (OP_Weather): rain, snow, lightning, wet ground and lying snow;
   - the moon's phases, thunder, rain and snow kept off covered ground;
   - a tilted sky (the sun rising east, standing south), the ambient and the haze's colour from the sky model, the
     haze eased between places and weathers with a storm fog band, weather transitions over tens of seconds, a
     flickering lightning.
4. **Water and lava** (done):
   - screen-space reflections, refraction, depth colour, shore foam, waves; lava glowing and pulsing;
   - under water: caustics, the water's colour and absorption, light shafts, Snell's window;
   - the sky as drawn mirrored where the screen holds it, EQG "noswap" environment cubes, falls as sheets, molten
     lava with crust, waterfalls without dark curtains;
   - next maybe planar reflections for what the screen does not hold.
5. **Post and content** (done): bloom; foliage wind; a texture override folder for upscaled packs; built HD packs
   with normal and roughness maps (tools/texpack); colour grading; TAA (item 6).
6. **Upscaling**:
   - done: a render scale with FSR 1 (EASU + RCAS);
   - done, the temporal groundwork: sub-pixel jitter on the projection, kept off picking and the nameplates; camera
     motion vectors from the depth; history reset on zone-in, camera cuts and resize; a TAA/TAAU of our own on it
     (`display.upscaler = taa`, Temporal upscaling);
   - done: motion written by the scene pipelines (last frame's bones, wind time and object matrices) and a reactive
     mask; FSR 3.1 (MIT) as a second temporal upscaler (`display.upscaler = fsr3`, AMD FSR 3.1); frame generation of
     our own on the engine's motion and depth (`display.frame_gen`, Frame generation);
   - MSAA is off while a temporal upscaler runs, so cut-outs lose alpha-to-coverage; particles, spells, water and lava
     need a reactive mask rather than motion vectors;
   - next a negative texture LOD bias; maybe a choice of more than one in-between frame for very high refresh rates,
     or AMD FSR 3.1's frame generation should ours fall short;
   - XeSS and FSR 4 are add-ons ("Upscaler providers"); DLSS is not planned.
