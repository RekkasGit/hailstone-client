# Particles (EDD, spellsnew.eff, WLD particle clouds)

Code: `src/assets/spell_effects.{h,cpp}` (files), `src/game/particles.{h,cpp}` (stage loop, emitters), `src/render/particle_renderer.{h,cpp}` and `shaders/particle.{vert,frag}` (drawing), `src/app/spell_particles.{h,cpp}` (the game's cues). Tests: `tests/particles_test.cpp`.  Field meanings follow OpenEQ's spell effect format notes where not otherwise stated.

## EDD

The file starts with `"EDD\0"` and a 4-byte ASCII version. Version `110` is read directly: (size - 8) / 0x1a0 records; record 0 is the file's own `None` sentinel. Other versions are converted field by field and a file without the magic is the legacy headerless format with 0xc4-byte records; neither occurs in a RoF2 install (`spellsnew.edd` 2,577 records, `actoremittersnew.edd` 944, `environmentemittersnew.edd` 626) and `assets::parse_edd` refuses them.

The record layout is `assets::EmitterDefinition` (offsets in its comments). Texture names are searched in `SpellEffects\`, `EnvEmitterEffects\`, `ActorEffects\`. Fields +0x13c, +0x140, +0x178 and +0x148..+0x168 are not used.

## spellsnew.eff stages

268-byte records: a 64-byte name and three 0x44-byte stages (cast, projectile, impact), each a wave id and four slots `{emitter index, minimum level, location, attach}` (slot k of a stage is at `+0x44 + stage x 0x44 + 16k`). A spell's SpellAnim (spells_us.txt field 145) picks the record.

## Creating emitters

`game::spawn_stage_emitters` runs the slot loop: skip an index < 1 or a caster level below the slot's minimum; location 0 the given point, 1 the caster, 2 the missile (not modelled), 3 the target; the attach id picks one of the actor's spell points (below): 1 point 0; 2..7 points 1..6, or point 0 when the model lacks it; 8 the actor without a point; 9 / 10 the eyes (points 7 / 8; without them the slot is skipped); 11 the mouth (point 9); 12 the actor without a point at the chest's x and y (z: 0.1 above the actor's floor height, not taken here) with scale = the actor height; any other id skips the slot. A point the model lacks leaves the emitter on the actor alone. The key is `spawn id << 16 | spell id`. Density and opacity are eqclient.ini `[Defaults] SpellParticleDensity` and `SpellParticleOpacity` (clamped to [0, 1]; 1.0 with the flag argument).

`ParticleSystem::create` works as follows: density <= 0 creates nothing; the duration is `-EDD+0x78` when that is negative, else the requested ms x 0.001 when positive, else `+0x78`; it returns the duration in ms (the stage length is the longest). The capacity is `initial + per tick x (ticks/s x min(lifetime, duration) + 1)`, replaced by +0x144 when set, at most 100,000.

## Spell points

An actor has ten named spell points: 0 `SPELLPOINT_DEFAULT`, 1 `_CHEST`, 2 `_HEAD`, 3 `_LHAND`, 4 `_RHAND`, 5 `_LFOOT`, 6 `_RFOOT`, 7 `_LEYE`, 8 `_REYE`, 9 `_MOUTH`. A point (0xa4 bytes) is a name[64], a bone name[64], an offset, a rotation (heading, pitch, roll, 512 per turn) and a scale (`assets/model_points.h`):

- **EQG models:** `<model>.pts` beside the mesh in its archive (`EQPT`, u32 count, u32 version 1, the records; the same loader also reads `<model>.prt`, the actor emitters). dkm.pts: DEFAULT on ROOT_BONE, CHEST on CHEST_CHEST03, HEAD on HEAD_HEAD 0.25 out, the hands on ARML_FARM / ARMR_FARM 1.3 out, the feet, both eyes and the mouth on HEAD_HEAD, and a DAMAGE_POINT.
- **WLD models** (the code being bone 0's first three letters): DEFAULT on bone 0; CHEST on `<code>CHEST_POINT_TRACK`, else `<code>CH_TRACK`; HEAD and MOUTH on `<code>HEHEAD_TRACK` (the mouth at (0.08, 0.6, 0) turned (128, 0, 128): the Luclin models), else on `<code>HEAD_POINT_TRACK` with the mouth's `XOffset` .. `ZScale` from `Resources\SPOffsets.ini` `[<code>]` (defaults 0.6, 0, 0, 0, 0, 0, 1, 1, 1); the hands on `<code>L_POINT_TRACK` / `R_POINT_TRACK`, else `<code>SPELL_POINT_TRACK`; the feet on `<code>BOFOOTL_TRACK` / `BOFOOTR_TRACK`, else `<code>BO_L_TRACK` / `BO_R_TRACK`. No eyes.

A point's bone is the first with exactly its name ; its matrix (`assets::point_matrix`) is the scale, then the rotation built from the table sine and cosine of (heading, -pitch, roll), then the offset, all in the bone's space. Each frame the emitter update puts an emitter that has a point at the point's world position every frame, and one on an actor without a point at the actor's position; with EDD +0x60 set its axes turn with the point (here `emitter_axes` of the point's axes and the definition's turns). The client takes the bone poses the actor renderer last drew (`ActorRenderer::drawn_bone`); the model viewer's `--spell-effect N [--spell-stage S]` replays a stage on its models.

## Actor emitters

An EQG model's `<model>.prt` (beside its mesh and `.pts`) lists emitters of `ActorEmittersNew.edd` on its points (`assets::parse_prt`): `PTCL`, u32 count, u32 version 1..5, records of 0x5c, 0x60, 0x64, 0x68 or 0x6c bytes that the loader widens to 0x6c (the missing fields: floor 0, variant -1, +0x64 0, alternate = the emitter). Each record keeps: the emitter (+0x00), the point name (+0x04), a trigger (+0x44: 0 always on, 1 started by the animation at +0x48), +0x4c, +0x50, a delay (+0x54, ms), a duration (+0x58, 5000 in every shipped file), a floor flag (+0x5c: drops the emitter to the floor below the point), a variant (+0x60: -1 all; those whose variant is -1 or the actor's are made) and an alternate emitter (+0x68, taken when the particle system asks for it).

When an actor is made, each always-on record whose point exists and whose variant is below 1 makes its emitter on the point, with density and opacity from the particle system, scaled by the actor's radius. Held items do the same on their own points: an item's points hang from `ATTACH_TO_ORIGIN`, its origin, which is the holding bone's frame.

The client (`app/actor_emitters.h`): the dressed mesh carries the always-on records of the EQG body and of its held items (`assets::MeshEmitter`; EQG held items are drawn too, `CharacterLibrary::item_mesh`); each drawn actor keeps a persistent emitter per record, placed every frame on the point as the actor renderer posed it; eqclient.ini `ActorParticleDensity` / `ActorParticleOpacity`. 12,449 always-on records in the shipped files point into its `ActorEmittersNew.edd` (945 records), 315 past its end (e.g. IT12753's 1059), which make nothing.

The trigger 1 records whose animation is not STND or IDLE (1,020 run-dust records on animation 27 NRUN, footfalls,
swings, casts) are started by the client each time their animation (the table row's old-style clip) starts or loops
on the actor, after +0x54, emitting for at most 1.5 s; the original's timing is not traced (docs/graphics.md,
Particles). The billboard mode +0x70 is drawn as docs/graphics.md says.

## Simulation and drawing

As OpenEQ describes the fields: the basis is (+X, -Y, +Z) for (transverse 1, transverse 2, axial) with an identity actor matrix; shapes 0..9 (point, even ring, sphere, cylinder side, disk, sphere surface, box surface, cone side, torus, random ring); axial and transverse velocities with accelerations, radial and orbital motion, world gravity (+0x128) and X wind (+0x12c); counts scale with the density; opacity `min(fade in x fade out, max opacity) x opacity`; size ramps for grow and shrink; colour from start to end over the particle's life; flipbook grids 2x2 (2..4 frames), 4x2 (5..8), 4x4 (9..16). Quads face the camera; +0x68 = 1 blends with DESTBLEND ONE, otherwise INVSRCALPHA; +0x64 = 1 turns depth writes off.

The box surface (shape 6) as the graphics DLL places it (its shape switch @0x100737be): two random numbers from its signed table (-1..1) for the two free axes, then the face from its unsigned table (0..1) times 6.0 rounded to nearest (fistp), so faces 0..4 are -x, +x, -y, +y, -z and 5 or 6 is +z (the -x face is half as likely as the others, +z one and a half times). The DLL's random numbers are two shuffled tables (2000 values i x 0.001 - 1, 1000 values i x 0.001); the client draws from its own generator.

## In the game

OP_BeginCast plays the cast stage at the caster for the packet's cast time; OP_Action 231 plays the impact stage with the target as caster and target. The zone viewer's `--spell-effect N` plays effect N's cast and impact stages 25 units in front of the camera every 4 s.

## WLD particle clouds (torches, lanterns, braziers)

Code: `assets/wld.h` (`ParticleCloud` 0x34, `ParticleSprite` 0x26), `assets/wld_particles.{h,cpp}` (the conversion), `assets::wld::skeleton_particles` and `load_classic_zone` (`Zone::particle_definitions`, `Zone::particle_fixtures`), `game::ParticleSystem::add_fixture`, `game::emitter_axes`, `game::emission_distance_factor`, `app/zone_particles.{h,cpp}` (client and viewer). Tests: `tests/wld_particles_test.cpp`. The format and placement follow OpenEQ's WLD particle notes.

The classic object archives draw flames and smoke as particle clouds, not meshes: a skeleton node's sprite reference can name a 0x34 instead of a 0x2d mesh. Bazaar's lanterns `HAVSTLIGHT300`/`301` (one `L307_PCD` flame each), `HAVSTLIGHT302` (two) and wall torches `FTORCH301` (`L301_PCD` flame, `CSMOKE_PCD` smoke on a child node) are 53 placements and 83 emitters; PoK has 475. All 826 clouds in the shipped WLDs have flags 4 and an 84-byte body; 567 have mode 3 with bits 0x30500 (the survey: modes 1..4, bits 0x0..0x38580).

0x34 body (after the name): u32 flags, selector, mode, bits, capacity; f32 x4 (+0x14 gravity, three more); u32 duration ms (+0x24); f32 radius (+0x28), spread angle (+0x2c, 512ths); u32 life ms (+0x30); f32 speed (+0x34); f32 x3 direction (+0x38..+0x40); u32 interval ms (+0x44); f32 size (+0x48); u32 colour (+0x4c, B, G, R in the low bytes); then flag 0x01 two float triples, 0x02 24 bytes, 0x04 the 0x26 reference. The reference word is resolved in full, and the first definition of a name wins (later same-name clouds, such as the bazaar's at fragments 609..750, reuse it).

0x26 body: flags (0), a 0x05 reference, the material word (0x80000017 everywhere). The texture is the 0x05 -> 0x04 -> 0x03 chain's first file (GENG00.DDS, CSMOKE.DDS, in `<zone>_obj.s3d`).

The converter fills the same 0x1a0-byte descriptor as an EDD record (`assets::particle_cloud_definition`):

| Descriptor | From |
| --- | --- |
| +0x13c, +0x140 | selector, bits |
| +0x74 follow | 1 when bits & 0xc000 |
| +0x6c actor scale | 1 when bits & 0x10000 or following |
| +0x60 | 1 (owner basis) |
| +0x144 capacity, +0x128 gravity | capacity, +0x14 |
| +0xa8 shape | mode 0 -> 6 box (size = half the flag-1 ranges' extent), 1 -> 5 sphere (radial speed 0.25 x speed .. speed), 2 -> 9 ring (radial = speed), 3 -> 4 point (0.1 disk with bit 0x80), 4 -> 4 disk, 5 -> 9 ring (radius +0x28 for 1, 2, 4, 5); modes 0, 4, 5 move along direction x speed in (transverse 1, transverse 2, axial) |
| mode 3 | axial speed = speed; radial 0 .. speed x tan(+0x2c) (table lookup: 512 per turn, index truncated and masked to 0..127); the axis: direction (1,0,0) -> +0xc4 = 128, (0,0,-1) -> 256, (-1,0,0) -> -128, (0,1,0) -> +0xc8 = -128, (0,-1,0) -> 128 (exact compares in that order), else +Z |
| +0x78 duration | -(ms x 0.001), -9999 for 0 |
| +0x7c lifetime | ms x 0.001 |
| bits & 0x100 | +0x94 fade out = lifetime, +0x170 = +0xa4 max opacity = 1; without it 0.5 and no fade |
| +0x84 per tick, +0x88 ticks/s | 1, 1000 / interval |
| +0x16c | size (the update multiplies the quad by it; 1 in every shipped EDD record) |
| +0xd4..+0xe8 colours | start = end = R, G, B |
| +0x130, +0x134 | max(texture frames, 1), 1000 / timing (timing < 2 counts as 100) |
| +0x68 additive | material (negative: through the alias table; 0x80000017 -> 0x011b0507) with bit 24: bit 20 |
| +0x64 no z-write | 1 |
| name "TFIRE2*" | lifetime 0.08, capacity 8, radius 0.05, 33.3 ticks/s |

Placement: each placed actor's particle node owns one emitter. Its origin is the node's world position (placement x rest pose). Its basis: the update takes the emitter axes from the node's world rows 2, 0, -1 as (axial, transverse 1, transverse 2); +0xc8 turns about transverse 1 (-128 brings the axis onto the node's +Y: final axes rows 1, 0, 2) and +0xc4 about transverse 2 (`game::emitter_axes`; the sign of the +0xc4 turn is inferred). The lanterns' and torches' flame nodes turn +Y up, so the flames rise. The owner scale (the node matrix's first row length: bazaar's lanterns stand at scale 1.5) scales motion and size when +0x6c is set. Emission scales with distance: rate x max(min(1, +0xa0 / distance), min(1, 16 / capacity)). The fixtures are persistent, created as for a 9999 s request: their duration and capacity follow from that by the rules above, so an environment emitter's EDD record with duration 0 holds a lifetime of particles (potranquility's `arc_brazier`, 6 per tick at 10/s for 5 s: 307), not one tick's (7). Spell density and opacity do not apply.

Drawing: additive (SRCALPHA, ONE), no depth writes, alpha test 1, no culling, no fog . The quad corners carry v = 1 at the top and 0 at the bottom, so the texture's last row is at the top (`ParticleRenderer::set_rows_bottom_up`): GENG00's teardrop then has its tail up, like a flame. Textures come from the zone's archives first (`set_texture_reader`).

Not modelled: the node-scale suppression below about 0.05 (no shipped particle node is scaled); radial motion only for visible emitters and the object-preview context; several-bitmap sprites as flipbooks (only the first bitmap is drawn); the gequip/character clouds. EQG zones' environment emitters (`<zone>_EnvironmentEmitters.txt` over `environmentemittersnew.edd`) are drawn as fixtures too (`assets::parse_environment_emitters`, `assets/zone_fires.h`; docs/graphics.md).
