/* a 16.16 fixed point implementation */

typedef int32_t FixedPoint;
#define SHIFT 16
inline FixedPoint mul(FixedPoint a, FixedPoint b)
{
	return int64_t(a)*int64_t(b) >> SHIFT;
}

inline FixedPoint add(FixedPoint a, FixedPoint b)
{
	return a + b;
}

struct PointFixed {
	FixedPoint x;
	FixedPoint y;
};

#define FIXED_1 (1 << SHIFT)
// Inverse: most libraries just do adjugate of the matrix multiplied by the inverse
// of the determinate
struct FixedMatrix
{
	FixedPoint xx; FixedPoint xy; FixedPoint yx; FixedPoint yy;
	FixedPoint x0; FixedPoint y0;
	PointFixed transform(int x, int y)
	{
		// when taking int parameters he can use a regular multiply instead of a
		// fixed one
		PointFixed ret;
		ret.x = x * xx + xy * y + x0;
		ret.y = y * yy + yx * x + y0;
		return ret;
	}

	FixedMatrix() { xx = FIXED_1; yx = 0; xy = 0; yy = FIXED_1; x0 = 0; y0 = 0;}
};


