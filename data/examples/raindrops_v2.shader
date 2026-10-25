// =====================================================================================
//  Rain on glass - particle version (uses the stateImage pass)
// =====================================================================================
//
//  Every drop is a particle: drops hit the glass, big ones start sliding, leave small
//  drops behind, absorb the drops in their way and wipe the fog and fine drizzle,
//  which slowly cover the glass again.
//
//  Setup
//  - Add "User-defined shader" to the source that should be "behind the glass",
//    enable "Load shader text from file" and select this file.
//  - Pick a "Preset". "Custom" uses the sliders in the "Custom preset" group.
//    The "Look", "Drops" and "Fog" sliders work with every preset.
//  - The fog grows back at the drizzle speed of the preset, so sliding drops leave
//    clear paths: short ones in "Storm", long ones in "Fallout".
//  - To clear the glass, click "Reload effect".
//  - The simulation pauses while the source is hidden.
//
//  Sizes are in pixels of a 1080 px high frame and scale with the source, so the
//  rain looks the same at any resolution. Speeds follow delta_time (same at 30 and 60 FPS).
//
//  State texture layout (RGBA32F, STATE_SCALE of the output):
//  - rows [0, R)   drop field A: x, y, radius, momentum
//  - rows [R, 2R)  drop field B: momentum x, spread x, spread y, shrink
//  - rows [2R, 3R) drop field C: last spawn, next spawn, parent index + 1, is new
//  - rows [3R, H)  glass map: up to 6 nearby drop indices + fog/drizzle coverage
//  where R = ceil(MAX_DROPS / state width). A drop with radius 0 is a free slot.
// =====================================================================================

#define STATE_SCALE 0.25
#define STATE_FORMAT RGBA32F

#define MAX_DROPS 1200
#define GLASS_H 1080.0
#define COLLISION_RADIUS 0.65
#define COLLISION_RADIUS_INCREASE 0.01
#define SPAWN_TOP -0.1
#define SPAWN_BOTTOM 0.95
#define MAX_MOMENTUM 40.0
// Only drops that really slide wipe the fog, not the small push of a landing drop.
#define WIPE_MOMENTUM 2.0
// Drizzle amount 50 refills a wiped path in about 5 seconds.
#define DRIZZLE_REFILL 15000.0
// Two indices per channel: a * INDEX_PACK + b, exact in 32-bit float.
#define INDEX_PACK 4096.0
#define EPOCH_COUNT 63.0

#define PRESET_CUSTOM 0
#define PRESET_RAIN 1
#define PRESET_STORM 2
#define PRESET_FALLOUT 3
#define PRESET_DRIZZLE 4

uniform float delta_time;
uniform int frame_count;

uniform int Preset<
    string label = "Preset";
    string widget_type = "select";
    int option_0_value = 0; string option_0_label = "Custom (sliders below)";
    int option_1_value = 1; string option_1_label = "Rain";
    int option_2_value = 2; string option_2_label = "Storm";
    int option_3_value = 3; string option_3_label = "Fallout (long trails)";
    int option_4_value = 4; string option_4_label = "Drizzle";
> = 1;

// ===================== CUSTOM PRESET =====================
uniform float Min_Radius<
    string label = "Drops: Min size";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 4.0; float maximum = 40.0; float step = 0.5;
> = 10.0;
uniform float Max_Radius<
    string label = "Drops: Max size";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 10.0; float maximum = 80.0; float step = 0.5;
> = 40.0;
uniform float Rain_Chance<
    string label = "Rain: Chance per frame";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 1.0; float step = 0.01;
> = 0.35;
uniform float Rain_Limit<
    string label = "Rain: Max new drops per frame";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 10.0; float step = 0.5;
> = 6.0;
uniform float Drizzle<
    string label = "Drizzle: Amount (how fast fog and droplets come back)";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 100.0; float step = 1.0;
> = 50.0;
uniform float Drizzle_Size<
    string label = "Drizzle: Max droplet size";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 2.0; float maximum = 8.0; float step = 0.1;
> = 4.5;
uniform float Trail_Rate<
    string label = "Trail: Drops left behind (rate)";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 5.0; float step = 0.1;
