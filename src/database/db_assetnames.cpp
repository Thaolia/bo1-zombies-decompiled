#include "db_assetnames.h"
#include "db_registry.h"

#include <gfx_d3d/r_material.h>

#include <xanim/xmodel.h>
#include <universal/q_shared.h>

#include <physics/physpreset_load_obj.h>
#include <physics/physconstraints_load_obj.h>
#include <glass/glass.h>
#include "db_load.h"
#include <bgame/bg_weapons.h>
#include <bgame/bg_local.h>
#include <gfx_d3d/r_bsp_load_obj.h>
#include <bgame/bg_emblems.h>
#include <qcommon/com_bsp.h>
#include <gfx_d3d/r_extracam.h>
#include <platform/arch_size.h>
#include <stringed/stringed_hooks.h>
#include <sound/snd_bank.h>
#include <game/g_bsp.h>
#include <qcommon/cm_load_obj.h>
#include <cgame/cg_effects_load_obj.h>

const char *(__cdecl *DB_XAssetGetNameHandler[43])(const XAssetHeader *) =
{
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_ImageGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  NULL,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_LocalizeEntryGetName,
  &DB_DDLGetName,
  NULL,
  NULL,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_DDLGetName,
  &DB_GetEmblemSetName
};

const char *g_assetNames[43] =
{
  "xmodelpieces",
  "physpreset",
  "physconstraints",
  "destructibledef",
  "xanim",
  "xmodel",
  "material",
  "techset",
  "image",
  "sound",
  "sound_patch",
  "col_map_sp",
  "col_map_mp",
  "com_map",
  "game_map_sp",
  "game_map_mp",
  "map_ents",
  "gfx_map",
  "lightdef",
  "ui_map",
  "font",
  "menufile",
  "menu",
  "localize",
  "weapon",
  "weapondef",
  "weaponvariant",
  "snddriverglobals",
  "fx",
  "impactfx",
  "aitype",
  "mptype",
  "mpbody",
  "mphead",
  "character",
  "xmodelalias",
  "rawfile",
  "stringtable",
  "packindex",
  "xGlobals",
  "ddl",
  "glasses",
  "emblemset"
};

void(__cdecl *DB_XAssetSetNameHandler[43])(XAssetHeader *, const char *) =
{
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  (void(*)(XAssetHeader*, const char*))&DB_ImageSetName,
  &DB_DDLSetname,
  NULL,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  NULL,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_LocalizeEntrySetName,
  &DB_DDLSetname,
  NULL,
  NULL,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  &DB_DDLSetname,
  NULL,
  NULL
};

static_assert(sizeof(RawFile) == BO1_ARCH_SIZE(12, 24));
int __cdecl DB_SizeofXAsset_RawFile_()
{
    return sizeof(RawFile);
}

static_assert(sizeof(PhysPreset) == BO1_ARCH_SIZE(84, 96));
int __cdecl DB_SizeofXAsset_PhysPreset_()
{
    return sizeof(PhysPreset);
}

static_assert(sizeof(PhysConstraints) == BO1_ARCH_SIZE(2696, 3216));
int __cdecl DB_SizeofXAsset_PhysConstraints_()
{
    return sizeof(PhysConstraints);
}

static_assert(sizeof(DestructibleDef) == BO1_ARCH_SIZE(24, 48));
//int __cdecl SV_GetMaxAttachCount()
int __cdecl DB_SizeofXAsset_DestructibleDef_()
{
    return sizeof(DestructibleDef);
}

static_assert(sizeof(Font_s) == BO1_ARCH_SIZE(24, 40));
int __cdecl DB_SizeofXAsset_Font_s_()
{
    return sizeof(Font_s);
}

static_assert(sizeof(XAnimParts) == BO1_ARCH_SIZE(104, 152));
//int __cdecl PM_MediumLandingForSurface()
int __cdecl DB_SizeofXAsset_XAnimParts_()
{
    return sizeof(XAnimParts);
}

static_assert(sizeof(XModel) == BO1_ARCH_SIZE(252, 328));
int __cdecl DB_SizeofXAsset_XModel_()
{
    return sizeof(XModel);
}

// (for some reason the intellisense size is wrong unless it's right next to the struct definition)
int __cdecl DB_SizeofXAsset_Material_()
{
    return sizeof(Material);
}

static_assert(sizeof(MaterialTechniqueSet) == BO1_ARCH_SIZE(528, 1056));
int __cdecl DB_SizeofXAsset_MaterialTechniqueSet_()
{
    return sizeof(MaterialTechniqueSet);
}

static_assert(sizeof(SndDriverGlobals) == BO1_ARCH_SIZE(52, 104));
int __cdecl DB_SizeofXAsset_SndDriverGlobals_()
{
    return sizeof(SndDriverGlobals);
}

