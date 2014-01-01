#include "types.h"
struct Shape;
Intermediate radial_gradient_eval(Shape *s, int x, int y);
Intermediate bitmap_linear_eval(Shape *s, int x, int y);
Intermediate bitmap_nearest_eval(Shape *s, int x, int y);
void generic_opaque_fill(Shape *s, uint32_t *buf, int x, int y, int w);
void generic_over_fill(Shape *s, uint32_t *buf, int x, int y, int w);
