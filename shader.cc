#include <stdint.h>
#include <new>
#include <math.h>
#include <stdio.h>
#include "shader.h"
#define force_inline __attribute__((always_inline))
#define BILINEAR_INTERPOLATION_BITS 4
typedef int32_t pixman_fixed_t;
#define pixman_fixed_1 (1<<16)
#ifdef APPROX
// this is an approximation of true 'over' that does a division by 256 instead
// of 255. It is the same style of blending that Skia does.
static inline uint32_t over(uint32_t src, uint32_t dst) {
    uint32_t a = src>>24;
    a = 256 - a;
    uint32_t mask = 0xff00ff;
    uint32_t rb = ((dst & 0xff00ff) * a) >> 8;
    uint32_t ag = ((dst >> 8) & 0xff00ff) * a;
    return src + (rb & mask) | (ag & ~mask);
}

#else

// This is an accurate pixman style 'over'
#define G_SHIFT 8
#define RB_MASK 0xff00ff
#define RB_MASK_PLUS_ONE 0x10000100

// this is essentially ((a*b+128)*257)/65536 but modified to only use 16 bits
// to do the entire operation.
static inline uint32_t mul(uint32_t x, uint32_t a)
{
	uint32_t t = (x & RB_MASK)*a + 0x80080;
	x = (t + ((t >> G_SHIFT) & RB_MASK)) >> G_SHIFT;
	x &= RB_MASK;
	return x;
}

static inline uint32_t add(uint32_t x, uint32_t y)
{
	uint32_t t = x+y;
	t |= RB_MASK_PLUS_ONE - ((t >> G_SHIFT) & RB_MASK);
	x = t & RB_MASK;
	return x;
}
static inline uint32_t over(uint32_t src, uint32_t dst) {
	uint32_t a = ~src>>24;
	uint32_t r1, r2, r3;
	r1 = dst;
	r2 = src & 0xff00ff;
	r1 = mul(r1, a);
	r1 = add(r1, r2);

	r2 = dst >> G_SHIFT;
	r3 = (src >> G_SHIFT) & 0xff00ff;
	r2 = mul(r2, a);
	r2 = add(r2, r3);

	return r1 | (r2 << G_SHIFT);
}
#endif

Intermediate radial_gradient_eval(Shape *s, int x, int y)
{
	RadialGradient *r = s->radial_gradient;
	PointFixed p = r->matrix.transform(x, y);
	// do transform
	int distance = (int)hypot(p.x, p.y);
	distance >>= 8;
	if (distance > 32768)
		distance = 32768;
	return Intermediate::expand(r->lookup[distance>>7]);
}

Intermediate linear_gradient_eval(Shape *s, int x, int y)
{
	LinearGradient *r = s->linear_gradient;
	PointFixed p = r->matrix.transform(x, y);
	int lx = p.x >> 16;
	if (lx > 0x100)
		lx = 0x100;
	if (lx < 0)
		lx = 0;
	return Intermediate::expand(r->lookup[lx]);
}

void linear_opaque_fill(Shape *s, uint32_t *buf, int x, int y, int w)
{
	LinearGradient *r = s->linear_gradient;
	PointFixed p = r->matrix.transform(x, y);
	int lx = p.x;
	int dx = r->matrix.xx;
	while (w >= 4) {
		if (lx > 65536)
			lx = 65536;
		if (lx < 0)
			lx = 0;
		*buf++ = r->lookup[lx>>16];
		lx += dx;
		w-=4;
	}
}

// we can reduce this to two multiplies
// http://stereopsis.com/doubleblend.html
uint32_t lerp(uint32_t a, uint32_t b, int t)
{
	uint32_t mask = 0xff00ff;
	uint32_t brb = ((b & 0xff00ff) * t) >> 8;
	uint32_t bag = ((b >> 8) & 0xff00ff) * t;
	t = 256-t;
	uint32_t arb = ((a & 0xff00ff) * t) >> 8;
	uint32_t aag = ((a >> 8) & 0xff00ff) * t;
	uint32_t rb = arb + brb;
	uint32_t ag = aag + bag;
	return (rb & mask) | (ag & ~mask);
}

void
build_lut(GradientStop *stops, int count, uint32_t *lut)
{
	GradientStop *stop;
	int last_pos = 0;
	uint32_t last_color, next_color;

	stop = &stops[0];
	last_color = stop->color;
	next_color = last_color;
	int next_pos = 256*stop->position;
	int i = 0;
	while (i < 257) {
		while (next_pos <= i) {
			stop++;
			last_color = next_color;
			if (stop > &stops[count-1]) {
				stop = &stops[count-1];
				next_pos = 256;
				next_color = stop->color;
				break;
			}
			next_pos = 256*stop->position;
			next_color = stop->color;
		}
		int inverse = 256*256/(next_pos-last_pos);
		int t = 0;
		// XXX we could actually avoid doing any multiplications inside
		// this loop by accumulating (next_color - last_color)*inverse
		for (; i<=next_pos; i++) {
			lut[i] = lerp(last_color, next_color, t>>8);
			t += inverse;
		}
		last_pos = next_pos;
	}
}
#undef force_inline
#define force_inline
#define static
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

static force_inline uint32_t
bilinear_interpolation_new (uint32_t tl, uint32_t tr,
			uint32_t bl, uint32_t br,
			int distx, int disty)
{
    int distxy, distxiy, distixy, distixiy;
    uint32_t lo, hi;

    distx <<= (4 - BILINEAR_INTERPOLATION_BITS);
    disty <<= (4 - BILINEAR_INTERPOLATION_BITS);

    uint32_t dt_rb = (tr & 0xff00ff) - (tl & 0xff00ff);
    uint32_t dt_ag = ((tr >> 8) & 0xff00ff) - ((tl >> 8) & 0xff00ff);
    uint32_t db_rb = (br & 0xff00ff) - (bl & 0xff00ff);
    uint32_t db_ag = ((br >> 8) & 0xff00ff) - ((bl >> 8) & 0xff00ff);

    dt_rb *= distx;
    dt_ag *= distx;
    db_rb *= distx;
    db_ag *= distx;

    uint32_t a_rb = (tl & 0xff00ff)<<4;
    uint32_t a_ag = (tl & 0xff00ff)<<4;
    uint32_t b_rb = (bl & 0xff00ff)<<4;
    uint32_t b_ag = (bl & 0xff00ff)<<4;

    a_rb += dt_rb;
    a_ag += dt_ag;
    b_rb += db_rb;
    b_ag += db_ag;

    uint32_t d_rb = b_rb - a_rb;
    uint32_t d_ag = b_ag - a_ag;
    a_rb <<= 4;
    a_ag <<= 4;

    d_rb *= disty;
    d_ag *= disty;

    a_rb += d_rb;
    a_ag += d_ag;

    return ((a_rb >> 8) & 0xff00ff) | (a_ag & 0xff00ff00);
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

    return bitmap->data[y * bitmap->width + x];
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

void generic_opaque_fill(Shape *s, uint32_t *buf, int x, int y, int w)
{
	while (w >= 4) {
		*buf++ = s->eval(s, x, y).finalize_unaccumulated();
		w-=4;
		x++;
	}
}

void generic_over_fill(Shape *s, uint32_t *buf, int x, int y, int w)
{
	while (w >= 4) {
		*buf = over(s->eval(s, x, y).finalize_unaccumulated(), *buf);
		buf++;
		w-=4;
		x++;
	}
}
