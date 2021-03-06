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
#ifndef EMCC
	long long start = mach_absolute_time();
#else
	canvas_begin_time();
#ifdef CANVAS
	canvas_clear();
#endif
#endif
#ifndef CANVAS
		PathBuilder p;
		p.r = rast;
#endif
		Matrix m;
		m.x0 = -25;
		m.y0 = -30;
		//Matrix rot(2*3.14159*45/360.);
		Matrix rot(2*3.141459*20*angle/360.);
		m.Mul(rot);
		Matrix trans;
		trans.x0 = 25;
		trans.y0 = 30;
		m.Mul(trans);
		m.x0 += 500;
		m.y0 += 500;
		bool ltr = true;
		//printf("%f %f %f %f %f %f\n", m.xx, m.xy, m.yx, m.yy, m.y0, m.x0);
		//printf("%f %f %f %f %f %f\n", rot.xx, rot.xy, rot.yx, rot.yy, rot.y0, rot.x0);
		float scale = 1;
		m.y0 -= 400;
		for (int col = 0; col < 10; col++) {
			if (ltr)
				m.x0 += -300;
			else
				m.x0 += 800;
			for (int row = 0; row < 12; row++) {
				int i=0;
				double *d = data;
		int k = 0;
				while (i < sizeof(commands)) {
					char command = commands[i];
					if (command == 'f') {
						double r = *d++;
						double g = *d++;
						double b = *d++;
						double a = *d++;
						//printf("rgba %f %f %f %f\n", r,g,b,a);
#ifdef CANVAS
						canvas_set_fill(r,g,b,a);
#else
						p.begin(r,g,b,a);
#endif
						i++;
						while (i < sizeof(commands)) {
							char command = commands[i];
							if (command == 'm') {
								i++;
								//printf("'m',");
								float x = *d++; float y = *d++;
								//printf("m %f %f\n", x, y);
								Point end = {x, y};
								//cairo_move_to(cr, x, y);
								end = m.Mul(end);
#ifdef CANVAS
								canvas_move_to(end.x, end.y);
#else
								p.move_to(end);
#endif
								//path.moveTo(x/20+500, y/20+500);
							} else if (command == 'l') {
								i++;
								float x = *d++; float y = *d++;
								//printf("'l',");
								//	printf("l %f, %f,\n", x, y);
								Point end = {x, y};
								end = m.Mul(end);
#ifdef CANVAS
								canvas_line_to(end.x, end.y);
#else
								p.line_to(end);
#endif
								k++;
							} else if (command == 'c') {
								i++;
								float cx = *d++;
								float cy = *d++;
								float dx = *d++;
								float dy = *d++;
								//	printf("c %f %f %f %f\n", cx, cy, dx, dy);
								Point control;
								Point end = {dx, dy};
								control.x = (cx);
								control.y = (cy);
								k++;
								control = m.Mul(control);
								end = m.Mul(end);
								//printf("'c',");
								//printf("%f, %f, %f, %f,\n", control.x, control.y, end.x, end.y);
#ifdef CANVAS
								canvas_quad_to(control.x, control.y, end.x, end.y);
#else
								p.quad_to(control, end);
#endif
								//cairo_quad_to(cr ,cx, cy, dx, dy);
							} else {
								break;
							}
						}
#ifdef CANVAS
						canvas_fill();
#endif
						//cairo_fill(cr);
					}
				}
				if (ltr)
					m.x0 += 100;
				else
					m.x0 -= 100;
				//translate_y = 0;
			}
			if (ltr)
				m.x0 -= 900;
			else
				m.x0 += 400;
			m.y0 += 100;
		}
		//start = mach_absolute_time();
		rast->rasterize();
#ifdef EMCC
		canvas_end_time();
#ifndef CANVAS
#ifdef WEBGL
		webgl_put_image(rast->buf, width, height);
#else
		canvas_put_image(rast->buf, width, height);
#endif
#endif
#else
		mach_timebase_info_data_t timebaseInfo;
		// Apple's QA1398 suggests that the output from mach_timebase_info
		// will not change while a program is running, so it should be safe
		// to cache the result.
		kern_return_t kr = mach_timebase_info(&timebaseInfo);

		double sNsPerTick = double(timebaseInfo.numer) / timebaseInfo.denom;
		long long end = mach_absolute_time();
		printf("%0.3f\n", (end-start)/(1000*1000.));
		char filename[300];
		sprintf(filename, "out%d.png", angle);
		write_png(filename, rast->buf, width, height);

#endif
		rast->reset();
}

void init() {
	width = 1520;
	 height = 1080;

#ifdef EMCC
        emscripten_set_canvas_size(width, height);
#ifdef WEBGL
	webgl_init();
#else
	canvas_create_context();
#endif
#else
#endif
	rast = new Rasterizer(width, height);
}

void fini()
{
	delete rast;
}
int main(int argc, char **argv)
{

	init();
	for (int angle = 0; angle < 10; angle++) {
		drawFrame(angle);
	}
	fini();
}
