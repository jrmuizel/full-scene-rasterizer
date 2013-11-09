/* Copyright 2013 Jeff Muizelaar
 *
 * Use of this source code is governed by a MIT-style license that can be
 * found in the LICENSE file.
 *
 * Portions Copyright 2006 The Android Open Source Project
 *
 * Use of that source code is governed by a BSD-style license that can be
 * found in the LICENSE.skia file.
 */

#include <algorithm>
#include <assert.h>
#include <vector>
#include <stdio.h>
#include <string.h>
#include "rasterizer.h"

using namespace std;

// One reason to have separate Edge/ActiveEdge is reduce the
// memory usage of inactive edges. On the other hand
// managing the lifetime of ActiveEdges is a lot
// trickier than Edges. Edges can stay alive for the entire
// rasterization. ActiveEdges will come and go in a much
// less predictable order. On the other hand having the
// ActiveEdges close together in memory would help
// avoid cache misses. If we did switch to having separate
// active edges it might be wise to store the active edges
// in an array instead of as a linked list. This will work
// well for the bubble sorting, but will cause more problems
// for insertion.

struct Edge {
	//XXX: it is probably worth renaming this to top and bottom
	int x1;
	int y1;
	int x2;
	int y2;
	int control_x;
	int control_y;
};


// it is possible to fit this into 64 bytes on x86-64
// with the following layout:
//
// 4 x2,y2
// 8 shape
// 8 next
// 6*4 slope_x,fullx,next_x,next_y, old_x,old_y
// 4*4 dx,ddx,dy,ddy
// 2 cury
// 1 count
// 1 shift
//
// some example counts 5704 curves, 1720 lines 7422 edges
struct ActiveEdge {
	int x2;
	int y2;
	Shape *shape;
	ActiveEdge *next;
	int slope_x;
	int fullx;
	int next_x;
	int next_y;

	int dx;
	int ddx;
	int dy;
	int ddy;


	int old_x;
	int old_y;

	int shift;
	// we need to use count so that we make sure that we always line the last point up
	// exactly. i.e. we don't have a great way to know when we're at the end implicitly.
	int count;

	// we want this to inline into step_edges() to
	// avoid the call overhead
	inline __attribute__((always_inline)) void step(int cury) {
		// if we have a shift that means we have a curve
		if (shift) {
			//printf("inner cur %d,%d next %d %d %f\n", curx, cury, next_x>>16, next_y>>16, fnext_y);
			if (cury >= (next_y>>16)) {
				old_y = next_y;
				old_x = next_x;
				fullx = next_x;
				// increment until we have a next_y that's greater
				while (count > 0 && (cury >= (next_y>>16))) {
					next_x += dx >> shift;
					dx += ddx;
					next_y += dy >> shift;
					dy += ddy;
					count--;
				}
				if (!count) {
					// for the last line sgement we can
					// just set next_y,x to the end point
					next_y = y2<<16;
					next_x = x2<<16;
				}
				// update slope if we're going to be using it
				// we want to avoid dividing by 0 which can happen if we exited the loop above early
				if ((cury+1) < y2) {
					// the maximum our x value can be is 4095 (which is 12 bits).
					// 12 + 3 + 16 = 31 which gives us an extra bit of room
					// to handle overflow.
					slope_x = ((next_x - old_x)<<3)/((next_y - old_y)>>13);
				}
			}
			fullx += slope_x;
		} else {
			// XXX: look into bresenham to control error here
			fullx += slope_x;
		}
		cury += 1;
	}
};


Rasterizer::Rasterizer(int width, int height) : width(width*4), height(height*4),
	cur_y(0),
	buf(nullptr),
	shapes(nullptr),
	active_edges(nullptr)
{
	edge_starts = new ActiveEdge*[this->height];
	for (int i = 0; i < this->height; i++) {
		edge_starts[i] = NULL;
	}
	buf = new uint32_t[width*height];
	memset(buf, 0, width*height*4);
}

void
Rasterizer::reset() {
	cur_y = 0;
	active_edges = nullptr;
	shapes = nullptr;
	for (int i = 0; i < this->height; i++) {
		edge_starts[i] = NULL;
	}
}