> = 1.0;
uniform float Trail_Size<
    string label = "Trail: Drop size (of the sliding drop)";
    string group = "Custom preset";
    string widget_type = "slider";
    float minimum = 0.1; float maximum = 0.6; float step = 0.01;
> = 0.35;

// ===================== LOOK =====================
uniform float BG_Blur<
    string label = "Look: Background blur (px)";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 60.0; float step = 0.5;
> = 10.0;
uniform int Blur_Samples<
    string label = "Look: Blur quality (samples)";
    string widget_type = "slider";
    int minimum = 8; int maximum = 48; int step = 1;
> = 24;
uniform float Min_Refraction<
    string label = "Look: Refraction of small drops (px)";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 512.0; float step = 1.0;
> = 128.0;
uniform float Max_Refraction<
    string label = "Look: Refraction of big drops (px)";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 1024.0; float step = 1.0;
> = 512.0;
uniform float Brightness<
    string label = "Look: Brightness inside drops";
    string widget_type = "slider";
    float minimum = 0.5; float maximum = 1.5; float step = 0.01;
> = 1.04;
uniform float Alpha_Multiply<
    string label = "Look: Edge sharpness";
    string widget_type = "slider";
    float minimum = 1.0; float maximum = 40.0; float step = 0.5;
> = 10.0;
uniform float Alpha_Subtract<
    string label = "Look: Edge threshold (higher = smaller drops)";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 20.0; float step = 0.1;
> = 3.0;

// ===================== DROPS =====================
uniform float Rim<
    string label = "Drops: Dark rim";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 1.0; float step = 0.01;
> = 0.25;
uniform float Highlight<
    string label = "Drops: Highlight";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 1.0; float step = 0.01;
> = 0.2;

// ===================== FOG =====================
uniform float Fog<
    string label = "Fog: Amount (wiped by sliding drops)";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 1.0; float step = 0.01;
> = 0.8;
uniform float Fog_Blur<
    string label = "Fog: Extra blur (px)";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 60.0; float step = 0.5;
> = 20.0;
uniform float Fog_Start<
    string label = "Fog: Amount on a fresh glass";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 1.0; float step = 0.01;
> = 1.0;
uniform float4 Fog_Color< string label = "Fog: Tint color"; > = {0.85, 0.9, 0.95, 1.0};
uniform float Fog_Tint<
    string label = "Fog: Tint amount";
    string widget_type = "slider";
    float minimum = 0.0; float maximum = 1.0; float step = 0.01;
> = 0.15;

uniform bool Debug< string label = "Debug: show water map"; > = false;

// Clamped reads of the source, so blur and refraction near the edges don't pull in black.
sampler_state clampSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

// Exact texel reads for the state data.
sampler_state stateSampler {
    Filter = Point;
    AddressU = Clamp;
    AddressV = Clamp;
};

// ===================== PRESETS =====================
struct Weather {
    float min_r;
    float max_r;
    float rain_chance;
    float rain_limit;
    float drizzle;
    float drizzle_min;
    float drizzle_max;
    float trail_rate;
    float trail_min;
    float trail_max;
};

Weather makeWeather(float min_r, float max_r, float rain_chance, float rain_limit, float drizzle,
                    float drizzle_max, float trail_rate, float trail_min, float trail_max)
{
    Weather w;
    w.min_r = min_r;
    w.max_r = max(max_r, min_r + 1.0);
    w.rain_chance = rain_chance;
    w.rain_limit = rain_limit;
    w.drizzle = drizzle;
    w.drizzle_min = 2.0;
    w.drizzle_max = max(drizzle_max, 2.0);
    w.trail_rate = trail_rate;
    w.trail_min = trail_min;
    w.trail_max = max(trail_max, trail_min);
    return w;
}

