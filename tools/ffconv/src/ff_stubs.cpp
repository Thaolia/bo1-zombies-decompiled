// Engine functions the transformed db_load.cpp calls that ffconv must not run: asset registration, D3D resources,
// sound banks and mod hooks. The converted zone keeps the stream's own values; the 64-bit engine runs the real ones
// when it loads the IWffs zone. The Mark_* functions belong to unloading and are never reached by a load. Shader
// creation only hands the bytecode to the shader collection.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <database/db_registry.h>
#include <database/db_stringtable_load.h>
#include <gfx_d3d/r_buffers.h>
#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_water.h>
#include <universal/assertive.h>

#include "ff_shaders.h"

namespace
{
[[noreturn]] void Unreachable(const char *name)
{
    std::fprintf(stderr, "ffconv: %s was called during a load\n", name);
    std::exit(3);
}
} // namespace

// An engine assert during the walk means the stream does not match the code: stop with its message.
bool Assert_MyHandler(const char *filename, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "ffconv: engine assert %s(%d): ", filename, line);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    std::exit(3);
}

void *DB_ModLinkDeferredAlias(void *data)
{
    return data;
}

void __cdecl Load_PhysPresetAsset(XAssetHeader *)
{
}

void __cdecl Load_PhysConstraintsAsset(XAssetHeader *)
{
}

void __cdecl Load_DestructibleDefAsset(XAssetHeader *)
{
}

void __cdecl Load_XAnimPartsAsset(XAssetHeader *)
{
}

void __cdecl Load_XModelAsset(XAssetHeader *)
{
}

void __cdecl Load_MaterialAsset(Material **)
{
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *)
{
}

void __cdecl Load_GfxImageAsset(XAssetHeader *)
{
}

void __cdecl Load_SndBankAsset(XAssetHeader *)
{
}

void __cdecl Load_SndPatchAsset(XAssetHeader *)
{
}

void __cdecl Load_ClipMapAsset(XAssetHeader *)
{
}

void __cdecl Load_ComWorldAsset(XAssetHeader *)
{
}

void __cdecl Load_GameWorldSpAsset(XAssetHeader *)
{
}

void __cdecl Load_GameWorldMpAsset(XAssetHeader *)
{
}

void __cdecl Load_MapEntsAsset(XAssetHeader *)
{
}

void __cdecl Load_DDLAsset(XAssetHeader *)
{
}

void __cdecl Load_GfxWorldAsset(XAssetHeader *)
{
}

void __cdecl Load_LightDefAsset(XAssetHeader *)
{
}

void __cdecl Load_FontAsset(XAssetHeader *)
{
}

void __cdecl Load_MenuListAsset(XAssetHeader *)
{
}

void __cdecl Load_MenuAsset(XAssetHeader *)
{
}

void __cdecl Load_LocalizeEntryAsset(XAssetHeader *)
{
}

void __cdecl Load_WeaponVariantDefAsset(XAssetHeader *)
{
}

void __cdecl Load_SndDriverGlobalsAsset(SndDriverGlobals **)
{
}

void __cdecl Load_FxEffectDefAsset(XAssetHeader *)
{
}

void __cdecl Load_FxImpactTableAsset(XAssetHeader *)
{
}

void __cdecl Load_RawFileAsset(XAssetHeader *)
{
}

void __cdecl Load_StringTableAsset(XAssetHeader *)
{
}

void __cdecl Load_PackIndexAsset(XAssetHeader *)
{
}

void __cdecl Load_XGlobalsAsset(XAssetHeader *)
{
}

void __cdecl Load_GlassesAsset(XAssetHeader *)
{
}

void __cdecl Load_EmblemSetAsset(XAssetHeader *)
{
}

void __cdecl Load_FxEffectDefFromName(const char **)
{
}

void __cdecl Load_PicmipWater(water_t **)
{
}

void __cdecl Load_Texture(GfxTexture *, GfxImage *)
{
}

void __cdecl Load_VertexBuffer(IDirect3DVertexBuffer9 **, unsigned __int8 *, int)
{
}

void __cdecl Load_BuildVertexDecl(MaterialVertexDeclaration **)
{
}

void __cdecl Load_CreateMaterialPixelShader(GfxPixelShaderLoadDef *loadDef, MaterialPixelShader *)
{
    ff::CollectShader(true, reinterpret_cast<const uint32_t *>(loadDef->program), loadDef->programSize);
}

void __cdecl Load_CreateMaterialVertexShader(GfxVertexShaderLoadDef *loadDef, MaterialVertexShader *)
{
    ff::CollectShader(false, reinterpret_cast<const uint32_t *>(loadDef->program), loadDef->programSize);
}

