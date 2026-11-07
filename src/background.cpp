#include "stdafx.h"
#include "background.h"

#include <math.h> // sin, cos, floor, sqrt

static const double BACKGROUND_SCROLL_SECONDS = 8.0; // To scroll one cell width (left, and as far up)
// Two ripples cross the surface: one moves points up and down by where they
// are across, a slower one sideways by where they are down. Each depends only
// on the other direction, so with these sizes no triangle folds over.
static const double BACKGROUND_WAVE_HEIGHT = 0.25;   // Of a cell, up and down
static const double BACKGROUND_WAVE_WIDTH = 0.15;    // Of a cell, sideways
static const double BACKGROUND_WAVE_LENGTH = 10.0;    // In cells
// The waves sway back and forth in place, each crest this far (of a wave
// length) to either side
static const double BACKGROUND_WAVE_SWAY = 0.5;
static const double BACKGROUND_WAVE_SECONDS = 24.0;  // For one there and back
static const double BACKGROUND_MIX_MIN = 0.01;       // Towards the accent color, darkest
static const double BACKGROUND_MIX_MAX = 0.20;       // and lightest
// How high the ripples lift the surface, of a cell (the light picks out the slopes)
static const double BACKGROUND_RIPPLE_HEIGHT = 0.5;
// The light: from the top left and in front (pointing at the surface, any length)
static const double BACKGROUND_LIGHT_X = 0.45;
static const double BACKGROUND_LIGHT_Y = 0.55;
static const double BACKGROUND_LIGHT_Z = 0.7;
// How much a slope changes the shade (1: fully towards the light is the
// lightest), and how much the ripples brighten
static const double BACKGROUND_SHADE_LIGHT = 2.5;
static const double BACKGROUND_SHADE_RIPPLE = 0.6;
static const double BACKGROUND_TWO_PI = 6.283185307179586;
// Turned clockwise around the middle of the screen; see
// BACKGROUND_DRAWN_COLUMNS for the limit
static const double BACKGROUND_TILT_DEGREES = 25.0;

// Small stable hash for per point and per triangle variation, 0..1
static double background_hash(int a, int b, int salt)
{
    unsigned int h = (unsigned int)a * 73856093u ^ (unsigned int)b * 19349663u ^ (unsigned int)salt * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (double)(h & 0xFFFF) / 65535.0;
}

// from moved amount of the way to to, rounded up when the fraction is at
// least 1 - threshold (0.5: plain rounding)
static D3DCOLOR background_mix(D3DCOLOR from, D3DCOLOR to, double amount, double threshold)
{
    int channels[3];
    for (int i = 0; i < 3; i++)
    {
        const int shift = 16 - 8 * i;
        const double f = (double)((from >> shift) & 0xFF);
        const double t = (double)((to >> shift) & 0xFF);
        const int level = (int)floor(f + (t - f) * amount + threshold);
        channels[i] = (level < 0) ? 0 : (level > 255) ? 255 : level;
    }
    return D3DCOLOR_XRGB(channels[0], channels[1], channels[2]);
}

void build_background_gradient(D3DCOLOR background, D3DCOLOR accent, DWORD *texels)
{
    for (int r = 0; r < BACKGROUND_DITHER_ROWS; r++)
    {
        DWORD *row = &texels[r * BACKGROUND_GRADIENT_WIDTH];
        const double threshold = (r + 0.5) / BACKGROUND_DITHER_ROWS;
        for (int i = 0; i < BACKGROUND_GRADIENT_SIZE; i++)
        {
            const double amount = BACKGROUND_MIX_MIN + (BACKGROUND_MIX_MAX - BACKGROUND_MIX_MIN) * i / (BACKGROUND_GRADIENT_SIZE - 1);
            row[i] = background_mix(background, accent, amount, threshold);
        }
    }
}

struct BackgroundPoint
{
    FLOAT x, y;
    double z;      // Height, in pixels (towards the viewer)
    double ripple; // How high the ripples are here, 0..1
    FLOAT u;       // Where it looks on the gradient (its shade)
};

// In wave lengths, on a sine; the sideways one slower and out of step, so the
// two never line up for long
static double background_sway(double seconds, double period, double phase)
{
    return BACKGROUND_WAVE_SWAY * sin(BACKGROUND_TWO_PI * seconds / period + phase);
}

