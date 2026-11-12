# OpenAssetTools addition: export a T6 (Black Ops II) world

These files are an addition to [OpenAssetTools](https://github.com/Laupetin/OpenAssetTools) (OAT) v0.34.0, which reads
Black Ops II fastfiles but has no dumper for the world geometry. They are compiled into OAT, so they are under OAT's
licence, the **GNU GPL v3**, not part of this repository's engine code. See `docs/BO2_MAPS.md` for what they are for.

- `src/ObjWriting/Game/T6/GfxWorld/GfxWorldObjDumperT6.{h,cpp}`: dumps a `gfxworld` asset as
  `<map>.d3dbsp.obj` + `.mtl` (every static surface, UVs, one material per world material with its colour map) and
  `<map>.d3dbsp.smodels.csv` (each placed static model: name, origin, three axes, scale), plus
  `<map>.d3dbsp.smodellight.bin` (`BO2SMLT1`: each placement's baked vertex colours per LOD, and each model's LOD
  vertex positions in the same order, which `tools/bo2/bo2_mesh.py` matches to the OBJ export's vertices), and
  `<map>.d3dbsp.worldlight.bin` / `.txt` (`BO2WLT1`: per OBJ vertex, its lightmap coordinate, colour, packed vectors,
  its surface's lightmap and primary light; the lightmap images and the sun), which `tools/bo2/bo2_worldlight.py`
  turns into the world's vertex colours.
- `src/ObjWriting/Game/T6/ClipMap/ClipMapCollisionDumperT6.{h,cpp}`: dumps the `clipmap` asset (OAT's
  `ASSET_TYPE_CLIPMAP_PVS`; the type OAT names `clipmap_unused` is not the one in the zones) as
  `<map>.d3dbsp.collision.bin`, format `BO2COL2` (materials, brushes with their non-axial planes and their owner,
  terrain triangles with their collision material and their owner, submodel bounds; the byte layout is at the top of
  the `.cpp`; "owner" is 0 for the world, n for submodel n, whose geometry is in that model's own space) and
  `<map>.d3dbsp.collision.txt` (counts and the material list), plus `<map>.d3dbsp.smodelcoll.csv` (the static models
  that collide, from the clipMap's own list: model, contents, origin, inverse scaled axes), which
  `tools/bo2/bo2_props_collision.py` turns into prop brushes.
- `src/ObjWriting/Game/T6/GameWorld/GameWorldPathDumperT6.{h,cpp}`: dumps the `gameworldmp` asset's path data as
  `<map>.d3dbsp.paths.csv`, one row per node (index, node type, origin, its links `to:dist:negotiation`), the graph
  BO2's map compiler made; `tools/bo2/bo2_layout.py` turns its links between plain pathnodes into the layout's
  `links` file.
- `src/ObjWriting/Game/T6/ComWorld/ComWorldLightsDumperT6.{h,cpp}`: dumps the `comworld` asset's primary lights as
  `<map>.d3dbsp.lights.csv` (type, colour, direction, origin, radius, cone, attenuation...), the lights a world
  surface's `primaryLightIndex` names; `tools/bo2/bo2_worldlight.py` bakes the lamps from it.
- `ObjWriterT6.patch`: registers the four dumpers in `src/ObjWriting/Game/T6/ObjWriterT6.cpp`.
- `ImageLoaderExternalT5.patch`: makes OAT's Linker write a BO1 (T5) image read from `images/<name>.iwi` the way BO1
  loads one: category `IMG_CATEGORY_LOAD_FROM_FILE`, load definition (flags, D3D format, level count) and the card
  memory that `Image_GetCardMemoryAmount` computes. Without it the image comes out with format 0 and BO1 stops on
  `unhandled case: 0` (`r_image_load_common.cpp`). `tools/bo2/bo2_weapon.py` needs it (`docs/BO2_WEAPONS.md`).

## Build (Linux / WSL)

    git clone --branch v0.34.0 https://github.com/Laupetin/OpenAssetTools.git oat-src
    cp -r tools/bo2/oat/src/. oat-src/src/
    git -C oat-src apply "$PWD/tools/bo2/oat/ObjWriterT6.patch"
    git -C oat-src apply "$PWD/tools/bo2/oat/ImageLoaderExternalT5.patch"
    cd oat-src && PREMAKE_NO_PROMPT=1 ./generate.sh        # downloads premake (SHA-256 checked) and the submodules
    make -C build config=release_x64 UnlinkerCli -j8        # build/bin/Release_x64/Unlinker
    make -C build config=release_x64 LinkerCli -j8          # build/bin/Release_x64/Linker (BO2 weapons)

premake gathers the source files when it generates the makefiles: a file added later needs `./premake5
--file=../premake5.lua gmake` again from `build/`, or the link misses it (`undefined reference to vtable`).
After editing a `.template` file (the image loader patch is one), run `make` twice: the first run regenerates
`build/src/.../*T5.cpp` but compiles the old object, so the Linker keeps the old behaviour without any error.

## Use

    Z="/mnt/c/Program Files (x86)/Steam/steamapps/common/Call of Duty Black Ops II/zone/all"
    oat-src/build/bin/Release_x64/Unlinker --include-assets gfxworld,clipmap,comworld,gameworldmp -o out/?zone? "$Z/zm_nuked.ff"
    oat-src/build/bin/Release_x64/Unlinker --include-assets mapents,xmodel,image,material \
        --model-format OBJ --image-format DDS -o out/?zone? "$Z/zm_nuked.ff"

The output is the player's own game data: keep it outside the repository (`build/` or another folder), never commit it.

## The vertex format it relies on

OAT keeps `GfxWorldDraw::vd0` as raw bytes. Read from `zm_nuked`'s buffer (2026-11-09): 36 bytes per vertex, xyz (3
floats), binormal sign (float), colour (4 bytes), texCoord (2 half floats), lightmap coord (2 half floats), normal and
tangent (packed). A surface's vertices start at `srfTriangles_t::vertexDataOffset0` and its indices count from there;
`vertexDataSize0` holds a few more vertices than `vertexCount` says, so only the stride is checked. The dumper refuses
a buffer whose size is not a multiple of 36.
