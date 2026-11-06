#ifndef BACKGROUND_H_
#define BACKGROUND_H_

#include <xtl.h>

//-----------------------------------------------------------------------------
// Moving background (background.cpp): a smooth surface with waves swaying
// back and forth across it, the theme background mixed a little towards its
// first map color.
// It's drawn as a regular grid of triangles, bent by the waves and tilted by
// 30 degrees, scrolling slowly up and to the left along the tilt (diagonally,
// the same speed in pixels both ways); it reaches past every side, so the
// mesh always covers the screen. It's lit from the top left: the waves lift
// the surface, the light picks out their slopes, and they brighten it as they
// pass. The shading is smooth, so the triangles themselves don't show.
//
// The colors come from a gradient texture (build_background_gradient), and
// each corner only says where on it to look (a float texture coordinate). The
// GPU then works out the color for every pixel, so slow changes move smoothly
// across the screen instead of whole triangles stepping one 8 bit level.
// The texture is also dithered: it holds each color rounded 16 ways, and each
// pixel picks one by where it is on the screen. Neighbouring pixels then land
// on both sides of the exact shade, so there are no bands, just a still grain
// of at most one level.
//-----------------------------------------------------------------------------

static const int BACKGROUND_ROWS = 12;         // Cells down the screen
static const int BACKGROUND_COLUMNS = BACKGROUND_ROWS * 3 / 2;     // Cells across the screen
// Cells drawn each way, centered on the screen: the grid is tilted
// (BACKGROUND_TILT_DEGREES in background.cpp), so it has to reach the corners
// of the screen when turned, plus some for the scrolling.
// Twice the screen's cells covers tilts up to about 35 degrees.
static const int BACKGROUND_DRAWN_COLUMNS = BACKGROUND_COLUMNS * 2 + 4;
static const int BACKGROUND_DRAWN_ROWS = BACKGROUND_ROWS * 2 + 4;
// Two triangles per cell
static const int BACKGROUND_VERTEX_COUNT = BACKGROUND_DRAWN_COLUMNS * BACKGROUND_DRAWN_ROWS * 2 * 3;

// Shades in the gradient, and the ways each is rounded
static const int BACKGROUND_GRADIENT_SIZE = 256;
static const int BACKGROUND_DITHER_ROWS = 16;
// The texture: one row per rounding
static const int BACKGROUND_GRADIENT_WIDTH = BACKGROUND_GRADIENT_SIZE;
static const int BACKGROUND_GRADIENT_HEIGHT = BACKGROUND_DITHER_ROWS;

// The gradient, BACKGROUND_GRADIENT_WIDTH x BACKGROUND_GRADIENT_HEIGHT texels.
// Columns go from the darkest to the lightest mix of background towards
// accent; row r rounds each with the threshold (r + 0.5) / 16.
void build_background_gradient(D3DCOLOR background, D3DCOLOR accent, DWORD *texels);

// Fills xy (x, y pairs, screen pixels) and uv (texture coordinates on the
// gradient, normalized; v repeats, draw with wrapping in v and point sampling)
// with up to BACKGROUND_VERTEX_COUNT vertices, for the background clock in
// seconds. Returns the vertex count.
// The mesh scrolls up and left: grid columns and rows have a fixed identity
// (scrolled + c, scrolled + r), so a point keeps its motion and a triangle its
// color as it moves across, and new ones come in from the right and the bottom.
int build_background_mesh(FLOAT width, FLOAT height, double seconds, FLOAT *xy, FLOAT *uv);

#endif // BACKGROUND_H_