Weather getWeather()
{
    //                         minR  maxR  chance limit drizzle size  trail tmin  tmax
    if (Preset == PRESET_RAIN)
        return makeWeather(10.0, 40.0, 0.35, 6.0, 50.0, 4.5, 1.0, 0.2,  0.35);
    if (Preset == PRESET_STORM)
        return makeWeather(20.0, 45.0, 0.55, 6.0, 80.0, 6.0, 1.0, 0.15, 0.3);
    if (Preset == PRESET_FALLOUT)
        return makeWeather(10.0, 40.0, 0.35, 6.0, 20.0, 4.5, 4.0, 0.2,  0.35);
    if (Preset == PRESET_DRIZZLE)
        return makeWeather(10.0, 40.0, 0.15, 2.0, 10.0, 4.5, 1.0, 0.2,  0.35);
    return makeWeather(Min_Radius, Max_Radius, Rain_Chance, Rain_Limit, Drizzle, Drizzle_Size, Trail_Rate,
                       Trail_Size * 0.6, Trail_Size);
}

// ===================== HELPERS =====================
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float4 hash42(float2 p)
{
    float4 p4 = frac(float4(p.x, p.y, p.x, p.y) * float4(0.1031, 0.1030, 0.0973, 0.1099));
    p4 += dot(p4, p4.wzxy + 33.33);
    return frac((p4.xxyz + p4.yzzw) * p4.zywx);
}

// Random value for a drop (or -1 for global values) in this frame; the frame number is split
// so the hash inputs stay small enough for float precision.
float rnd(float id, float salt)
{
    float lo = float(frame_count % 4096);
    float hi = float(frame_count / 4096);
    return frac(hash12(float2(id + salt * 977.0 + 3.0, lo + 0.5)) + hash12(float2(hi + salt * 13.0, 7.3)));
}

// Rates are per 1/60 s step; a long frame counts as at most two steps.
float stepScale()
{
    return clamp(delta_time * 60.0, 0.0, 2.0);
}

float glassWidth()
{
    return GLASS_H * uv_size.x / max(uv_size.y, 1.0);
}

// The state covers the whole filter output; this also handles expanded borders.
float2 outputUV(float2 uv)
{
    return (uv - uv_offset) / uv_scale;
}

int stateWidth()
{
    return max(int(state_size.x + 0.5), 1);
}

int bandRows()
{
    int w = stateWidth();
    return (MAX_DROPS + w - 1) / w;
}

float2 texelUV(int x, int y)
{
    return (float2(float(x), float(y)) + 0.5) / state_size;
}

float4 readPrev(int x, int y)
{
    return previous_state.SampleLevel(stateSampler, texelUV(x, y), 0.0);
}

float4 readCur(int x, int y)
{
    return state_texture.SampleLevel(stateSampler, texelUV(x, y), 0.0);
}

// ===================== DROPS =====================
struct Drop {
    float4 a; // x, y, r, momentum
    float4 b; // momentum x, spread x, spread y, shrink
    float4 c; // last spawn, next spawn, parent index + 1, is new
};

Drop loadPrevDrop(int i)
{
    int w = stateWidth();
    int rows = bandRows();
    int x = i % w;
    int y = i / w;
    Drop d;
    d.a = readPrev(x, y);
    d.b = readPrev(x, y + rows);
    d.c = readPrev(x, y + rows * 2);
    return d;
}

float4 loadCurDropA(int i)
{
    int w = stateWidth();
    return readCur(i % w, i / w);
}

float4 loadCurDropB(int i)
{
    int w = stateWidth();
    return readCur(i % w, i / w + bandRows());
}

Drop deadDrop()
{
    Drop d;
    d.a = float4(0.0, 0.0, 0.0, 0.0);
    d.b = float4(0.0, 0.0, 0.0, 0.0);
    d.c = float4(0.0, 0.0, 0.0, 0.0);
    return d;
}

bool isAlive(Drop d)
{
    return d.a.z > 0.0;
}

// Momentum after this frame's random gravity kick. Deterministic, so any texel can recompute it.
float kickedMomentum(Drop d, float id, float ts, Weather w)
{
    float m = d.a.w;
    float delta_r = w.max_r - w.min_r;
    if (rnd(id, 1.0) < (d.a.z - w.min_r) * (0.1 / delta_r) * ts)
        m += rnd(id, 2.0) * (d.a.z / w.max_r * 4.0);
    return m;
}

