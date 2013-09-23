/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.skia file.
 */

typedef float Sk2Scalar;

struct Sk2Point
{
	    void set(Sk2Scalar x, Sk2Scalar y) { fX = x; fY = y; }
	Sk2Scalar fX;
	Sk2Scalar fY;
};


int Sk2ChopQuadAtYExtrema(const Sk2Point src[3], Sk2Point dst[5]);
