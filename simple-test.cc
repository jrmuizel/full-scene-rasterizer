#include "bird.h"
#ifdef EMSCRIPTEN
#define EMCC
#endif
#include <assert.h>
#ifndef EMCC
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif
#include <unistd.h>
#include <new>
#include "lenna.h"

//#define CANVAS
#define WEBGL

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "rasterizer.h"
#include "minpng.h"
#include "matrix.h"
#ifdef EMCC
extern "C" {
	extern void emscripten_set_canvas_size(int, int);
	extern void canvas_create_context();
	extern void canvas_set_fill(double, double, double, double);
	extern void canvas_move_to(double, double);
	extern void canvas_line_to(double, double);
	extern void canvas_quad_to(double, double, double, double);
	extern void canvas_fill();
	extern void canvas_put_image(void *, int, int);
	extern void webgl_put_image(void *, int, int);
	extern void webgl_init();
	extern void canvas_begin_time();
	extern void canvas_end_time();
	extern void canvas_clear();
	extern void drawFrame(int);
	extern void init();
	extern void fini();
}
#endif

Rasterizer *rast;
int width;
int height;
void drawFrame(int angle)
{
		PathBuilder pb;
		pb.r = rast;

                Point p;
                p.x = 50;
                p.y = 50;
                pb.move_to(p);

                p.x = 100;
                p.y = 70;
                pb.line_to(p);

                p.x = 110;
                p.y = 150;
                pb.line_to(p);

                p.x = 40;
                p.y = 180;
                pb.line_to(p);

                pb.close();

		rast->rasterize();
		mach_timebase_info_data_t timebaseInfo;
		// Apple's QA1398 suggests that the output from mach_timebase_info
		// will not change while a program is running, so it should be safe
		// to cache the result.
		kern_return_t kr = mach_timebase_info(&timebaseInfo);

		double sNsPerTick = double(timebaseInfo.numer) / timebaseInfo.denom;
		long long end = mach_absolute_time();
		char filename[300];
		sprintf(filename, "out%d.png", angle);
		write_png(filename, rast->buf, width, height);

		rast->reset();
}

void init() {
	width = 1520;
	 height = 1080;

	rast = new Rasterizer(width, height);
}

void fini()
{
	delete rast;
}
int main(int argc, char **argv)
{

	init();
        drawFrame(0);
	fini();
}