static inline int32_t Sk2Abs32(int32_t value) {
#ifdef SK_CPU_HAS_CONDITIONAL_INSTR
    if (value < 0)
        value = -value;
    return value;
#else
    int32_t mask = value >> 31;
    return (value ^ mask) - mask;
#endif
}

int Sk2CLZ(uint32_t x) {
    if (x == 0) {
        return 32;
    }

#ifdef SK_CPU_HAS_CONDITIONAL_INSTR
    int zeros = 31;
    if (x & 0xFFFF0000) {
        sub_shift(zeros, x, 16);
    }
    if (x & 0xFF00) {
        sub_shift(zeros, x, 8);
    }
    if (x & 0xF0) {
        sub_shift(zeros, x, 4);
    }
    if (x & 0xC) {
        sub_shift(zeros, x, 2);
    }
    if (x & 0x2) {
        sub_shift(zeros, x, 1);
    }
#else
    int zeros = ((x >> 16) - 1) >> 31 << 4;
    x <<= zeros;

    int nonzero = ((x >> 24) - 1) >> 31 << 3;
    zeros += nonzero;
    x <<= nonzero;

    nonzero = ((x >> 28) - 1) >> 31 << 2;
    zeros += nonzero;
    x <<= nonzero;

    nonzero = ((x >> 30) - 1) >> 31 << 1;
    zeros += nonzero;
    x <<= nonzero;

    zeros += (~x) >> 31;
#endif

    return zeros;
}

static inline int cheap_distance(int dx, int dy)
{
    dx = Sk2Abs32(dx);
    dy = Sk2Abs32(dy);
    // return max + min/2
    if (dx > dy)
        dx += dy >> 1;
    else
        dx = dy + (dx >> 1);
    return dx;
}

static inline int diff_to_shift(int dx, int dy)
{
    //printf("diff_to_shift: %d %d\n", dx, dy);
    // cheap calc of distance from center of p0-p2 to the center of the curve
    int dist = cheap_distance(dx, dy);

    //printf("dist: %d\n", dist);
    // shift down dist (it is currently in dot6)
    // down by 5 should give us 1/2 pixel accuracy (assuming our dist is accurate...)
    // this is chosen by heuristic: make it as big as possible (to minimize segments)
    // ... but small enough so that our curves still look smooth
    //printf("%d dist\n", dist);
    dist = (dist + (1 << 4)) >> 5;

    // each subdivision (shift value) cuts this dist (error) by 1/4
    return ((32 - Sk2CLZ(dist)))>> 1;
}

// this metric is take from skia
static int compute_curve_steps(Edge *e)
{
	int dx = ((e->control_x << 1) - e->x1 - e->x2);
	int dy = ((e->control_y << 1) - e->y1 - e->y2);
	int shift = diff_to_shift(dx<<4, dy<<4);
	assert(shift >= 0);
	return shift;
}

