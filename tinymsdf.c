#include "tinymsdf.h"

#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
	float x, y;
} vec2;

vec2 vec2_add(vec2 a, vec2 b) {
	return (vec2){a.x + b.x, a.y + b.y};
}

vec2 vec2_mul(float a, vec2 b) {
	return (vec2) {a * b.x, a * b.y};
}

int vec2_equal(vec2 a, vec2 b) {
	return a.x == b.x && a.y == b.y;
}

typedef struct {
	double distance, dot;
} signed_distance_t;

typedef enum {
	EDGE_COLOR_BLACK = 0,
	EDGE_COLOR_RED = 1,
	EDGE_COLOR_GREEN = 2,
	EDGE_COLOR_YELLOW = 3,
	EDGE_COLOR_BLUE = 4,
	EDGE_COLOR_MAGENTA = 5,
	EDGE_COLOR_CYAN = 6,
	EDGE_COLOR_WHITE = 7
} edge_color_t;

typedef enum {
	EDGE_TYPE_LINEAR = 0,
	EDGE_TYPE_QUADRATIC = 1,
	EDGE_TYPE_CUBIC = 2
} edge_type_t;

typedef struct {
	edge_color_t color;
	edge_type_t type;
	vec2 p[4];
} edge_segment_t;

edge_segment_t edge_segment_linear(vec2 p0, vec2 p1, edge_color_t color /*= EDGE_COLOR_WHITE*/) {
	edge_segment_t edge_segment = {color, EDGE_TYPE_LINEAR};
	edge_segment.p[0] = p0;
	edge_segment.p[1] = p1;
	return edge_segment;
}

edge_segment_t edge_segment_quadradic(vec2 p0, vec2 p1, vec2 p2, edge_color_t color /*= EDGE_COLOR_WHITE*/) {
	if (vec2_equal(p1, p0) || vec2_equal(p1, p2))
		p1 = vec2_mul(0.5f, vec2_add(p0, p2));
	
	edge_segment_t edge_segment = {color, EDGE_TYPE_QUADRATIC};
	edge_segment.p[0] = p0;
	edge_segment.p[1] = p1;
	edge_segment.p[2] = p2;
	return edge_segment;
}

edge_segment_t edge_segment_cubic(vec2 p0, vec2 p1, vec2 p2, vec2 p3, edge_color_t color /*= EDGE_COLOR_WHITE*/) {
	edge_segment_t edge_segment = {color, EDGE_TYPE_CUBIC};
	edge_segment.p[0] = p0;
	edge_segment.p[1] = p1;
	edge_segment.p[2] = p2;
	edge_segment.p[3] = p3;
	return edge_segment;
}

typedef struct {
	edge_segment_t *edges;
	int edges_count;
} contour_t;

typedef struct {
	contour_t *contours;
	int contour_count;
} shape_t;

typedef struct {
	vec2 position;
	shape_t *shape;
	contour_t *contour;
} context_t;

#define F26DOT6_TO_FLOAT(x) (1/64.0f*(float)x)

inline vec2 ftPoint2(const FT_Vector *vector) {
	return (vec2){F26DOT6_TO_FLOAT(vector->x), F26DOT6_TO_FLOAT(vector->y)};
}

int tinymsdf_move_to(const FT_Vector *to, void *user)
{
	context_t *context = user;
	if (!(context->contour != NULL && context->contour->edges_count == 0)) {
		int index = context->shape->contour_count;
		context->shape->contours = realloc(context->shape->contours, sizeof(context_t) * ++context->shape->contour_count);
		context->shape->contours[index] = (contour_t){NULL, 0};
		context->contour = &context->shape->contours[index];
	}
	context->position = ftPoint2(to);
	return 0;
}

int tinymsdf_line_to(const FT_Vector *to, void *user)
{
	context_t *context = user;
	vec2 endpoint = ftPoint2(to);
	if (!vec2_equal(endpoint, context->position)) {
		int index = context->contour->edges_count;
		context->contour->edges = realloc(context->contour->edges, sizeof(edge_segment_t) * context->shape->contour_count);
		context->contour->edges[index] = edge_segment_linear(context->position, endpoint, EDGE_COLOR_WHITE);
		context->position = endpoint;
	}
	return 0;
}

int tinymsdf_conic_to(const FT_Vector *control, const FT_Vector *to, void *user)
{
	context_t *context = user;
	int index = context->contour->edges_count;
	context->contour->edges = realloc(context->contour->edges, sizeof(edge_segment_t) * ++context->contour->edges_count);
	context->contour->edges[index] = edge_segment_quadradic(context->position, ftPoint2(control), ftPoint2(to), EDGE_COLOR_WHITE);
	context->position = ftPoint2(to);
	return 0;
}

int tinymsdf_cubic_to(const FT_Vector *control1, const FT_Vector *control2, const FT_Vector *to, void *user)
{
	context_t *context = user;
	int index = context->contour->edges_count;
	context->contour->edges = realloc(context->contour->edges, sizeof(edge_segment_t) * ++context->shape->contour_count);
	context->contour->edges[index] = edge_segment_cubic(context->position, ftPoint2(control1), ftPoint2(control2), ftPoint2(to), EDGE_COLOR_WHITE);
	context->position = ftPoint2(to);
	return 0;
}

tinymsdf_error_t tinymsdf_load_glyph(shape_t *shape, FT_Face face, unicode_t unicode)
{
	FT_Error error = FT_Load_Char(face, unicode, FT_LOAD_NO_SCALE);
	if (error)
		return TINYMSDF_GLYPH_LOAD;

	FT_Outline_Funcs outlineFuncs;
	outlineFuncs.move_to = &tinymsdf_move_to;
	outlineFuncs.line_to = &tinymsdf_line_to;
	outlineFuncs.conic_to = &tinymsdf_conic_to;
	outlineFuncs.cubic_to = &tinymsdf_cubic_to;
	outlineFuncs.shift = 0;
	outlineFuncs.delta = 0;

	context_t context;
	context.position = (vec2){0, 0};
	context.shape = shape;
	context.contour = NULL;
	
	error = FT_Outline_Decompose(&face->glyph->outline, &outlineFuncs, &context);
	if (error)
		return TINYMSDF_OUTLINE_DECOMPOSE;

	return TINYMSDF_SUCCESS;
}

tinymsdf_error_t tinymsdf_create_bitmap(float **pixels, int width, int height, int components) {
	*pixels = malloc(sizeof(float) * components * width * height);
	memset(*pixels, 0.0f, sizeof(float) * components * width * height);
	return TINYMSDF_SUCCESS;
}

void tinymsdf_print_shape(shape_t *shape) {
	printf("shape_t\n");
}

tinymsdf_error_t tinymsdf_generate_mtsdf(float *pixels, int width, int height, FT_Face face, unicode_t unicode)
{
	shape_t shape = {NULL, 0};
	tinymsdf_error_t error = tinymsdf_load_glyph(&shape, face, unicode);
	if (error)
		return error;
	tinymsdf_print_shape(&shape);

	error = tinymsdf_create_bitmap(&pixels, width, height, 4);
	if (error)
		return error;
	
	return TINYMSDF_SUCCESS;
}