// -1..1: the up and down ripple by x, the sideways one by y
static double background_ripple_y(double x, double seconds, double wave_length)
{
    return sin(BACKGROUND_TWO_PI * (x / wave_length - background_sway(seconds, BACKGROUND_WAVE_SECONDS, 0.0)));
}

static double background_ripple_x(double y, double seconds, double wave_length)
{
    return sin(BACKGROUND_TWO_PI * (y / wave_length - background_sway(seconds, BACKGROUND_WAVE_SECONDS * 1.4, 1.0)));
}

// Rounding per pixel: (5x + 9y) mod 16, so neighbours differ a lot. Linear in
// x and y, so the GPU interpolates it; v wraps, +0.5 is a row's middle.
static double background_dither_v(double x, double y)
{
    return (5.0 * x + 9.0 * y + 0.5) / BACKGROUND_DITHER_ROWS;
}

// How much the surface faces the light, plus the ripple's brightening.
// along: left to right through the point, across: above to below.
static FLOAT background_shade_u(const BackgroundPoint &left, const BackgroundPoint &right,
                                const BackgroundPoint &above, const BackgroundPoint &below, double ripple)
{
    // The normal (cross product, towards the viewer) against the light
    const double ux = right.x - left.x, uy = right.y - left.y, uz = right.z - left.z;
    const double wx = below.x - above.x, wy = below.y - above.y, wz = below.z - above.z;
    const double nx = uy * wz - uz * wy;
    const double ny = uz * wx - ux * wz;
    const double nz = ux * wy - uy * wx;
    const double n_length = sqrt(nx * nx + ny * ny + nz * nz);
    const double l_length = sqrt(BACKGROUND_LIGHT_X * BACKGROUND_LIGHT_X + BACKGROUND_LIGHT_Y * BACKGROUND_LIGHT_Y +
                                 BACKGROUND_LIGHT_Z * BACKGROUND_LIGHT_Z);
    // The light: up (-y), left (-x) and out of the screen
    const double facing = (n_length > 0.0)
        ? (-nx * BACKGROUND_LIGHT_X - ny * BACKGROUND_LIGHT_Y + nz * BACKGROUND_LIGHT_Z) / (n_length * l_length)
        : 0.0;
    // A level surface is the middle shade; slopes move it either way
    const double level = BACKGROUND_LIGHT_Z / l_length;
    double shade = 0.5 + BACKGROUND_SHADE_LIGHT * (facing - level) + BACKGROUND_SHADE_RIPPLE * (ripple - 0.5);
    shade = (shade < 0.0) ? 0.0 : (shade > 1.0) ? 1.0 : shade;

    // On the texel centers: the first at shade 0, the last at 1
    return (FLOAT)((0.5 + shade * (BACKGROUND_GRADIENT_SIZE - 1)) / BACKGROUND_GRADIENT_WIDTH);
}

// Each corner keeps its own place on the gradient and the GPU blends across
// the triangle, so the triangles don't show
static void background_emit(const BackgroundPoint &a, const BackgroundPoint &b, const BackgroundPoint &c,
                            FLOAT *xy, FLOAT *uv, int &v)
{
    // Less whole repeats, the same for all three corners: stays exact in a float
    const double dither_base = floor(background_dither_v(a.x, a.y));

    const BackgroundPoint *corners[3] = {&a, &b, &c};
    for (int k = 0; k < 3; k++)
    {
        xy[v * 2] = corners[k]->x;
        xy[v * 2 + 1] = corners[k]->y;
        uv[v * 2] = corners[k]->u;
        uv[v * 2 + 1] = (FLOAT)(background_dither_v(corners[k]->x, corners[k]->y) - dither_base);
        v++;
    }
}

