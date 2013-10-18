#include <stdint.h>
#include <stdlib.h>
#include "skia-utils.h"
#include "arena.h"

struct Point
{
	Point(float x, float y) : x(x), y(y) {}
	Point() {}
	float x;
	float y;
};

struct Color
{
	int r;
	int g;
	int b;
	int a;
};

// A class that we use for computation of intermediate
// color values. We use this to accumulate the results
// of 4x4 subpixels. For this to be exact we need
// to be able to store 16*255 or 4 extra bits per component.
struct Intermediate
{
	// use a SWAR approach:
	//      aaaaaaaa rrrrrrrr gggggggg bbbbbbbb
	// ag = aaaaaaaaaaaaaaaaa ggggggggggggggggg
	// rb = rrrrrrrrrrrrrrrrr bbbbbbbbbbbbbbbbb
	//
	// This cuts the number of additions in half,
	// is more compact and easier to finalize,
	// into back into argb
	int ag;
	int rb;

	Intermediate() : ag(0), rb(0)
	{
	}

	void accumulate(Intermediate i)
	{
		ag += i.ag;
		rb += i.rb;
	}

	// XXX: this needs to be fleshed out
	// how do we do 'over' with immediates'
	Intermediate
	over(Intermediate c)
	{
		if ((c.ag & 0xff0000) == 0xff0000) {
			this->ag = c.ag;
			this->rb = c.rb;
		} else {
			// a fast approximation of OVER
			int alpha = 0xff - (c.ag >> 16);
			this->ag = (((this->ag * alpha) >> 8) & 0xff00ff) + c.ag;
			this->rb = (((this->rb * alpha) >> 8) & 0xff00ff) + c.rb;
		}
		return *this;
	}

	void
	assign(Color c)
	{
		this->ag = c.a << 16 | c.g;
		this->rb = c.r << 16 | c.b;
	}

	uint32_t finalize_unaccumulated() {
		return (ag << 8) | rb;
	}

	uint32_t finalize() {
		uint32_t result;
		result  = (ag << 4) & 0xff00ff00;
		result |= (rb >> 4) & 0x00ff00ff;
		return result;
	}
};



struct Span;
struct Gradient
{
	Intermediate color;
	Intermediate eval(int x, int y);
};

struct Shape
{
	Shape() {}
	int fill_style;
	bool opaque;
	// we can union the different fill style implementations here.
	// e.g. a pointer to an image fill or gradient fill
	union {
		Intermediate color;
		Gradient *gradient;
	};
	void (*fill)(Shape *s, uint32_t *buf, int x, int y, int w);
	int winding;
	int z;
#ifndef NDEBUG
	Shape *next;
#endif
	Span *span_begin;
};

extern void solid_fill(Shape *s, uint32_t *buf, int x, int y, int w);
extern void gradient_fill(Shape *s, uint32_t *buf, int x, int y, int w);

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

	void begin_gradient(float r, float g, float b, float a)
	{
		shape = new (this->shape_arena.alloc(sizeof(Shape))) Shape;
#ifndef NDEBUG
		shape->next = 0;
#endif
		Color c;
		shape->fill_style = 1;
		shape->fill = gradient_fill;
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


