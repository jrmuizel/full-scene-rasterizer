#include <stdint.h>
#include <new>
#include <math.h>

#include "shader.h"
#include "rasterizer.h"
#define force_inline __attribute__((always_inline))
#define BILINEAR_INTERPOLATION_BITS 4
typedef int32_t pixman_fixed_t;
#define pixman_fixed_1 (1<<16)
// this is an approximation of true 'over' that does a division by 256 instead
// of 255.
static inline uint32_t over(uint32_t src, uint32_t dst) {
    uint32_t a = src>>24;
    a = 255 - a;
    uint32_t mask = 0xff00ff;
    uint32_t rb = ((src & 0xff00ff) * a) >> 8;
    uint32_t ag = ((src >> 8) & 0xff00ff) * a;
    return (rb & mask) | (ag & ~mask);
}

Intermediate radial_gradient_eval(Shape *s, int x, int y)
{
	RadialGradient *r = s->radial_gradient;
	// do transform
	float distance = hypot(x - r->center_x, y - r->center_y);
	if (distance > 0)
		distance = 1;
	return Intermediate::expand(r->lookup[(int)(distance*256)]);
}

/* Inspired by Filter_32_opaque from Skia */
static force_inline uint32_t
bilinear_interpolation (uint32_t tl, uint32_t tr,
			uint32_t bl, uint32_t br,
			int distx, int disty)
{
    int distxy, distxiy, distixy, distixiy;
    uint32_t lo, hi;

    distx <<= (4 - BILINEAR_INTERPOLATION_BITS);
    disty <<= (4 - BILINEAR_INTERPOLATION_BITS);

    distxy = distx * disty;
    distxiy = (distx << 4) - distxy;	/* distx * (16 - disty) */
    distixy = (disty << 4) - distxy;	/* disty * (16 - distx) */
    distixiy =
	16 * 16 - (disty << 4) -
	(distx << 4) + distxy; /* (16 - distx) * (16 - disty) */

    lo = (tl & 0xff00ff) * distixiy;
    hi = ((tl >> 8) & 0xff00ff) * distixiy;

    lo += (tr & 0xff00ff) * distxiy;
    hi += ((tr >> 8) & 0xff00ff) * distxiy;

    lo += (bl & 0xff00ff) * distixy;
    hi += ((bl >> 8) & 0xff00ff) * distixy;

    lo += (br & 0xff00ff) * distxy;
    hi += ((br >> 8) & 0xff00ff) * distxy;

    return ((lo >> 8) & 0xff00ff) | (hi & ~0xff00ff);
}

uint32_t clamp(int a, int max)
{
    if (a < 0)
	return 0;
    if (a > max)
	return max;
    return a;
}

/* portions Copyright Pixman */

static force_inline int
pixman_fixed_to_bilinear_weight (pixman_fixed_t x)
{
        return (x >> (16 - BILINEAR_INTERPOLATION_BITS)) &
	               ((1 << BILINEAR_INTERPOLATION_BITS) - 1);
}

static uint32_t
get_pixel(Bitmap *bitmap, int x, int y)
{
    if (x < 0)
	x = 0;
    if (x > bitmap->width)
	x = bitmap->width;

    if (y < 0)
	y = 0;
    if (y > bitmap->height)
	y = bitmap->height;

    return bitmap->data[y * bitmap->width*4 + x];
}

#define pixman_fixed_to_int(f)          ((int) ((f) >> 16))

static force_inline uint32_t
bits_image_fetch_pixel_bilinear (Bitmap   *image,
				 pixman_fixed_t  x,
				 pixman_fixed_t  y)
{
    int x1, y1, x2, y2;
    uint32_t tl, tr, bl, br;
    int32_t distx, disty;

    x1 = x - pixman_fixed_1 / 2;
    y1 = y - pixman_fixed_1 / 2;

    distx = pixman_fixed_to_bilinear_weight (x1);
    disty = pixman_fixed_to_bilinear_weight (y1);

    x1 = pixman_fixed_to_int (x1);
    y1 = pixman_fixed_to_int (y1);
    x2 = x1 + 1;
    y2 = y1 + 1;

    tl = get_pixel (image, x1, y1);
    tr = get_pixel (image, x2, y1);
    bl = get_pixel (image, x1, y2);
    br = get_pixel (image, x2, y2);

    return bilinear_interpolation (tl, tr, bl, br, distx, disty);
}

Intermediate bitmap_linear_eval(Shape *s, int x, int y)
{
    Bitmap *bitmap  = s->bitmap;
    PointFixed p = bitmap->matrix.transform(x, y);
    return Intermediate::expand(bits_image_fetch_pixel_bilinear(bitmap, p.x, p.y));
}

Intermediate bitmap_nearest_eval(Shape *s, int x, int y)
{
    Bitmap *bitmap  = s->bitmap;
    PointFixed p = bitmap->matrix.transform(x, y);
    return Intermediate::expand(get_pixel(bitmap, pixman_fixed_to_int(p.x), pixman_fixed_to_int(p.y)));
}
