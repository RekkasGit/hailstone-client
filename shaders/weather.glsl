// The weather on what it falls on, in the modern lighting (render/weather_renderer.h WeatherLook; SceneLighting::weather
// x: wet, y: snow, z: rain falling, w: puddles): snow lying on what faces up, in drifts; the rain darkening what it
// soaks and letting it reflect the sky; water standing in the ground's low spots, ringed by the drops while it rains.
// None of it reaches under cover (sky_open, render/cover_map.h). Shared by the zone, its objects and the characters.
// After lighting.glsl.

float weather_hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float weather_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(weather_hash(i), weather_hash(i + vec2(1, 0)), u.x),
               mix(weather_hash(i + vec2(0, 1)), weather_hash(i + vec2(1, 1)), u.x), u.y);
}

// The sky seen in direction r: the horizon's colour (the haze's), a little deeper overhead.
vec3 weather_sky(vec3 r, vec3 horizon, vec3 sky) {
    return mix(horizon, horizon * 0.7 + sky * 0.3, smoothstep(0.0, 0.6, r.z));
}

// The snow lying at world point p on a surface facing n (0..1): on what faces up, in drifts that close up as more falls.
float snow_cover(float snow, vec3 p, vec3 n, float open) {
    float s = snow * smoothstep(0.55, 0.9, n.z) * open;
    if (s <= 0.0) return 0.0;
    float drift = weather_noise(p.xy * 0.15) * 0.6 + weather_noise(p.xy * 0.6) * 0.4;
    return s * smoothstep(0.75 - snow * 0.6, 0.95 - snow * 0.5, drift + 0.3);
}

// How much of a level surface at p stands under water (0..1): slow noise picks the low spots, which fill first and
// spread as the puddles grow, to about a quarter of the ground in a long storm.
float puddle_cover(float puddles, vec3 p, vec3 n, float open) {
    float level = smoothstep(0.93, 0.985, n.z) * open;
    if (puddles <= 0.0 || level <= 0.0) return 0.0;
    float low = weather_noise(p.xy * 0.045) * 0.65 + weather_noise(p.xy * 0.17 + 5.3) * 0.35;
    float edge = 0.86 - 0.24 * puddles;
    return level * smoothstep(edge, edge + 0.05, low);
}

// The rings the drops make on standing water: the surface's slope (dh/dx, dh/dy) at p, time t. Two jittered grids of
// cells about a unit across; a cell has a drop once a second or so at its own moment, more of them the harder it rains,
// and each ring spreads and fades. `footprint` (a pixel's size in world units) fades them out before they shimmer.
vec2 rain_ripples(vec2 p, float t, float rain, float footprint) {
    float fade = 1.0 - smoothstep(0.03, 0.1, footprint);
    if (rain <= 0.0 || fade <= 0.0) return vec2(0.0);
    vec2 slope = vec2(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        vec2 q = p * (layer == 0 ? 0.8 : 1.3) + float(layer) * 17.0;
        vec2 cell = floor(q);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                vec2 c = cell + vec2(x, y);
                float h = weather_hash(c), h2 = weather_hash(c + 7.1);
                if (h2 > 0.35 + 0.65 * rain) continue;
                vec2 centre = c + 0.5 + (vec2(h, h2) - 0.5) * 0.8;
                float age = fract(t * (0.8 + 0.3 * h) + h * 7.0);
                vec2 d = q - centre;
                float dist = length(d);
                float ring = dist - age * 1.1;
                float wave = sin(ring * 16.0) * exp(-ring * ring * 25.0);
                float life = (1.0 - age) * (1.0 - age);
                slope += d / max(dist, 1e-3) * wave * life;
            }
    }
    return slope * 0.12 * fade;
}

// A lit surface `color` (linear) under the weather at world point p, facing n, seen along `view` (to the eye); `snow_lit`
// is white snow lit there; `open` its sky_open. `horizon` and `sky` colour the reflections (weather_sky); `pools` lets
// water stand on it (the ground's level surfaces); `sheen` scales the wet look (the characters' is lighter).
vec3 weather_surface(vec3 color, vec3 snow_lit, vec3 p, vec3 n, vec3 view, vec4 weather, float open, vec3 horizon,
                     vec3 sky, float t, float footprint, bool pools, float sheen) {
    float snow = snow_cover(weather.y, p, n, open);
    if (snow > 0.0) color = mix(color, snow_lit, snow * 0.9);
    float wet = weather.x * mix(0.35, 1.0, smoothstep(0.55, 0.9, n.z)) * (1.0 - snow) * open * sheen;
    float pool = pools ? puddle_cover(weather.w, p, n, open) * (1.0 - snow) : 0.0;
    if (wet <= 0.0 && pool <= 0.0) return color;
    if (wet > 0.0) {
        vec3 r = reflect(-view, n);
        float fresnel = 0.04 + 0.96 * pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 5.0);
        color = color * mix(1.0, 0.6, wet) +
                weather_sky(normalize(vec3(r.xy, max(r.z, 0.02))), horizon, sky) * fresnel * wet * 0.7;
    }
    if (pool > 0.0) {
        // Standing water: the ground under it darker, the sky in it nearly a mirror's, broken by the rings, whose
        // crests catch the sky's light.
        vec2 slope = rain_ripples(p.xy, t, weather.z, footprint);
        vec3 wn = normalize(vec3(-slope, 1.0));
        vec3 r = reflect(-view, wn);
        float fresnel = 0.15 + 0.85 * pow(1.0 - clamp(dot(wn, view), 0.0, 1.0), 4.0);
        vec3 seen = weather_sky(normalize(vec3(r.xy, max(r.z, 0.02))), horizon, sky);
        vec3 water = color * 0.25 * (1.0 - fresnel) + seen * (fresnel * 1.7 + length(slope) * 5.0);
        color = mix(color, water, pool);
    }
    return color;
}