#define SAMPLE_SIZE 4
#define SAMPLE_SHIFT 2
// An example number of edges is 7422 but
// can go as high as edge count: 374640
// with curve count: 67680
void
Rasterizer::add_edge(Point start, Point end, Shape *shape, bool curve, Point control)
{
	//static int count;
	//printf("edge count: %d\n",++count);
	// order the points from top to bottom
	if (end.y < start.y) {
		swap(start, end);
	}

	// how do we deal with edges to the right and left of the canvas?
	ActiveEdge *e = new (this->edge_arena.alloc(sizeof(ActiveEdge))) ActiveEdge;
	e->shape = shape;
	Edge edge;
	edge.x1 = start.x * SAMPLE_SIZE;
	edge.y1 = start.y * SAMPLE_SIZE;
	edge.control_x = control.x * SAMPLE_SIZE;
	edge.control_y = control.y * SAMPLE_SIZE;
	edge.x2 = end.x * SAMPLE_SIZE;
	edge.y2 = end.y * SAMPLE_SIZE;
	e->x2 = edge.x2;
	e->y2 = edge.y2;
#if 0
	if (curve)
	printf("%d %d, %d %d, %d %d\n",
	       e->edge.x1,
	       e->edge.y1,
	       e->edge.control_x,
	       e->edge.control_y,
	       e->edge.x2,
	       e->edge.y2);
#endif
	e->next = nullptr;
	//e->curx = e->edge.x1;
	int cury = edge.y1;
	e->fullx = edge.x1 << 16;

	// if the edge is completely above or completely below we can drop it
	if (edge.y2 < 0 || edge.y1 > height)
		return;

	// drop horizontal edges
	if (cury >= e->y2)
		return;

	if (curve) {
		// Based on Skia
		// we'll iterate t from 0..1 (0-256)
		// range of A is 4 times coordinate-range
		// we can get more accuracy here by using the input points instead of the rounded versions
		int A = (edge.x1 - edge.control_x - edge.control_x + edge.x2)<<15;
		int B = (edge.control_x - edge.x1);
		int C = edge.x1;
		int shift = compute_curve_steps(&edge);
		e->shift = shift;
		e->count = 1<<shift;
		e->dx = 2*A*(1<<(16-shift)) + B*65536;
		e->dx = 2*(A>>shift) + 2*B*65536;
		e->ddx = 2*(A >> (shift-1));

		A = (edge.y1 - edge.control_y - edge.control_y + edge.y2)<<15;
		B = (edge.control_y - edge.y1);
		C = edge.y1;
		e->dy = 2*A*(1<<(16-shift)) + (B)*65536;
		e->ddy = 2*A*(1<<(16-shift));
		e->dy = 2*(A>>shift) + 2*B*65536;
		e->ddy = 2*(A >> (shift-1));

		// compute the first next_x,y
		e->count--;
		e->next_x = (e->fullx) + (e->dx>>e->shift);
		e->next_y = (cury*65536) + (e->dy>>e->shift);
		e->dx += e->ddx;
		e->dy += e->ddy;

		// skia does this part in UpdateQuad. unfortunately we duplicate it
		while (e->count > 0 && cury >= (e->next_y>>16)) {
			e->next_x += e->dx>>shift;
			e->dx += e->ddx;
			e->next_y += e->dy>>shift;
			e->dy += e->ddy;
			e->count--;
		}
		if (!e->count) {
			e->next_y = edge.y2<<16;
			e->next_x = edge.x2<<16;
		}
		e->slope_x = ((e->next_x - (e->fullx))<<2)/((e->next_y - (cury<<16))>>14);
	} else {
		e->shift = 0;
		e->slope_x = ((edge.x2 - edge.x1)*(1<<16))/(edge.y2 - edge.y1);
	}

	if (cury < 0) {
		// XXX: we could compute an intersection with the top and bottom so we don't need to step them into view
		// for curves we can just step them into place.
		while (cury < 0) {
			e->step(cury);
			cury++;
		}

		// cury was adjusted so check again for horizontal edges
		if (cury >= e->y2)
			return;
	}

	// add to the begining of the edge start list
	// if edges are added from left to right
	// the'll be in this list from right to left
	// this works out later during insertion
	e->next = edge_starts[cury];
	edge_starts[cury] = e;
}

void
Rasterizer::add_edges(Point *p, int count, Shape *shape)
{
	for (int i = 1; i < count; i++) {
		Point start = p[i-1];
		Point end = p[i];
		add_edge(start, end, shape);
	}
	Point control = {2,12};
	add_edge(p[count-1], p[0], shape, true, control);

#ifndef NDEBUG
	// add to shape list
	shape->next = this->shapes;
	this->shapes = shape;
#endif
}

void
Rasterizer::step_edges()
{
	ActiveEdge **prev_ptr = &this->active_edges;
	ActiveEdge *edge = this->active_edges;
	int cury = cur_y; // avoid any aliasing problems
	while (edge) {
		edge->step(cury);
		// avoid aliasing between edge->next and prev_ptr so that we can reuse next
		ActiveEdge *next = edge->next;
		// remove any finished edges
		if ((cury+1) >= edge->y2) {
			// remove from active list
			*prev_ptr = next;
		} else {
			prev_ptr = &edge->next;
		}
		edge = next;
	}
}

int comparisons;
static inline void dump_edges(ActiveEdge *e)
{
	while (e) {
		printf("%d ", e->fullx);
		e = e->next;
	}
	printf("\n");
}