int build_background_mesh(FLOAT width, FLOAT height, double seconds, FLOAT *xy, FLOAT *uv)
{
    const double cell_w = (double)width / BACKGROUND_COLUMNS;
    const double cell_h = (double)height / BACKGROUND_ROWS;
    const double wave_length_x = BACKGROUND_WAVE_LENGTH * cell_w;
    const double wave_length_y = BACKGROUND_WAVE_LENGTH * cell_h;

    // Up as many pixels as left: 45 degrees
    const double scroll_x = seconds / BACKGROUND_SCROLL_SECONDS;
    const double scroll_y = scroll_x * cell_w / cell_h;
    const int scrolled_x = (int)floor(scroll_x);
    const int scrolled_y = (int)floor(scroll_y);
    const double shift_x = (scroll_x - scrolled_x) * cell_w;
    const double shift_y = (scroll_y - scrolled_y) * cell_h;

    const double tilt = BACKGROUND_TILT_DEGREES * BACKGROUND_TWO_PI / 360.0;
    const double tilt_cos = cos(tilt);
    const double tilt_sin = sin(tilt);
    const double middle_x = width * 0.5;
    const double middle_y = height * 0.5;

    // In the grid's own space, centered on the screen, then turned onto it
    static BackgroundPoint points[BACKGROUND_DRAWN_ROWS + 1][BACKGROUND_DRAWN_COLUMNS + 1];
    for (int r = 0; r <= BACKGROUND_DRAWN_ROWS; r++)
    {
        const int row_id = scrolled_y + r;
        for (int c = 0; c <= BACKGROUND_DRAWN_COLUMNS; c++)
        {
            const int column_id = scrolled_x + c;
            const double x = (c - BACKGROUND_DRAWN_COLUMNS / 2) * cell_w - shift_x;
            const double y = (r - BACKGROUND_DRAWN_ROWS / 2) * cell_h - shift_y;
            // The waves stay put while the grid scrolls through them
            const double grid_x = x + BACKGROUND_WAVE_WIDTH * cell_w * background_ripple_x(y, seconds, wave_length_y);
            const double grid_y = y + BACKGROUND_WAVE_HEIGHT * cell_h * background_ripple_y(x, seconds, wave_length_x);
            points[r][c].x = (FLOAT)(middle_x + grid_x * tilt_cos - grid_y * tilt_sin);
            points[r][c].y = (FLOAT)(middle_y + grid_x * tilt_sin + grid_y * tilt_cos);
            // Height: the ripple (the up and down one counts most)
            const double ripple = 0.65 * (0.5 + 0.5 * background_ripple_y(x, seconds, wave_length_x)) +
                                  0.35 * (0.5 + 0.5 * background_ripple_x(y, seconds, wave_length_y));
            points[r][c].ripple = ripple;
            points[r][c].z = cell_w * BACKGROUND_RIPPLE_HEIGHT * ripple;
        }
    }

    // Shade from the slope (both neighbours, so it's smooth) and the ripple
    for (int r = 0; r <= BACKGROUND_DRAWN_ROWS; r++)
    {
        for (int c = 0; c <= BACKGROUND_DRAWN_COLUMNS; c++)
        {
            const BackgroundPoint &left = points[r][(c > 0) ? c - 1 : c];
            const BackgroundPoint &right = points[r][(c < BACKGROUND_DRAWN_COLUMNS) ? c + 1 : c];
            const BackgroundPoint &above = points[(r > 0) ? r - 1 : r][c];
            const BackgroundPoint &below = points[(r < BACKGROUND_DRAWN_ROWS) ? r + 1 : r][c];
            points[r][c].u = background_shade_u(left, right, above, below, points[r][c].ripple);
        }
    }

    int v = 0;
    for (int r = 0; r < BACKGROUND_DRAWN_ROWS; r++)
    {
        const int row_id = scrolled_y + r;
        for (int c = 0; c < BACKGROUND_DRAWN_COLUMNS; c++)
        {
            // Two triangles per cell; the diagonal alternates like a checkerboard
            const BackgroundPoint &tl = points[r][c];
            const BackgroundPoint &tr = points[r][c + 1];
            const BackgroundPoint &bl = points[r + 1][c];
            const BackgroundPoint &br = points[r + 1][c + 1];
            if (((row_id + scrolled_x + c) & 1) != 0)
            {
                background_emit(tl, tr, br, xy, uv, v);
                background_emit(br, bl, tl, xy, uv, v);
            }
            else
            {
                background_emit(tl, tr, bl, xy, uv, v);
                background_emit(br, bl, tr, xy, uv, v);
            }
        }
    }
    return v;
}