bool wantsTrail(Drop d, float id, float ts, Weather w)
{
    if (!isAlive(d) || w.trail_rate <= 0.0 || w.rain_chance <= 0.0)
        return false;
    return d.c.x + kickedMomentum(d, id, ts, w) * ts * w.trail_rate > d.c.y;
}

// The segment a drop sweeps this frame.
float2 sweepEnd(Drop d, float ts)
{
    return d.a.xy + float2(d.b.x, d.a.w) * ts;
}

float2 closestOnSegment(float2 p, float2 s0, float2 s1)
{
    float2 ab = s1 - s0;
    float t = saturate(dot(p - s0, ab) / max(dot(ab, ab), 0.0001));
    return s0 + ab * t;
}

// Symmetric merge rule on the previous frame, so the eater and the eaten agree.
bool canEat(Drop di, float i, Drop dj, float j, float ts)
{
    if (!isAlive(di) || !isAlive(dj) || i == j)
        return false;
    bool bigger = di.a.z > dj.a.z || (di.a.z == dj.a.z && i < j);
    bool active = di.a.w > 0.0 || di.c.w > 0.5;
    if (!bigger || !active)
        return false;
    if (abs(di.c.z - (j + 1.0)) < 0.5 || abs(dj.c.z - (i + 1.0)) < 0.5)
        return false;
    float2 near = closestOnSegment(dj.a.xy, di.a.xy, sweepEnd(di, ts));
    float reach = (di.a.z + dj.a.z) * (COLLISION_RADIUS + di.a.w * COLLISION_RADIUS_INCREASE * ts);
    return distance(dj.a.xy, near) < reach;
}

// Gravity, shrinking, trail counter, spread decay and movement.
Drop advanceDrop(Drop d, float id, float ts, Weather w)
{
    bool trail = wantsTrail(d, id, ts, w);
    d.a.w = kickedMomentum(d, id, ts, w);

    if (d.a.z <= w.min_r && rnd(id, 3.0) < 0.05 * ts)
        d.b.w += 0.01;
    d.a.z -= d.b.w * ts;

    d.c.x += d.a.w * ts * w.trail_rate;
    if (trail) {
        d.a.z *= pow(0.97, ts);
        d.c.x = 0.0;
        d.c.y = lerp(w.min_r, w.max_r, rnd(id, 4.0)) - d.a.w * 2.0 * w.trail_rate + (w.max_r - d.a.z);
    }

    d.b.y *= pow(0.4, ts);
    d.b.z *= pow(0.7, ts);

    if (d.a.w > 0.0)
        d.a.xy = sweepEnd(d, ts);

    // Dried out or slid off the glass.
    if (d.a.z <= 0.0 || d.a.y > GLASS_H + d.a.z)
        d = deadDrop();
    return d;
}

Drop finishDrop(Drop d, float ts, Weather w)
{
    d.a.w = max(d.a.w - max(1.0, w.min_r * 0.5 - d.a.w) * 0.1 * ts, 0.0);
    d.b.x *= pow(0.7, ts);
    d.c.w = 0.0;
    return d;
}

Drop makeTrailDrop(Drop parent, float parent_id, float ts, Weather w)
{
    Drop d = deadDrop();
    float r = parent.a.z;
    d.a.x = parent.a.x + (rnd(parent_id, 5.0) * 2.0 - 1.0) * r * 0.1;
    d.a.y = parent.a.y - r * 0.01;
    d.a.z = r * lerp(w.trail_min, w.trail_max, rnd(parent_id, 6.0));
    d.b.z = kickedMomentum(parent, parent_id, ts, w) * 0.15;
    d.c.z = parent_id + 1.0;
    d.c.w = 1.0;
    return d;
}

Drop makeRainDrop(float id, Weather w)
{
    Drop d = deadDrop();
    float r = lerp(w.min_r, w.max_r, pow(rnd(id, 7.0), 3.0));
    d.a.x = rnd(id, 8.0) * glassWidth();
    d.a.y = lerp(SPAWN_TOP, SPAWN_BOTTOM, rnd(id, 9.0)) * GLASS_H;
    d.a.z = r;
    d.a.w = 1.0 + (r - w.min_r) * 0.3 + rnd(id, 10.0) * 0.5;
    d.b.y = 1.5;
    d.b.z = 1.5;
    d.c.w = 1.0;
    return d;
}