// Insertion sort the new edges into the active list
// The new edges could be showing up at any x coordinate
// but existing active edges will be sorted.
//
// Merge in the new edges. Since both lists are sorted we can do
// this in a single pass.
// Note: we could do just O(1) append the list of new active edges
// to the existing active edge list, but then we'd have to sort
// the entire resulting list
void
Rasterizer::insert_starting_edges()
{
	ActiveEdge *new_edges = nullptr;
	ActiveEdge *e = edge_starts[this->cur_y];
	// insertion sort all of the new edges
	while (e) {
		ActiveEdge **prev_ptr = &new_edges;
		ActiveEdge *a = new_edges;
		while (a && e->fullx > a->fullx) {
			// comparisons++;
			prev_ptr = &a->next;
			a = a->next;
		}
		*prev_ptr = e;
		e = e->next;
		(*prev_ptr)->next = a;
	}

	// merge the sorted new_edges into active_edges
	ActiveEdge **prev_ptr = &active_edges;
	ActiveEdge *a = active_edges;
	e = new_edges;
	while (e) {
		while (a && e->fullx > a->fullx) {
			// comparisons++;
			prev_ptr = &a->next;
			a = a->next;
		}
		*prev_ptr = e;
		e = e->next;
		(*prev_ptr)->next = a;
		prev_ptr = &((*prev_ptr)->next);
	}
}

void
Rasterizer::check_windings()
{
#ifdef NDEBUG
	return;
#else
	Shape *s = shapes;
	while (s) {
		assert(s->winding == 0);
		s = s->next;
	}
#endif
}

#define MAX_COLORS 16
// we can blend together colors that are less then the max
//
// Originally this took the approach of a list of accumulating colors that were added or removed.
// The problem with this approach is that one needs to keep all of the color around. Because we
// don't know when they will be removed. Alternatively, if
// we don't add colors to the span until we know
// the end point we know don't need to worry about keeping the remaining colors around.
//
// If we drop S1 because S2 is on top then we won't have it
// when S2 finishes.
//
//   |     ---------
//   |    S2       S2
// z |  ----------------
//   | S1              S1
//   ---------------------
//          x

// we should aim for this to be 64 bytes
struct Span
{
	Span() : x_end(0), shape_count(0), next(nullptr) {}
	void add_color(Shape *);
	Intermediate compute_color(int x, int y);
	Span* split(int x);

	int x_end;
	int shape_count;
	Span *next;
#define SPAN_COLORS 8
	Shape* shapes[SPAN_COLORS];
};

/* If we're only handling opaque colors this can
 * be simplified greatly */
inline __attribute__((always_inline))
void
Span::add_color(Shape *s)
{
	assert(shape_count < SPAN_COLORS);
	if (!shape_count) {
		shapes[0] = s;
		shape_count = 1;
		return;
	}

	// if opaque and a higher z then just replace
	if (s->opaque && shapes[shape_count-1]->z < s->z) {
		shapes[0] = s;
		shape_count = 1;
		return;
	}

	// if the top shape is opaque just discard
	if (shapes[shape_count-1]->opaque && shapes[shape_count-1]->z > s->z)
		return;

	// if we're above everything else
	if (shapes[shape_count-1]->z < s->z) {
		shapes[shape_count++] = s;
		return;
	}

	int i;
	// otherwise find out where in the array to insert it
	for (i=0; i<shape_count; i++) {
		if (shapes[i]->z > s->z) {
			break;
		}
	}

	// move all of the shapes to make room
	for (int j=shape_count; j>i; j--) {
		shapes[j] = shapes[j-1];
	}
	shapes[i] = s;
	shape_count++;

#if 0
	// insert s into shapes in descending z order
	// XXX: it may make sense for this array to go in the other direction
	// depending on what order we tend to add colors
	auto it = shapes.begin();
	for (; it != shapes.end(); it++) {
		if ((*it)->z < s->z) {
			if (s->color.a == 255) {
				*it = s;
				return;
			}
			break;
		}
	}
	shapes.insert(it, s);
#endif
}

