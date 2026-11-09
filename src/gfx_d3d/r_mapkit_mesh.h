#pragma once

// mod: mapkit "mesh" edit - another game's drawable world (tools/bo2/bo2_mesh.py, docs/BO2_MAPS.md), drawn as one rigid
// XModel per chunk with materials cloned from a BO1 model material.

// reads the layout's mesh edit and builds the models, images and materials; at the end of R_FinishLoadingModels
void __cdecl R_Mapkit_LoadMesh();
// drops the models and their GPU buffers (the images and material clones stay for the next load)
void __cdecl R_Mapkit_FreeMesh();
// adds the visible chunks to the scene, as the skybox model is added
void __cdecl R_Mapkit_AddMesh();