static_assert(sizeof(XGlobals) == BO1_ARCH_SIZE(40, 48));
int __cdecl DB_SizeofXAsset_XGlobals_()
{
    return sizeof(XGlobals);
}

static_assert(sizeof(StringTable) == BO1_ARCH_SIZE(20, 32));
int __cdecl DB_SizeofXAsset_StringTable_()
{
    return sizeof(StringTable);
}

static_assert(sizeof(clipMap_t) == BO1_ARCH_SIZE(332, 568));
int __cdecl DB_SizeofXAsset_clipMap_t_()
{
    return sizeof(clipMap_t);
}

static_assert(sizeof(ComWorld) == BO1_ARCH_SIZE(64, 88));
int __cdecl DB_SizeofXAsset_ComWorld_()
{
    return sizeof(ComWorld);
}

static_assert(sizeof(EmblemSet) == BO1_ARCH_SIZE(44, 80));
int __cdecl DB_SizeofXAsset_EmblemSet_()
{
    return sizeof(EmblemSet);
}

static_assert(sizeof(GfxWorld) == BO1_ARCH_SIZE(1084, 1672));
int __cdecl DB_SizeofXAsset_GfxWorld_()
{
    return sizeof(GfxWorld);
}

static_assert(sizeof(GfxLightDef) == BO1_ARCH_SIZE(16, 32));
int __cdecl DB_SizeofXAsset_GfxLightDef_()
{
    return sizeof(GfxLightDef);
}

static_assert(sizeof(menuDef_t) == BO1_ARCH_SIZE(400, 456));
int __cdecl DB_SizeofXAsset_menuDef_t_()
{
    return sizeof(menuDef_t);
}

static_assert(sizeof(XAnimTree_s) == BO1_ARCH_SIZE(8, 16));
//int __cdecl XAnimTreeSize()
//{
//    return 8;
//}

static_assert(sizeof(WeaponVariantDef) == BO1_ARCH_SIZE(228, 288));
int __cdecl DB_SizeofXAsset_WeaponVariantDef_()
{
    return sizeof(WeaponVariantDef);
}

static_assert(sizeof(FxEffectDef) == BO1_ARCH_SIZE(60, 72));
int __cdecl DB_SizeofXAsset_FxEffectDef_()
{
    return sizeof(FxEffectDef);
}

static_assert(sizeof(PackIndex) == BO1_ARCH_SIZE(28, 40));
int __cdecl DB_SizeofXAsset_PackIndex_()
{
    return sizeof(PackIndex);
}

static_assert(sizeof(Glasses) == BO1_ARCH_SIZE(56, 72));
int __cdecl DB_SizeofXAsset_Glasses_()
{
    return sizeof(Glasses);
}

// The exe folds identical functions, so its table shares one size function between types of the same 32-bit size
// (GfxImage used SndDriverGlobals', SndBank XGlobals'...). Their 64-bit sizes differ: each type has its own here.
template <typename T>
static int __cdecl DB_SizeofXAsset()
{
    return sizeof(T);
}

static_assert(sizeof(XModelPieces) == BO1_ARCH_SIZE(12, 24));
static_assert(sizeof(GfxImage) == BO1_ARCH_SIZE(52, 72));
static_assert(sizeof(SndBank) == BO1_ARCH_SIZE(40, 72));
static_assert(sizeof(SndPatch) == BO1_ARCH_SIZE(20, 40));
static_assert(sizeof(GameWorldSp) == BO1_ARCH_SIZE(44, 88));
static_assert(sizeof(GameWorldMp) == BO1_ARCH_SIZE(44, 88));
static_assert(sizeof(MapEnts) == BO1_ARCH_SIZE(12, 24));
static_assert(sizeof(MenuList) == BO1_ARCH_SIZE(12, 24));
static_assert(sizeof(LocalizeEntry) == BO1_ARCH_SIZE(8, 16));
static_assert(sizeof(FxImpactTable) == BO1_ARCH_SIZE(8, 16));
static_assert(sizeof(ddlRoot_t) == BO1_ARCH_SIZE(8, 16));

