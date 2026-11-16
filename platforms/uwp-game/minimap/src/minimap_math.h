#ifndef GC_MINIMAP_MATH_H
#define GC_MINIMAP_MATH_H
/* Original sector/projection math shared by guest code and synthetic tests. */
typedef struct { float x, z; int valid, clipped; } minimap_point;
static inline int minimap_finite(float value) {
    union { float f; unsigned int bits; } v = { value };
    return (v.bits & 0x7f800000u) != 0x7f800000u;
}
static inline int minimap_sea_room(float x, float z) {
    if (!minimap_finite(x) || !minimap_finite(z) || x < -350000.0f ||
        x >= 350000.0f || z < -350000.0f || z >= 350000.0f) return 0;
    float fx = x / 100000.0f + 0.5f, fz = z / 100000.0f + 0.5f;
    int gx = (int)fx, gz = (int)fz;
    if ((float)gx > fx) --gx;
    if ((float)gz > fz) --gz;
    return (gz + 3) * 7 + gx + 4;
}
static inline int minimap_bounds_valid(const float* b) {
    for (int i = 0; i < 4; ++i) if (!minimap_finite(b[i])) return 0;
    float width = b[2] - b[0], height = b[3] - b[1];
    return width > 0 && height > 0 && minimap_finite(width) && minimap_finite(height);
}
static inline minimap_point minimap_project(float x, float z, const float* b) {
    minimap_point result = {0, 0, 0, 0};
    if (!minimap_finite(x) || !minimap_finite(z) || !minimap_bounds_valid(b)) return result;
    float u = (x - b[0]) / (b[2] - b[0]), v = (z - b[1]) / (b[3] - b[1]);
    if (!minimap_finite(u) || !minimap_finite(v)) return result;
    result.valid = 1;
    result.clipped = u < .025f || u > .975f || v < .025f || v > .975f;
    result.x = u < .025f ? .025f : u > .975f ? .975f : u;
    result.z = v < .025f ? .025f : v > .975f ? .975f : v;
    return result;
}
/* Setup writes exactly four network-order IEEE-754 floats; never trust a partial file. */
static inline int minimap_decode_bounds(const unsigned char* bytes, unsigned int count, float* b) {
    if (count != 16) return 0;
    for (unsigned int i = 0; i < 4; ++i) {
        const unsigned char* p = bytes + i * 4;
        union { unsigned int bits; float f; } value;
        value.bits = ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
                     ((unsigned int)p[2] << 8) | p[3];
        b[i] = value.f;
    }
    return minimap_bounds_valid(b);
}
/* Is the snapshot from Link's step still current? The HUD callback runs once per logic pass;
 * full_step is the port's full logic step counter (wwhd_logic_step). While the game is paused
 * or a full-screen menu is open, Link's Execute is skipped, so a new full step begins without a
 * capture and the panel hides. True 60 half passes don't start a new full step: Link may skip
 * them (sailing runs at 30 Hz), so they keep the full pass's decision instead of flickering. */
typedef struct { unsigned long long step; int started, current, captured; } minimap_freshness;
static inline void minimap_note_capture(minimap_freshness* f) { f->captured = 1; }
static inline int minimap_snapshot_current(minimap_freshness* f, unsigned long long full_step) {
    if (!f->started || full_step != f->step) {
        f->started = 1;
        f->step = full_step;
        f->current = f->captured;
    } else if (f->captured) {
        f->current = 1;
    }
    f->captured = 0;
    return f->current;
}
#endif