// Number of new rain drops this frame: each next drop appears with chance c, up to the limit.
float rainCount(float ts, Weather w)
{
    float area = glassWidth() * GLASS_H / (1024.0 * 768.0);
    float c = w.rain_chance * ts * area;
    float limit = ceil(w.rain_limit * ts * area);
    float expected = 0.0;
    float term = 1.0;
    [loop] for (int k = 1; k <= 64; k++) {
        if (float(k) > limit)
            break;
        term *= min(c, 1.0);
        expected += term;
    }
    return floor(expected + rnd(-1.0, 11.0));
}

Drop updateLiveDrop(Drop d, float id, float ts, Weather w)
{
    float eaten_r2 = 0.0;
    float2 pull = float2(0.0, 0.0);
    float eaten_momentum = 0.0;
    bool absorbed = false;
    [loop] for (int j = 0; j < MAX_DROPS; j++) {
        Drop o = loadPrevDrop(j);
        float jd = float(j);
        if (canEat(o, jd, d, id, ts)) {
            absorbed = true;
            break;
        }
        if (canEat(d, id, o, jd, ts)) {
            eaten_r2 += o.a.z * o.a.z;
            pull += o.a.xy - d.a.xy;
            eaten_momentum = max(eaten_momentum, o.a.w);
        }
    }

    Drop result = deadDrop();
    if (!absorbed) {
        result = advanceDrop(d, id, ts, w);
        if (isAlive(result)) {
            if (eaten_r2 > 0.0) {
                float target = min(sqrt(result.a.z * result.a.z + eaten_r2 * 0.8), w.max_r);
                result.a.z = target;
                result.b.x += pull.x * 0.1;
                result.b.y = 0.0;
                result.b.z = 0.0;
                result.a.w = max(eaten_momentum, min(MAX_MOMENTUM, result.a.w + target * 0.04 + 1.0));
            }
            result = finishDrop(result, ts, w);
        }
    }
    return result;
}

// Free slots go to new rain drops first and then to trail drops:
// the k-th remaining free slot takes the trail drop of the k-th drop that leaves one.
Drop fillFreeSlot(int i, float ts, Weather w)
{
    int free_rank = 0;
    [loop] for (int j = 0; j < i; j++) {
        if (readPrev(j % stateWidth(), j / stateWidth()).z <= 0.0)
            free_rank++;
    }

    Drop result = deadDrop();
    float rain = rainCount(ts, w);
    if (float(free_rank) < rain) {
        result = makeRainDrop(float(i), w);
    } else {
        int trail_rank = free_rank - int(rain);
        int seen = 0;
        [loop] for (int j = 0; j < MAX_DROPS; j++) {
            Drop o = loadPrevDrop(j);
            if (wantsTrail(o, float(j), ts, w)) {
                if (seen == trail_rank) {
                    result = makeTrailDrop(o, float(j), ts, w);
                    break;
                }
                seen++;
            }
        }
    }
    return result;
}

Drop updateDrop(int i, float ts, Weather w)
{
    Drop d = loadPrevDrop(i);
    Drop result;
    if (isAlive(d))
        result = updateLiveDrop(d, float(i), ts, w);
    else
        result = fillFreeSlot(i, ts, w);
    return result;
}

// ===================== GLASS MAP =====================
float packPair(float a, float b)
{
    return a * INDEX_PACK + b;
}

float2 unpackPair(float v)
{
    float a = floor((v + 0.5) / INDEX_PACK);
    return float2(a, v - a * INDEX_PACK);
}

