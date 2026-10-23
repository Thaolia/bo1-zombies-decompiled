#pragma once
#include <xanim/xanim.h>
#include <xanim/dobj.h>
#include "r_buffers.h"

void __cdecl R_SkinXSurfaceSkinned(
                const XSurface *xsurf,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertex *skinVerticesOut);
void __cdecl R_SkinXSurfaceWeight(
                const XSurface *xsurf,
                const GfxPackedVertex *inVerts,
                const XSurfaceVertexInfo *vertexInfo,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertex *outVerts);
void __cdecl R_SkinXSurfaceWeight0(
                const GfxPackedVertex *vertsIn,
                const unsigned __int16 *vertexBlend,
                int vertCount,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertex *vertsOut);
void __cdecl MatrixTransformVertexAndBasis(
                const float *offset,
                float binormalSign,
                PackedUnitVec normal,
                PackedUnitVec tangent,
                const DObjSkelMat *mat,
                GfxPackedVertex *vert);
void __cdecl R_SkinXSurfaceWeight1(
                const GfxPackedVertex *vertsIn,
                const unsigned __int16 *vertexBlend,
                int vertCount,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertex *vertsOut);
void __cdecl R_SkinXSurfaceWeight2(
                const GfxPackedVertex *vertsIn,
                const unsigned __int16 *vertexBlend,
                int vertCount,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertex *vertsOut);
void __cdecl R_SkinXSurfaceWeight3(
                const GfxPackedVertex *vertsIn,
                const unsigned __int16 *vertexBlend,
                int vertCount,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertex *vertsOut);
void __cdecl R_SkinXSurfaceRigid(
                const XSurface *surf,
                int totalVertCount,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertex *vertices);
void    R_SkinXModelCmd(struct SkinXModelCmd *data);
void __cdecl R_MultiplySkelMat(const DObjSkelMat *mat0, const DObjSkelMat *mat1, DObjSkelMat *out);


// The SSE skinning path packs vertices with MMX (__m64), which only 32-bit x86 has; other targets skin in C.
#if defined(_M_IX86) || defined(__i386__)
#define R_SKIN_MMX 1
void __cdecl R_SkinXSurfaceSkinnedSse(
                const XSurface *xsurf,
                const DObjSkelMat *boneMatrix,
                GfxPackedVertexNormal *skinVertNormalIn,
                GfxPackedVertexNormal *skinVertNormalOut,
                GfxPackedVertex *skinVerticesOut);
#else
#define R_SKIN_MMX 0
#endif


//__m128 * _mm_cvtpu8_ps@<eax>(int a1@<ebp>);
//__m128 *_mm_cvtpu16_ps@<eax>(int a1@<ebp>);
