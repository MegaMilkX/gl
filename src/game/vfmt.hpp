#pragma once

#include "platform/win32/gl/glextutil.h"

namespace VFMT {

typedef uint32_t ATTRIBUTE_UID;

template<int SIZE, GLenum GLTYPE>
struct ELEM_TYPE {
    static const int size = SIZE;
    static const GLenum gl_type = GLTYPE;
};

typedef ELEM_TYPE<1, GL_BYTE>           BYTE;
typedef ELEM_TYPE<1, GL_UNSIGNED_BYTE>  UBYTE;
typedef ELEM_TYPE<2, GL_SHORT>          SHORT;
typedef ELEM_TYPE<2, GL_UNSIGNED_SHORT> USHORT;
typedef ELEM_TYPE<2, GL_HALF_FLOAT>     HALF_FLOAT;
typedef ELEM_TYPE<4, GL_FLOAT>          FLOAT;
typedef ELEM_TYPE<4, GL_INT>            INT;
typedef ELEM_TYPE<4, GL_UNSIGNED_INT>   UINT;
typedef ELEM_TYPE<8, GL_DOUBLE>         DOUBLE;

struct AttribDesc {
    ATTRIBUTE_UID uid;
    GLenum gl_type;
    int elem_size;
    int count;
    bool normalized;
    const char* name;
    const char* input_name;
};

#define VFMT_ATTRIB_TABLE \
    DEF_ATTRIB(FLOAT, 3, false, Position) \
    DEF_ATTRIB(FLOAT, 2, false, UV) \
    DEF_ATTRIB(FLOAT, 2, false, UVLightmap) \
    DEF_ATTRIB(FLOAT, 3, false, Normal) \
    DEF_ATTRIB(FLOAT, 3, false, Tangent) \
    DEF_ATTRIB(FLOAT, 3, false, Bitangent) \
    DEF_ATTRIB(FLOAT, 4, false, BoneIndex4) \
    DEF_ATTRIB(FLOAT, 4, false, BoneWeight4) \
    DEF_ATTRIB(UBYTE, 4, true,  ColorRGBA) \
    DEF_ATTRIB(UBYTE, 3, true,  ColorRGB) \
    DEF_ATTRIB(FLOAT, 3, false, Velocity) \
    DEF_ATTRIB(FLOAT, 1, false, TextUVLookup) \
    DEF_ATTRIB(FLOAT, 4, false, ParticlePosition) \
    DEF_ATTRIB(FLOAT, 4, false, ParticleData) \
    DEF_ATTRIB(FLOAT, 4, false, ParticleScale) \
    DEF_ATTRIB(FLOAT, 4, false, ParticleColorRGBA) \
    DEF_ATTRIB(FLOAT, 4, false, ParticleSpriteData) \
    DEF_ATTRIB(FLOAT, 4, false, ParticleSpriteUV) \
    DEF_ATTRIB(FLOAT, 4, false, ParticleRotation) \
    DEF_ATTRIB(FLOAT, 4, false, TrailInstanceData0) \

#define DEF_ATTRIB(ELEM_TYPE, ELEM_COUNT, NORMALIZED, NAME) NAME,
enum ATTRIBUTE {
    VFMT_ATTRIB_TABLE
    NUM_ATTRIBS
};
#undef DEF_ATTRIB

#define DEF_ATTRIB(ELEM_TYPE, ELEM_COUNT, NORMALIZED, NAME) AttribDesc{ ATTRIBUTE::NAME, ELEM_TYPE::gl_type, ELEM_TYPE::size, ELEM_COUNT, NORMALIZED, #NAME, "in" #NAME },
constexpr AttribDesc attrib_desc_table[] = {
    VFMT_ATTRIB_TABLE
};
#undef DEF_ATTRIB

inline const AttribDesc* getAttribDesc(int i) {
    return &attrib_desc_table[i];
}

inline const AttribDesc* getAttribDescByInputName(const char* inputName) {
    for (int i = 0; i < sizeof(attrib_desc_table) / sizeof(attrib_desc_table[0]); ++i) {
        const auto& dsc = attrib_desc_table[i];
        if (dsc.input_name) {
            return &dsc;
        }
    }
    return 0;
}

}