// Keeps the six biggest candidates; ids are index + 1, 0 = empty.
void insertCandidate(float id, float r, inout float4 ids, inout float2 ids2, inout float4 rs, inout float2 rs2)
{
    float min_r = rs.x;
    int slot = 0;
    if (rs.y < min_r) { min_r = rs.y; slot = 1; }
    if (rs.z < min_r) { min_r = rs.z; slot = 2; }
    if (rs.w < min_r) { min_r = rs.w; slot = 3; }
    if (rs2.x < min_r) { min_r = rs2.x; slot = 4; }
    if (rs2.y < min_r) { min_r = rs2.y; slot = 5; }
    if (r <= min_r)
        return;
    if (slot == 0) { ids.x = id; rs.x = r; }
    else if (slot == 1) { ids.y = id; rs.y = r; }
    else if (slot == 2) { ids.z = id; rs.z = r; }
    else if (slot == 3) { ids.w = id; rs.w = r; }
    else if (slot == 4) { ids2.x = id; rs2.x = r; }
    else { ids2.y = id; rs2.y = r; }
}

float mapRows()
{
    return max(state_size.y - float(bandRows() * 3), 1.0);
}

// Glass position of a map texel center and half of its size.
float2 mapTexelCenter(int x, int map_y)
{
    float2 cell = float2(glassWidth() / state_size.x, GLASS_H / mapRows());
    return (float2(float(x), float(map_y)) + 0.5) * cell;
}

int2 mapTexelAt(float2 g)
{
    float2 cell = float2(glassWidth() / state_size.x, GLASS_H / mapRows());
    int2 t = int2(floor(g / cell));
    return int2(clamp(t.x, 0, stateWidth() - 1), clamp(t.y, 0, int(mapRows()) - 1) + bandRows() * 3);
}

float4 updateMap(int x, int map_y, float ts, Weather w)
{
    float4 prev = readPrev(x, map_y + bandRows() * 3);
    float epoch = floor(prev.w);
    float coverage = prev.w - epoch;
    if (epoch < 1.0) {
        epoch = 1.0;
        coverage = min(Fog_Start, 0.99);
    }
    if (w.drizzle > 0.0)
        coverage = min(coverage + ts * w.drizzle / DRIZZLE_REFILL, 0.99);

    float2 g = mapTexelCenter(x, map_y);
    float2 half_cell = 0.5 * float2(glassWidth() / state_size.x, GLASS_H / mapRows());
    float4 ids = float4(0.0, 0.0, 0.0, 0.0);
    float2 ids2 = float2(0.0, 0.0);
    float4 rs = float4(0.0, 0.0, 0.0, 0.0);
    float2 rs2 = float2(0.0, 0.0);
    bool wiped = false;

    int w_tex = stateWidth();
    int rows = bandRows();
    [loop] for (int j = 0; j < MAX_DROPS; j++) {
        // Only fields A and B are needed here; this loop runs for every map texel.
        Drop d;
        d.a = readPrev(j % w_tex, j / w_tex);
        if (d.a.z <= 0.0)
            continue;
        d.b = readPrev(j % w_tex, j / w_tex + rows);
        d.c = float4(0.0, 0.0, 0.0, 0.0);

        // Sliding drops wipe the fog and drizzle with an ellipse slightly below their center.
        if (d.a.w > WIPE_MOMENTUM) {
            float pr = d.a.z * 0.45;
            float2 end = sweepEnd(d, ts);
            float2 q = g - closestOnSegment(g, d.a.xy, end) - float2(0.0, pr * 0.5);
            q /= float2(pr, pr * 1.5);
            if (dot(q, q) < 1.0)
                wiped = true;
        }

        // Drops that may cover this texel after this frame's move; the margin covers the
        // gravity kick and growth from merging.
        float2 c = sweepEnd(d, ts);
        float margin = 4.0 * ts + d.a.z * 0.5 + 2.0;
        float2 ext = float2(d.a.z * (1.0 + d.b.y), d.a.z * 1.5 * (1.0 + d.b.z)) + half_cell + margin;
        if (abs(g.x - c.x) < ext.x && abs(g.y - c.y) < ext.y)
            insertCandidate(float(j) + 1.0, d.a.z, ids, ids2, rs, rs2);
    }

    if (wiped) {
        coverage = 0.0;
        epoch = epoch >= EPOCH_COUNT ? 1.0 : epoch + 1.0;
    }
    return float4(packPair(ids.x, ids.y), packPair(ids.z, ids.w), packPair(ids2.x, ids2.y), epoch + coverage);
}

