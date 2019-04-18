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

// See also: http://www.flipcode.com/archives/Fast_Approximate_Distance_Functions.shtml
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

// this metric is taken from skia
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

#define SHIFT   2
#define SCALE   (1 << SHIFT)
#define MASK    (SCALE - 1)
#define SUPER_Mask      ((1 << SHIFT) - 1)
// An example number of edges is 7422 but
// can go as high as edge count: 374640
// with curve count: 67680
void
Rasterizer::add_edge(Point start, Point end, bool curve, Point control)
{
	//static int count;
	//printf("edge count: %d\n",++count);
	// order the points from top to bottom
	if (end.y < start.y) {
		swap(start, end);
	}

	// how do we deal with edges to the right and left of the canvas?
	ActiveEdge *e = new (this->edge_arena.alloc(sizeof(ActiveEdge))) ActiveEdge;
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

static inline int coverage_to_alpha(int aa)
{
    aa <<= 8 - 2*SHIFT;
    aa -= aa >> (8 - SHIFT - 1);
    return aa;
}


// Skia does stepping and scanning of edges in a single
// pass over the edge list.
void Rasterizer::scan_edges()
{
        ActiveEdge *edge = this->active_edges;
        int winding = 0;

        // handle edges that begin to the left of the bitmap
        while (edge && edge->fullx < 0) {
                winding++;
                edge = edge->next;
        }

        int prevx = 0;
        while (edge) {
                if ((edge->fullx>>16) >= width)
                        break;

                if (winding++ & 1) {
                        blit_span((prevx + (1<<15))>>16, (edge->fullx + (1<<15))>>16);
                }
                prevx = edge->fullx;
                edge = edge->next;
        }

        // we don't need to worry about any edges beyond width
}

void Rasterizer::blit_span(int x1, int x2)
{
        printf("%d %d\n", x1, x2);
        int max = (1 << (8 - SHIFT)) - (((cur_y & MASK) + 1) >> SHIFT);
        uint32_t *b = &buf[cur_y/4*this->width/4 + (x1 >> SHIFT)];

        int fb = x1 & SUPER_Mask;
        int fe = x2 & SUPER_Mask;
        int n = (x2 >> SHIFT) - (x1 >> SHIFT) - 1;

        // invert the alpha on the left side
        if (n < 0) {
                *b += coverage_to_alpha(fe - fb)*0x1010101;
        } else {
                fb = (1 << SHIFT) - fb;
                *b += coverage_to_alpha(fb) * 0x1010101;
                b++;
                while (n) {
                        *b += max*0x1010101;
                        b++;
                        n--;
                }
                *b += coverage_to_alpha(fe) * 0x1010101;
        }
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
	}
	edge_arena.reset();
	// printf("comparisons: %d\n", comparisons);
}
