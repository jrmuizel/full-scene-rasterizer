#ifndef RASTERIZER_H
#define RASTERIZER_H
#include <stdint.h>
#include <stdlib.h>
#include "skia-utils.h"
#include "arena.h"
#include "types.h"
#include "shader.h"

extern void solid_fill(Shape *s, uint32_t *buf, int x, int y, int w);
extern void gradient_fill(Shape *s, uint32_t *buf, int x, int y, int w);
extern Intermediate gradient_eval(Shape *s, int x, int y);

struct ActiveEdge;
struct Rasterizer
{
	Rasterizer(int width, int height);
	~Rasterizer() { delete[] edge_starts; };
	void add_edge(Point start, Point end, Shape *shape, bool curve = false, Point control = Point(0,0));
	void add_edges(Point *p, int count, Shape *shape);
        void insert_starting_edges();
	void step_edges();
	void scan_edges();
        void sort_edges();
	void paint_spans();
	void rasterize();
	void reset();
	void check_windings();

	ActiveEdge **edge_starts;
	int width;
	int height;
	int cur_y;
	uint32_t *buf;
	Shape *shapes;
	ActiveEdge *active_edges;
	// We currently have 4 span lists. We walk
	// through all of the at the same time accumulating
	// a pixel color as we go.
	Span* spans[4];
	ArenaPool span_arena;
	ArenaPool edge_arena;
};

struct PathBuilder
{
	PathBuilder()
	{
		shape = NULL;
		z = 0;
	}
	Point current_point;
	Point first_point;
	Shape *shape;
	Rasterizer *r;
	ArenaPool shape_arena;
	ArenaPool gradient_arena;
	int z;
	void begin(float r, float g, float b, float a)
	{
		shape = new (this->shape_arena.alloc(sizeof(Shape))) Shape;
#ifndef NDEBUG
		shape->next = 0;
#endif
		Color c;
		shape->fill_style = 0;
		// XXX: support alpha
		c.r=r*255*a;
		c.g=g*255*a;
		c.b=b*255*a;
		c.a=a*255;
		if (c.a == 255)
			shape->opaque = true;
		shape->color.assign(c);
		shape->z = z++;
		shape->winding = 0;
		shape->fill = solid_fill;

	}
#if 0
	void begin_gradient(float r, float g, float b, float a)
	{
		shape = new (this->shape_arena.alloc(sizeof(Shape))) Shape;
#ifndef NDEBUG
		shape->next = 0;
#endif
		Color c;
		shape->fill_style = 1;
		shape->fill = gradient_fill;
		shape->eval = gradient_eval;
		shape->gradient = new (this->gradient_arena.alloc(sizeof(Gradient))) Gradient;
		c.r=r*255*a;
		c.g=g*255*a;
		c.b=b*255*a;
		c.a=a*255;
		shape->opaque = false;
		shape->gradient->color.assign(c);
		shape->z = z++;
		shape->winding = 0;

	}
#endif
	void begin_bitmap(Bitmap *b)
	{
		shape = new (this->shape_arena.alloc(sizeof(Shape))) Shape;
#ifndef NDEBUG
		shape->next = 0;
#endif
		shape->fill_style = 1;
		shape->fill = generic_opaque_fill;
		shape->eval = bitmap_nearest_eval;
		shape->bitmap = b;
		shape->opaque = true;
		shape->z = z++;
		shape->winding = 0;

	}

	void begin_radial(RadialGradient *r)
	{
		shape = new (this->shape_arena.alloc(sizeof(Shape))) Shape;
#ifndef NDEBUG
		shape->next = 0;
#endif
		shape->fill_style = 1;
		shape->fill = generic_over_fill;
		shape->eval = radial_gradient_eval;
		shape->radial_gradient = r;
		shape->opaque = false;
		shape->z = z++;
		shape->winding = 0;

	}

	void begin_linear(LinearGradient *l)
	{
		shape = new (this->shape_arena.alloc(sizeof(Shape))) Shape;
#ifndef NDEBUG
		shape->next = 0;
#endif
		shape->fill_style = 1;
		shape->fill = generic_over_fill;
		shape->eval = linear_gradient_eval;
		shape->linear_gradient = l;
		shape->opaque = false;
		shape->z = z++;
		shape->winding = 0;
	}

	void close()
	{
		r->add_edge(current_point, first_point, shape);
	}
	void move_to(Point p)
	{
		current_point = p;
		first_point = p;
	}

	void line_to(Point p)
	{
		r->add_edge(current_point, p, shape);
		current_point = p;
	}

	void quad_to(Point control, Point p)
	{
		Sk2Point in[3] = {{current_point.x, current_point.y},
				 {control.x, control.y},
				 {p.x, p.y}};
		Sk2Point out[5];
		int n = Sk2ChopQuadAtYExtrema(in, out);
		if (n == 0) {
			r->add_edge(current_point, p, shape, true, control);
		} else {
			Point a = {out[1].fX, out[1].fY};
			Point b = {out[2].fX, out[2].fY};
			Point c = {out[3].fX, out[3].fY};
			r->add_edge(current_point, b, shape, true, a);
			r->add_edge(b, p, shape, true, c);
		}
		current_point = p;
	}
};

#endif
