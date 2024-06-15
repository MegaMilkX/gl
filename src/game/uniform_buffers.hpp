#pragma once

#include <unordered_map>
#include <string>
#include "math/gfxm.hpp"
#include "platform/win32/gl/glextutil.h"

namespace UBFMT {

template<int SIZE, typename TYPE>
struct ELEM_TYPE {
    static const int size = SIZE;
    using type = TYPE;
};

#define DEF_ELEM_TYPE(SIZE, TYPE, NAME) \
	typedef ELEM_TYPE<SIZE, TYPE> NAME;

DEF_ELEM_TYPE( 4,	bool,					BOOL);		// TODO: Consider using other type than bool
DEF_ELEM_TYPE( 4,	int32_t,				INT);
DEF_ELEM_TYPE( 4,	uint32_t,				UINT);
DEF_ELEM_TYPE( 4,	float,					FLOAT);
DEF_ELEM_TYPE( 8,	double,					DOUBLE);
DEF_ELEM_TYPE( 8,	gfxm::tvec2<bool>,		BVEC2);
DEF_ELEM_TYPE(12,	gfxm::tvec3<bool>,		BVEC3);
DEF_ELEM_TYPE(16,	gfxm::tvec4<bool>,		BVEC4);
DEF_ELEM_TYPE( 8,	gfxm::ivec2,			IVEC2);
DEF_ELEM_TYPE(12,	gfxm::ivec3,			IVEC3);
DEF_ELEM_TYPE(16,	gfxm::ivec4,			IVEC4);
DEF_ELEM_TYPE( 8,	gfxm::tvec2<uint32_t>,	UVEC2);
DEF_ELEM_TYPE(12,	gfxm::tvec3<uint32_t>,	UVEC3);
DEF_ELEM_TYPE(16,	gfxm::tvec4<uint32_t>,	UVEC4);
DEF_ELEM_TYPE( 8,	gfxm::vec2,				VEC2);
DEF_ELEM_TYPE(12,	gfxm::vec3,				VEC3);
DEF_ELEM_TYPE(16,	gfxm::vec4,				VEC4);
DEF_ELEM_TYPE(16,	gfxm::dvec2,			DVEC2);
DEF_ELEM_TYPE(24,	gfxm::dvec3,			DVEC3);
DEF_ELEM_TYPE(32,	gfxm::dvec4,			DVEC4);
//DEF_ELEM_TYPE(16,	gfxm::mat2,				MAT2); // No gfxm::mat2 defined. If u need it - implement it first
//DEF_ELEM_TYPE(24, gfxm::mat2x3,           MAT2X3);
//DEF_ELEM_TYPE(32, gfxm::mat2x4,           MAT2X4);
DEF_ELEM_TYPE(36,	gfxm::mat3,				MAT3);
//DEF_ELEM_TYPE(24,	gfxm::mat3x2,			MAT3X2);
//DEF_ELEM_TYPE(48,	gfxm::mat3x4,			MAT3X4);
DEF_ELEM_TYPE(64,	gfxm::mat4,				MAT4);
//DEF_ELEM_TYPE(32,	gfxm::mat4x2,			MAT4X2);
//DEF_ELEM_TYPE(48,	gfxm::mat4x3,			MAT4X3);

#undef DEF_ELEM_TYPE


#define COMMON_FIELDS \
	UNIFORM_FIELD(MAT4, matProjection) \
	UNIFORM_FIELD(MAT4, matView) \
	UNIFORM_FIELD(VEC2, viewportSize)
#define MODEL_FIELDS \
	UNIFORM_FIELD(MAT4, matModel)

#define UNIFORM_BUFFERS \
	UNIFORM_STRUCT(Common, COMMON_FIELDS) \
	UNIFORM_STRUCT(Model, MODEL_FIELDS)

#define UNIFORM_STRUCT(NAME, FIELDS) struct NAME { FIELDS };
#define UNIFORM_FIELD(TYPE, NAME) TYPE::type NAME;
UNIFORM_BUFFERS
#undef UNIFORM_FIELD
#undef UNIFORM_STRUCT


#undef UNIFORM_BUFFERS

}