// XXX: Ideally we want to exploit the fact that there will be a lot of
// correlation between previous scans of the edge.  For super-sampling we want
// to advantage of the fact that many of the spans in a run will have large
// sections where all four scans match up. For now we search for the coherence
// in paint_spans.
void Rasterizer::scan_edges()
{
	this->check_windings();

	// These Spans last until the end of paint_spans()
	Span *s = new (this->span_arena.alloc(sizeof(Span))) Span();
	this->spans[this->cur_y % 4] = s;

	ActiveEdge *edge = this->active_edges;
	// handle edges that begin to the left of the bitmap
	while (edge && edge->fullx < 0) {
		// we only need to keep track of the shapes that are live starting at 0
		if (++edge->shape->winding & 1) {
			// we're entering a shape
			// mark the starting place in the shape
			edge->shape->span_begin = s;
		}
		// leaving a shape can be ignored because we'll clobber shape->span_begin when
		// we enter it again
		edge = edge->next;
	}

	while (edge) {
		// XXX: there's no point in adding spans beyond the end
		// it might be nice to move this kind of test out of scan
		// edges so that we only start the edge when it enters the
		// draw area.
		if ((edge->fullx>>16) >= width)
			break;

		// finish the last span
		s->x_end = edge->fullx>>16;

		Span *s_next = new (this->span_arena.alloc(sizeof(Span))) Span();

		// loop for all edges at the same co-ordinate
		do {
			if (++edge->shape->winding & 1) {
				// we're entering a shape
				// mark the starting place in the shape
				edge->shape->span_begin = s_next;
			} else {
				// we're exiting a shape
				// add the color to all the spans starting at
				// span_begin
				Span *q = edge->shape->span_begin;
				// only color the span if starts before this span
				if (q != s_next)
				{
					// we usually only loop once because content tends
					// not overlap
					while (q) {
						q->add_color(edge->shape);
						q = q->next;
					}
				}
			}
			if (edge->next)
				assert(edge->fullx <= edge->next->fullx);
			edge = edge->next;
		} while (edge && (edge->fullx >> 16) == s->x_end);

		s->next = s_next;
		s = s_next;

	}

	// finish up any remaining edges >= width
	while (edge) {
		if (++edge->shape->winding & 1) {
			// any new beginings can be ignored
			edge->shape->span_begin = nullptr;
		} else {
			// we're exiting a shape
			// add the color to all the spans starting at
			// span_begin
			Span *q = edge->shape->span_begin;
			// we usually only loop once because content tends
			// not overlap
			while (q) {
				q->add_color(edge->shape);
				q = q->next;
			}
		}
		edge = edge->next;
	}

	// mark the end of the final span
	s->x_end = width;
}

// You may have heard that one should never use a bubble sort.
// However in our situation a bubble sort is actually a good choice.
// The input list will be mostly sorted except for a couple of lines
// that have need to be swapped. Further it is common that our edges are
// already sorted and bubble sort lets us avoid doing any memory writes.

// Some statistics from using a bubble sort on an
// example scene. You can see that bubble sort does
// noticably better than O (n lg n).
// summary(edges*bubble_sort_iterations)
//   Min. 1st Qu.  Median    Mean 3rd Qu.    Max.
//    0.0     9.0    69.0   131.5   206.0  1278.0
// summary(edges*log2(edges))
//   Min. 1st Qu.  Median    Mean 3rd Qu.    Max.    NA's
//   0.00   28.53  347.10  427.60  787.20 1286.00    2.00
void
Rasterizer::sort_edges()
{
	if (!this->active_edges)
		return;
	bool swapped;
	do {
		swapped = false;
		ActiveEdge *edge = this->active_edges;
		ActiveEdge *next = edge->next;
		ActiveEdge **prev = &this->active_edges;
		while (next) {
			if (edge->fullx > next->fullx) {
				// swap edge and next
				(*prev) = next;
				edge->next = next->next;
				next->next = edge;
				swapped = true;
			}
			prev = &edge->next;
			edge = next;
			next = edge->next;
		}
	} while (swapped);
}

// we should compute this in reverse
inline __attribute__((always_inline))
Intermediate
Span::compute_color(int x, int y)
{
	Intermediate intermediate;
	for (int i=0; i<shape_count; i++) {
		if (shapes[i]->fill_style == 0)
			intermediate = intermediate.over(shapes[i]->color);
		else
			intermediate = intermediate.over(shapes[i]->eval(shapes[i], x>>SAMPLE_SHIFT, y>>SAMPLE_SHIFT));
	}
	return intermediate;
}