float4 stateImage(VertData v_in) : TARGET
{
    int2 t = int2(floor(outputUV(v_in.uv) * state_size));
    int w_tex = stateWidth();
    int rows = bandRows();
    float ts = stepScale();
    Weather w = getWeather();

    if (t.y >= rows * 3)
        return updateMap(t.x, t.y - rows * 3, ts, w);

    int band = t.y / rows;
    int i = (t.y - band * rows) * w_tex + t.x;
    if (i >= MAX_DROPS)
        return float4(0.0, 0.0, 0.0, 0.0);

    Drop d = updateDrop(i, ts, w);
    if (band == 0)
        return d.a;
    if (band == 1)
        return d.b;
    return d.c;
}

// ===================== RENDER =====================
struct Water {
    float transmit;  // product of (1 - alpha)
    float weight;
    float2 refraction;
    float depth;
};

// Drop lens: soft egg-shaped alpha and an inverted refraction direction.
void addSprite(float2 p, float2 c, float r, float2 spread, float depth, inout Water water)
{
    float2 half_size = float2(r * (1.0 + spread.x), r * 1.5 * (1.0 + spread.y));
    float2 u = (p - c) / half_size;
    if (abs(u.x) >= 1.0 || abs(u.y) >= 1.0)
        return;
    float alpha = exp(-5.75 * dot(u, u)) * smoothstep(-0.66, -0.45, u.y);
    if (alpha < 0.002)
        return;
    float2 refraction = float2(-u.x, u.y > 0.0 ? -u.y : min(-u.y * 1.1, 0.22));
    float wgt = alpha * alpha * alpha * alpha;
    water.transmit *= 1.0 - alpha;
    water.weight += wgt;
    water.refraction += refraction * wgt;
    water.depth += depth * wgt;
}

float dropDepth(float r, float2 spread, Weather w)
{
    float d = saturate((r - w.min_r) / (w.max_r - w.min_r) * 0.9);
    return d / ((spread.x + spread.y) * 0.5 + 1.0);
}

void addCandidate(float2 p, float id, Weather w, inout Water water)
{
    if (id < 0.5)
        return;
    int i = int(id - 0.5);
    float4 a = loadCurDropA(i);
    if (a.z <= 0.0)
        return;
    float4 b = loadCurDropB(i);
    addSprite(p, a.xy, a.z, b.yz, dropDepth(a.z, b.yz, w), water);
}

// Fine droplets: one per cell, present while the cell's drizzle coverage is high enough.
void addDrizzle(float2 p, float cell, float layer, Weather w, inout Water water)
{
    float2 offset = float2(layer * 0.37, layer * 0.61);
    float2 id = floor(p / cell + offset);
    float2 origin = (id - offset) * cell;
    float4 m = state_texture.SampleLevel(stateSampler, texelUV(mapTexelAt(origin + cell * 0.5).x,
                                                               mapTexelAt(origin + cell * 0.5).y), 0.0);
    float epoch = floor(m.w);
    float coverage = m.w - epoch;
    float4 h = hash42(id + float2(layer * 41.3 + epoch * 7.17, epoch * 3.1));
    if (h.x >= coverage)
        return;
    float r = lerp(w.drizzle_min, w.drizzle_max, h.y * h.y);
    float2 c = origin + cell * (0.3 + 0.4 * h.zw);
    addSprite(p, c, r, float2(0.0, 0.0), 0.0, water);
}

float mapCoverage(int x, int y)
{
    float w = readCur(x, y).w;
    float epoch = floor(w);
    return epoch < 1.0 ? min(Fog_Start, 0.99) : w - epoch;
}

// Fog coverage between map texels, so wiped paths have smooth edges.
float fogCoverage(float2 p)
{
    float2 cell = float2(glassWidth() / state_size.x, GLASS_H / mapRows());
    float2 f = p / cell - 0.5;
    float2 i0 = floor(f);
    float2 t = f - i0;
    int max_x = stateWidth() - 1;
    int max_y = int(mapRows()) - 1;
    int base = bandRows() * 3;
    int x0 = clamp(int(i0.x), 0, max_x);
    int x1 = clamp(int(i0.x) + 1, 0, max_x);
    int y0 = clamp(int(i0.y), 0, max_y) + base;
    int y1 = clamp(int(i0.y) + 1, 0, max_y) + base;
    float top = lerp(mapCoverage(x0, y0), mapCoverage(x1, y0), t.x);
    float bottom = lerp(mapCoverage(x0, y1), mapCoverage(x1, y1), t.x);
    return lerp(top, bottom, t.y);
}