void __cdecl Mark_PhysPresetAsset(PhysPreset *)
{
    Unreachable("Mark_PhysPresetAsset");
}

void __cdecl Mark_PhysConstraintsAsset(PhysConstraints *)
{
    Unreachable("Mark_PhysConstraintsAsset");
}

void __cdecl Mark_DestructibleDefAsset(DestructibleDef *)
{
    Unreachable("Mark_DestructibleDefAsset");
}

void __cdecl Mark_XAnimPartsAsset(XAnimParts *)
{
    Unreachable("Mark_XAnimPartsAsset");
}

void __cdecl Mark_XModelAsset(XModel *)
{
    Unreachable("Mark_XModelAsset");
}

void __cdecl Mark_MaterialAsset(Material *)
{
    Unreachable("Mark_MaterialAsset");
}

void __cdecl Mark_MaterialTechniqueSetAsset(MaterialTechniqueSet *)
{
    Unreachable("Mark_MaterialTechniqueSetAsset");
}

void __cdecl Mark_GfxImageAsset(GfxImage *)
{
    Unreachable("Mark_GfxImageAsset");
}

void __cdecl Mark_SndBankAsset(SndBank *)
{
    Unreachable("Mark_SndBankAsset");
}

void __cdecl Mark_SndPatchAsset(SndPatch *)
{
    Unreachable("Mark_SndPatchAsset");
}

void __cdecl Mark_ClipMapAsset(clipMap_t *)
{
    Unreachable("Mark_ClipMapAsset");
}

void __cdecl Mark_ComWorldAsset(ComWorld *)
{
    Unreachable("Mark_ComWorldAsset");
}

void __cdecl Mark_GameWorldSpAsset(GameWorldSp *)
{
    Unreachable("Mark_GameWorldSpAsset");
}

void __cdecl Mark_GameWorldMpAsset(GameWorldMp *)
{
    Unreachable("Mark_GameWorldMpAsset");
}

void __cdecl Mark_MapEntsAsset(MapEnts *)
{
    Unreachable("Mark_MapEntsAsset");
}

void __cdecl Mark_DDLAsset(ddlRoot_t *)
{
    Unreachable("Mark_DDLAsset");
}

void __cdecl Mark_GfxWorldAsset(GfxWorld *)
{
    Unreachable("Mark_GfxWorldAsset");
}

void __cdecl Mark_LightDefAsset(GfxLightDef *)
{
    Unreachable("Mark_LightDefAsset");
}

void __cdecl Mark_FontAsset(Font_s *)
{
    Unreachable("Mark_FontAsset");
}

void __cdecl Mark_MenuListAsset(MenuList *)
{
    Unreachable("Mark_MenuListAsset");
}

void __cdecl Mark_MenuAsset(menuDef_t *)
{
    Unreachable("Mark_MenuAsset");
}

void __cdecl Mark_LocalizeEntryAsset(LocalizeEntry *)
{
    Unreachable("Mark_LocalizeEntryAsset");
}

void __cdecl Mark_WeaponVariantDefAsset(WeaponVariantDef *)
{
    Unreachable("Mark_WeaponVariantDefAsset");
}

void __cdecl Mark_SndDriverGlobalsAsset(SndDriverGlobals *)
{
    Unreachable("Mark_SndDriverGlobalsAsset");
}

void __cdecl Mark_FxEffectDefAsset(FxEffectDef *)
{
    Unreachable("Mark_FxEffectDefAsset");
}

void __cdecl Mark_FxImpactTableAsset(FxImpactTable *)
{
    Unreachable("Mark_FxImpactTableAsset");
}

void __cdecl Mark_RawFileAsset(RawFile *)
{
    Unreachable("Mark_RawFileAsset");
}

void __cdecl Mark_StringTableAsset(StringTable *)
{
    Unreachable("Mark_StringTableAsset");
}

void __cdecl Mark_PackIndexAsset(PackIndex *)
{
    Unreachable("Mark_PackIndexAsset");
}

void __cdecl Mark_XGlobalsAsset(XGlobals *)
{
    Unreachable("Mark_XGlobalsAsset");
}

void __cdecl Mark_GlassesAsset(Glasses *)
{
    Unreachable("Mark_GlassesAsset");
}

void __cdecl Mark_EmblemSetAsset(EmblemSet *)
{
    Unreachable("Mark_EmblemSetAsset");
}

void __cdecl Mark_ScriptStringCustom(unsigned __int16 *)
{
    Unreachable("Mark_ScriptStringCustom");
}
