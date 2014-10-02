/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.skia file.
 */

#include <assert.h>
#include "skia-utils.h"

// XXX: we'll want to switch this to fixed point
static inline double Sk2ScalarDiv(double a, double b) {
	return a / b;
}
static inline double Sk2ScalarMul(double a, double b) {
	return a * b;
}
static inline double Sk2ScalarAbs(double a) {
	if (a < 0)
		return -a;
	return a;
}
static inline bool Sk2ScalarIsNaN(double a)
{
	return a != a;
}
#define SK_Scalar1 1.0
#define Sk2ASSERT(x) assert(x)
// we can do this 
static int valid_unit_divide(Sk2Scalar numer, Sk2Scalar denom, Sk2Scalar* ratio)
{
    Sk2ASSERT(ratio);

    if (numer < 0)
    {
        numer = -numer;
        denom = -denom;
    }

    if (denom == 0 || numer == 0 || numer >= denom)
        return 0;

    Sk2Scalar r = Sk2ScalarDiv(numer, denom);
    if (Sk2ScalarIsNaN(r)) {
        return 0;
    }
    Sk2ASSERT(r >= 0 && r < SK_Scalar1);
    if (r == 0) // catch underflow if numer <<<< denom
        return 0;
    *ratio = r;
    return 1;
}




#ifdef SK_SCALAR_IS_FIXED
    static int is_not_monotonic(int a, int b, int c, int d)
    {
        return (((a - b) | (b - c) | (c - d)) & ((b - a) | (c - b) | (d - c))) >> 31;
    }

    static int is_not_monotonic(int a, int b, int c)
    {
        return (((a - b) | (b - c)) & ((b - a) | (c - b))) >> 31;
    }
#else
    static int is_not_monotonic(float a, float b, float c)
    {
        float ab = a - b;
        float bc = b - c;
        if (ab < 0)
            bc = -bc;
        return ab == 0 || bc < 0;
    }
#endif


static inline Sk2Scalar Sk2ScalarInterp(Sk2Scalar A, Sk2Scalar B, Sk2Scalar t) {
    Sk2ASSERT(t >= 0 && t <= SK_Scalar1);
    return A + Sk2ScalarMul(B - A, t);
}

static void interp_quad_coords(const Sk2Scalar* src, Sk2Scalar* dst, Sk2Scalar t)
{
    Sk2Scalar    ab = Sk2ScalarInterp(src[0], src[2], t);
    Sk2Scalar    bc = Sk2ScalarInterp(src[2], src[4], t);

    dst[0] = src[0];
    dst[2] = ab;
    dst[4] = Sk2ScalarInterp(ab, bc, t);
    dst[6] = bc;
    dst[8] = src[4];
}

void Sk2ChopQuadAt(const Sk2Point src[3], Sk2Point dst[5], Sk2Scalar t)
{
    Sk2ASSERT(t > 0 && t < SK_Scalar1);

    interp_quad_coords(&src[0].fX, &dst[0].fX, t);
    interp_quad_coords(&src[0].fY, &dst[0].fY, t);
}

// ensures that the y values are contiguous
// dst[1].fY = dst[3].fY = dst[2].fY
// I'm not sure why we need this
static inline void flatten_double_quad_extrema(Sk2Scalar coords[14])
{
	    coords[2] = coords[6] = coords[4];
}

/*  Returns 0 for 1 quad, and 1 for two quads, either way the answer is
 stored in dst[]. Guarantees that the 1/2 quads will be monotonic.
 */
int Sk2ChopQuadAtYExtrema(const Sk2Point src[3], Sk2Point dst[5])
{
    Sk2ASSERT(src);
    Sk2ASSERT(dst);

#if 0
    static bool once = true;
    if (once)
    {
        once = false;
        Sk2Point s[3] = { 0, 26398, 0, 26331, 0, 20621428 };
        Sk2Point d[6];

        int n = Sk2ChopQuadAtYExtrema(s, d);
        Sk2Debugf("chop=%d, Y=[%x %x %x %x %x %x]\n", n, d[0].fY, d[1].fY, d[2].fY, d[3].fY, d[4].fY, d[5].fY);
    }
#endif

    Sk2Scalar a = src[0].fY;
    Sk2Scalar b = src[1].fY;
    Sk2Scalar c = src[2].fY;

    if (is_not_monotonic(a, b, c))
    {
        Sk2Scalar    tValue;
        if (valid_unit_divide(a - b, a - b - b + c, &tValue))
        {
            Sk2ChopQuadAt(src, dst, tValue);
            flatten_double_quad_extrema(&dst[0].fY);
            return 1;
        }
        // if we get here, we need to force dst to be monotonic, even though
        // we couldn't compute a unit_divide value (probably underflow).
        b = Sk2ScalarAbs(a - b) < Sk2ScalarAbs(b - c) ? a : c;
    }
    dst[0].set(src[0].fX, a);
    dst[1].set(src[1].fX, b);
    dst[2].set(src[2].fX, c);
    return 0;
}