int(__cdecl *DB_GetXAssetSizeHandler[43])() =
{
  &DB_SizeofXAsset<XModelPieces>,
  &DB_SizeofXAsset_PhysPreset_,
  &DB_SizeofXAsset_PhysConstraints_,
  &DB_SizeofXAsset_DestructibleDef_,
  //&PM_MediumLandingForSurface,
  DB_SizeofXAsset_XAnimParts_,
  &DB_SizeofXAsset_XModel_,
  &DB_SizeofXAsset_Material_,
  &DB_SizeofXAsset_MaterialTechniqueSet_,
  &DB_SizeofXAsset<GfxImage>,
  &DB_SizeofXAsset<SndBank>,
  &DB_SizeofXAsset<SndPatch>,
  &DB_SizeofXAsset_clipMap_t_,
  &DB_SizeofXAsset_clipMap_t_,
  &DB_SizeofXAsset_ComWorld_,
  &DB_SizeofXAsset<GameWorldSp>,
  &DB_SizeofXAsset<GameWorldMp>,
  &DB_SizeofXAsset<MapEnts>,
  &DB_SizeofXAsset_GfxWorld_,
  &DB_SizeofXAsset_GfxLightDef_,
  NULL,
  &DB_SizeofXAsset_Font_s_,
  &DB_SizeofXAsset<MenuList>,
  &DB_SizeofXAsset_menuDef_t_,
  &DB_SizeofXAsset<LocalizeEntry>,
  &DB_SizeofXAsset_WeaponVariantDef_,
  NULL,
  NULL,
  &DB_SizeofXAsset_SndDriverGlobals_,
  &DB_SizeofXAsset_FxEffectDef_,
  &DB_SizeofXAsset<FxImpactTable>,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  &DB_SizeofXAsset_RawFile_,
  &DB_SizeofXAsset_StringTable_,
  &DB_SizeofXAsset_PackIndex_,
  &DB_SizeofXAsset_XGlobals_,
  &DB_SizeofXAsset<ddlRoot_t>,
  &DB_SizeofXAsset_Glasses_,
  &DB_SizeofXAsset_EmblemSet_
};





const char *__cdecl DB_ImageGetName(const XAssetHeader *header)
{
    return header->image->name;
}

void __cdecl DB_ImageSetName(XAssetHeader *header, const char *name)
{
    //header->xmodelPieces[3].pieces = name;
    header->image->name = name;
}

const char *__cdecl DB_LocalizeEntryGetName(const XAssetHeader *header)
{
    return header->localize->name;
}

void __cdecl DB_LocalizeEntrySetName(XAssetHeader *header, const char *name)
{
    header->localize->name = name;
}

void __cdecl DB_DDLSetname(XAssetHeader *header, const char *name)
{
    header->xmodelPieces->name = name;
}

const char *__cdecl DB_DDLGetName(const XAssetHeader *header)
{
    return header->xmodelPieces->name;
}

const char *__cdecl DB_GetEmblemSetName(const XAssetHeader *header)
{
    return "emblemset";
}

const char *__cdecl DB_GetXAssetHeaderName(int type, const XAssetHeader *header)
{
    const char *v2; // eax
    const char *name; // [esp+0h] [ebp-4h]

    if ( !header
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp", 687, 0, "%s", "header") )
    {
        __debugbreak();
    }
    if ( !DB_XAssetGetNameHandler[type]
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp",
                    688,
                    0,
                    "%s",
                    "DB_XAssetGetNameHandler[type]") )
    {
        __debugbreak();
    }
    if ( !header->xmodelPieces
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp", 689, 0, "%s", "header->data") )
    {
        __debugbreak();
    }
    name = DB_XAssetGetNameHandler[type](header);
    if ( !name )
    {
        v2 = va("Name \"%s\" not found for asset type %s\n", 0, g_assetNames[type]);
        if ( !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp",
                        691,
                        0,
                        "%s\n\t%s",
                        "name",
                        v2) )
            __debugbreak();
    }
    return name;
}

const char *__cdecl DB_GetXAssetName(const XAsset *asset)
{
    if ( !asset
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp", 698, 0, "%s", "asset") )
    {
        __debugbreak();
    }
    return DB_GetXAssetHeaderName(asset->type, &asset->header);
}

void __cdecl DB_SetXAssetName(XAsset *asset, const char *name)
{
    if ( !DB_XAssetSetNameHandler[asset->type]
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp",
                    705,
                    0,
                    "%s",
                    "DB_XAssetSetNameHandler[asset->type]") )
    {
        __debugbreak();
    }
    DB_XAssetSetNameHandler[asset->type](&asset->header, name);
}

int __cdecl DB_GetXAssetTypeSize(int type)
{
    if ( !DB_GetXAssetSizeHandler[type]
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp",
                    712,
                    0,
                    "%s",
                    "DB_GetXAssetSizeHandler[type]") )
    {
        __debugbreak();
    }
    return DB_GetXAssetSizeHandler[type]();
}

const char *__cdecl DB_GetXAssetTypeName(unsigned int type)
{
    if ( type > 0x2A
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_assetnames.cpp",
                    718,
                    0,
                    "%s",
                    "type >= 0 && type < ASSET_TYPE_COUNT") )
    {
        __debugbreak();
    }
    return g_assetNames[type];
}

