#ifndef MATRIX_H
#define MATRIX_H

#include <math.h>
struct Matrix
{
	float xx; float xy; float yx; float yy;
	float x0; float y0;
	Point Mul(Point p)
	{
		Point ret;
		ret.x = p.x * xx + xy * p.y + x0;
		ret.y = p.y * yy + yx * p.x + y0;
		return ret;
	}

	void Mul(Matrix m)
	{
		Matrix tmp;
		tmp.xx = xx*m.xx + yx*m.xy;
		tmp.yx = xx*m.yx + yx*m.yy;

		tmp.xy = xy*m.xx + yy*m.xy;
		tmp.yy = xy*m.yx + yy*m.yy;

		tmp.x0 = x0*m.xx + y0*m.xy + m.x0;
		tmp.y0 = y0*m.yx + y0*m.yy + m.y0;
		*this = tmp;
	}
	Matrix() { xx = 1; yx = 0; xy = 0; yy = 1; x0 = 0; y0 = 0;}

	Matrix(float radians)
	{
		float s = sin(radians);
		float c = cos(radians);

		xx = c;
		yx = s;
		xy = -s;
		yy = c;
		x0 = 0;
		y0 = 0;
	}
};

#endif
