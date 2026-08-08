#ifndef HP_ORAM_MODEL_H
#define HP_ORAM_MODEL_H

#include "ThD_test.h"

#define HP_ORAM_VERTEX_COUNT    (305u)
#define HP_ORAM_TRI_COUNT       (634u)

extern const Thd_point_t hp_oram_vertices[HP_ORAM_VERTEX_COUNT];
extern const tri_t hp_oram_tris[HP_ORAM_TRI_COUNT];
extern const int16_t hp_oram_face_normals_q14[HP_ORAM_TRI_COUNT][3];
extern const int16_t hp_oram_vertex_normals_q14[HP_ORAM_VERTEX_COUNT][3];

#endif
