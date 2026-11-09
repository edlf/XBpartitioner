#ifndef BACKGROUND_H_
#define BACKGROUND_H_

#include "d3dcolor.h"

// Moving background: a smooth, tilted grid of triangles with waves swaying
// across it, scrolling up and left, lit from the top left, in the theme's
// background mixed a little towards its first map color.
// Corners only give a place on a gradient texture, so the GPU shades every
// pixel smoothly. The texture is dithered 16 ways by screen position: no
// banding, only a still grain of at most one level.

static const int BACKGROUND_ROWS = 12;         // Cells down the screen
static const int BACKGROUND_COLUMNS = BACKGROUND_ROWS * 3 / 2;     // Cells across the screen
// Cells drawn each way: the tilted grid must still reach the screen's corners,
// plus some for the scrolling. Twice the screen's cells covers up to ~35 degrees.
static const int BACKGROUND_DRAWN_COLUMNS = BACKGROUND_COLUMNS * 2 + 4;
static const int BACKGROUND_DRAWN_ROWS = BACKGROUND_ROWS * 2 + 4;
static const int BACKGROUND_VERTEX_COUNT = BACKGROUND_DRAWN_COLUMNS * BACKGROUND_DRAWN_ROWS * 2 * 3;

// Shades in the gradient, and the ways each is rounded
static const int BACKGROUND_GRADIENT_SIZE = 256;
static const int BACKGROUND_DITHER_ROWS = 16;
// The texture: one row per rounding
static const int BACKGROUND_GRADIENT_WIDTH = BACKGROUND_GRADIENT_SIZE;
static const int BACKGROUND_GRADIENT_HEIGHT = BACKGROUND_DITHER_ROWS;

// Columns go from the darkest to the lightest mix of background towards
// accent; row r rounds with the threshold (r + 0.5) / 16.
void build_background_gradient(D3DCOLOR background, D3DCOLOR accent, DWORD *texels);

// xy: screen pixels; uv: on the gradient, v repeats (wrap in v, point
// sampling). Returns the vertex count. Rows and columns keep their identity
// as they scroll, so points keep their motion and triangles their color.
int build_background_mesh(FLOAT width, FLOAT height, double seconds, FLOAT *xy, FLOAT *uv);

#endif // BACKGROUND_H_