// We use this to accumulate horizontally neighbouring sub-pixels
// and write them out to buf when complete
struct SubpixelQueue
{
	uint32_t *buf;
	int count;
	Intermediate value;

	void queue(Intermediate i)
	{
		value.accumulate(i);
		if (count++ == 3) {
			// if we've moved on to another pixel
			// finalize the previous one and store
			// it in the buffer
			*buf++ = value.finalize();
			value = Intermediate();
			count = 0;
		}
	}
};
#define MULTI
#ifdef MULTI
#if 0
static bool is_coherent(Span *s1, Span *s2, Span *s3, Span *s4)
{
	if (s1->shape_count != s2->shape_count || s1->shape_count != s3->shape_count || s1->shape_count != s4->shape_count)
		return false;
	for (int i=0; i < s1->shape_count; i++) {
		if (s1->shapes[i] != s2->shapes[i] | s1->shapes[i] != s3->shapes[i] | s1->shapes[i] != s4->shapes[i])
			return false;
	}
	return true;
}
#else
// we are usually coherent so we don't wat to shortcut this function more than necessary
bool is_coherent(Span *s1, Span *s2, Span *s3, Span *s4)
{
	if (s1->shape_count != s2->shape_count | s1->shape_count != s3->shape_count | s1->shape_count != s4->shape_count)
		return false;
	bool coherent = true;
	for (int i=0; i < s1->shape_count; i++) {
		coherent = coherent & (s1->shapes[i] == s2->shapes[i]);
		coherent = coherent & (s1->shapes[i] == s3->shapes[i]);
		coherent = coherent & (s1->shapes[i] == s4->shapes[i]);
	}
	return coherent;
}
#endif
#else
static bool is_coherent(Span *s1, Span *s2)
{
	if (s1->shape_count != s2->shape_count)
		return false;
	for (int i=0; i < s1->shape_count; i++) {
		if (s1->shapes[i] != s2->shapes[i])
			return false;
	}
	return true;
}
#endif


static bool is_solid(Span *s)
{
	for (int i=0; i < s->shape_count; i++) {
		if (s->shapes[i]->fill_style != 0)
			return false;
	}
	return true;
}

