#ifndef HP_ORAM1_MODEL_H
#define HP_ORAM1_MODEL_H

#include "ThD_test.h"

#define HP_ORAM1_VERTEX_COUNT    (116u)
#define HP_ORAM1_TRI_COUNT       (232u)

extern const Thd_point_t hp_oram1_vertices[HP_ORAM1_VERTEX_COUNT];
extern const tri_t hp_oram1_tris[HP_ORAM1_TRI_COUNT];
extern const int16_t hp_oram1_face_normals_q14[HP_ORAM1_TRI_COUNT][3];
extern const int16_t hp_oram1_vertex_normals_q14[HP_ORAM1_VERTEX_COUNT][3];

#endif
