#pragma once

// mod: mapkit "mesh" edit - another game's drawable world (tools/bo2/bo2_mesh.py, docs/BO2_MAPS.md), drawn as one rigid
// XModel per chunk and one XModel per prop model (with its LODs, drawn once per placement), with materials cloned from
// BO1 model materials.

// reads the layout's mesh edit and builds the models, images and materials; at the end of R_FinishLoadingModels
void __cdecl R_Mapkit_LoadMesh();
// drops the models and their GPU buffers (the images and material clones stay for the next load)
void __cdecl R_Mapkit_FreeMesh();
// lost device: releases the GPU buffers before the device Reset, rebuilds them after it (r_init.cpp)
void __cdecl R_Mapkit_ReleaseMeshBuffers();
void __cdecl R_Mapkit_RestoreMeshBuffers();
// adds the visible chunks and props to the scene, as the skybox model is added
void __cdecl R_Mapkit_AddMesh(const float *viewOrigin);