void
Rasterizer::paint_spans()
{
	SubpixelQueue output;
	output.buf = &buf[cur_y/4*this->width/4];
	output.count = 0;
	int start_x = 0;
	Span *s1 = this->spans[0];
	Span *s2 = this->spans[1];
	Span *s3 = this->spans[2];
	Span *s4 = this->spans[3];
	// XXX: having the begin of the span means that things are a little tricky
	// for computing the color of the current spans
	while (s1 && s1 && s3 && s4) {
		Intermediate c;
		int min = s1->x_end;
		if (s2->x_end < min)
			min = s2->x_end;
		if (s3->x_end < min)
			min = s3->x_end;
		if (s4->x_end < min)
			min = s4->x_end;


		int w = min - start_x;
		int x = start_x;
		if (1) {
#if 0
			bool solid = true;
			for (int i=0; i<4; i++) {
				solid = solid && is_solid(this->spans[i]);
				if (!solid)
					break;
			}
#endif
			// we end up recomputing the solidness of spans when ever not all of the spans change
			// we could move this calculation into add_color to avoid that
			bool solid = is_solid(s1) && is_solid(s2) && is_solid(s3) && is_solid(s4);
#ifdef MULTI
			bool coherent = is_coherent(s1, s2, s3, s4);
#else
			bool coherent = is_coherent(s1, s2) && is_coherent(s1, s3) && is_coherent(s1, s4);
#endif
			// check to see if all of the shapes are the same

			if (solid) {
				Intermediate c;
				if (coherent && s1->shape_count == 1) {
					c = s1->shapes[0]->color;
					c.accumulate(c);
					c.accumulate(c);
				} else {
					c = s1->compute_color(x, cur_y-3);
					c.accumulate(s2->compute_color(x, cur_y-2));
					c.accumulate(s3->compute_color(x, cur_y-1));
					c.accumulate(s4->compute_color(x, cur_y));
				}
				while (w && output.count) {
					output.queue(c);
					w--;
				}
				if (w >=4 ) {
					uint32_t value;
					Intermediate full = c;
					full.accumulate(c);
					full.accumulate(c);
					full.accumulate(c);
					value = full.finalize();

					while (w >= 8) {
						*output.buf++ = value;
						*output.buf++ = value;
						w-=8;
					}
					if (w >= 4) {
						*output.buf++ = value;
						w-=4;
					}
				}

				while (w) {
					output.queue(c);
					w--;
				}
			} else {
				while (w && output.count) {
					c = s1->compute_color(x, cur_y-3);
					c.accumulate(s2->compute_color(x, cur_y-2));
					c.accumulate(s3->compute_color(x, cur_y-1));
					c.accumulate(s4->compute_color(x, cur_y));
					output.queue(c);
					w--;
					x++;
				}
				if (coherent && w >=4) {
					//XXX we can drop this memset if we always have a solid color fill under everything
					memset(output.buf, 0, w);
					for (int i=0; i<s1->shape_count; i++) {
						s1->shapes[i]->fill(s1->shapes[i], output.buf, x>>SAMPLE_SHIFT, cur_y>>SAMPLE_SHIFT, w);
					}
					output.buf += (w>>2);
					x+=w;
					w = w&3;
				}
				while (w) {
					c = s1->compute_color(x, cur_y-3);
					c.accumulate(s2->compute_color(x, cur_y-2));
					c.accumulate(s3->compute_color(x, cur_y-1));
					c.accumulate(s4->compute_color(x, cur_y));

					output.queue(c);
					w--;
					x++;
				}

			}
		} else {
			while (w) {
				c = s1->compute_color(x, cur_y-3);
				c.accumulate(s2->compute_color(x, cur_y-2));
				c.accumulate(s3->compute_color(x, cur_y-1));
				c.accumulate(s4->compute_color(x, cur_y));
				output.queue(c);
				w--;
				x++;
			}
		}


		// we could compute and subtract here or we need some different solution
		if (s1->x_end == min)
			s1 = s1->next;
		if (s2->x_end == min)
			s2 = s2->next;
		if (s3->x_end == min)
			s3 = s3->next;
		if (s4->x_end == min)
			s4 = s4->next;
		start_x = min;
		//assert(s1 && s2 && s3 && s4);
	}
}

// one thing to consider is whether
// we should share value computation
// for particular coverage and how easy it is to do this.
// compute coverage
void
Rasterizer::rasterize() {
	for (cur_y = 0; cur_y < height; ) {
		// we do 4x4 super-sampling so we need
		// to scan 4 times before painting a line of pixels
		for (int i=0; i<4; i++) {
			// insert the new edges into the sorted list
			insert_starting_edges();
			// scan over the edge list producing a list of spans
			scan_edges();
			// step all of the edges to the next scanline
			// dropping the ones that end
			step_edges();
			// sort the remaning edges
			sort_edges();
			cur_y++;
		}
		// adjust cur_y so that cur_y/4 is the correct line of pixels for paint_spans
		cur_y--;
		paint_spans();
		span_arena.reset();
		cur_y++;
	}
	edge_arena.reset();
	// printf("comparisons: %d\n", comparisons);
}

void solid_fill(Shape *s, uint32_t *buf, int x, int y, int w)
{
	uint32_t c = s->color.finalize_unaccumulated();
	while (w >= 4) {
		*buf++ = c;
		w-=4;
	}
}

void gradient_fill(Shape *s, uint32_t *buf, int x, int y, int w)
{
	if (x > 200)
		x = 200;
	while (w >= 4) {
		uint32_t c = 0xff000000 | (x)<<16 | (255-x)<<8;
		uint32_t a = c >> 24;
		*buf++ = c;//0x99009900;//c;
		w-=4;
		x++;
		if (x > 200)
			x = 200;
	}
}

Intermediate gradient_eval(Shape *s, int x, int y)
{
	if (x > 200)
		x = 200;
	Intermediate ret;
	ret.ag = 0xff0000 | (255-x);
	ret.rb = x << 16;
	return ret;
}