float3 blurSample(float2 uv, float radPx, float2 res, float rot, int n)
{
    float3 a = float3(0.0, 0.0, 0.0);
    float fn = float(n);
    for (int i = 0; i < n; i++) {
        float fi = float(i) + 0.5;
        float rr = sqrt(fi / fn) * radPx;
        float an = fi * 2.39996323 + rot;
        a += image.SampleLevel(clampSampler, uv + float2(cos(an), sin(an)) * rr / res, 0.0).rgb;
    }
    return a / fn;
}

float ign(float2 p)
{
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

float4 mainImage(VertData v_in) : TARGET
{
    float2 uv = v_in.uv;
    float2 res = uv_size;
    float k = res.y / GLASS_H;
    float2 p = outputUV(uv) * float2(glassWidth(), GLASS_H);
    Weather w = getWeather();

    Water water;
    water.transmit = 1.0;
    water.weight = 0.0;
    water.refraction = float2(0.0, 0.0);
    water.depth = 0.0;

    if (w.drizzle > 0.0) {
        addDrizzle(p, w.drizzle_max * 2.0, 0.0, w, water);
        addDrizzle(p, w.drizzle_max * 1.6, 1.0, w, water);
        addDrizzle(p, w.drizzle_max * 1.3, 2.0, w, water);
    }

    int2 t = mapTexelAt(p);
    float4 m = state_texture.SampleLevel(stateSampler, texelUV(t.x, t.y), 0.0);
    float2 c0 = unpackPair(m.x);
    float2 c1 = unpackPair(m.y);
    float2 c2 = unpackPair(m.z);
    addCandidate(p, c0.x, w, water);
    addCandidate(p, c0.y, w, water);
    addCandidate(p, c1.x, w, water);
    addCandidate(p, c1.y, w, water);
    addCandidate(p, c2.x, w, water);
    addCandidate(p, c2.y, w, water);

    float2 refraction = water.weight > 0.0 ? water.refraction / water.weight : float2(0.0, 0.0);
    float depth = water.weight > 0.0 ? water.depth / water.weight : 0.0;
    float a = saturate((1.0 - water.transmit) * Alpha_Multiply - Alpha_Subtract);
    float fog = Fog * fogCoverage(p) * (1.0 - a);

    if (Debug)
        return float4(a, fog, depth, 1.0);

    float2 offset = refraction * (Min_Refraction + depth * (Max_Refraction - Min_Refraction)) * k;
    float3 fg = image.SampleLevel(clampSampler, uv + offset / res, 0.0).rgb * Brightness;

    // Background: blurred glass, more blur and a tint where the fog has not been wiped.
    float3 sharp = image.SampleLevel(clampSampler, uv, 0.0).rgb;
    float3 bg = sharp;
    float rad = (BG_Blur + Fog_Blur * fog) * k;
    if (rad > 0.5) {
        float3 blurred = blurSample(uv, rad, res, ign(uv * res) * 6.2831853, max(Blur_Samples, 4));
        bg = lerp(sharp, blurred, saturate(rad / (2.0 * k)));
    }
    bg = lerp(bg, Fog_Color.rgb, fog * Fog_Tint);

    // Dark rim towards the drop edge and a small highlight in the upper left part.
    float edge = saturate(length(refraction) / 0.46);
    float rim = smoothstep(0.55, 1.0, edge) * a;
    float shine = (1.0 - smoothstep(0.0, 0.12, length(refraction - float2(0.15, 0.2)))) * a;
    fg = fg * (1.0 - Rim * rim) + Highlight * shine;

    float alpha = image.SampleLevel(textureSampler, uv, 0.0).a;
    return float4(saturate(lerp(bg, fg, a)), alpha);
}
