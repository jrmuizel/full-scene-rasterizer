#include <new>
#include "minpng.h"
#include "rasterizer.h"
int main()
{
	GradientStop stops[5];
	stops[0].position = 0.25;
	stops[1].position = 0.75;
	stops[2].position = 1;
	stops[0].color = 0xffff0000;
	stops[1].color = 0xff00ff00;
	stops[2].color = 0xffffff00;
	uint32_t out[257];
	build_lut(stops, 3, out);
	write_png("out.png", out, 257, 1);
}

