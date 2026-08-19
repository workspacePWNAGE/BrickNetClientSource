#pragma once
#include "../raylib.h"

namespace bn_mesh {

    struct LoadedMeshComponent {
        Model model;
        Vector3 localOffset;
        Vector3 scale;
        Color color;
        BoundingBox localBox;
    };

    inline LoadedMeshComponent LoadModelAsset(const char* filePath, Vector3 offset, Vector3 scaling, Color partColor) {
        LoadedMeshComponent comp;
        comp.model = LoadModel(filePath);
        comp.localOffset = offset;
        comp.scale = scaling;
        comp.color = partColor;
        
        comp.localBox = GetModelBoundingBox(comp.model);
        
        comp.localBox.min = (Vector3){ comp.localBox.min.x * scaling.x, comp.localBox.min.y * scaling.y, comp.localBox.min.z * scaling.z };
        comp.localBox.max = (Vector3){ comp.localBox.max.x * scaling.x, comp.localBox.max.y * scaling.y, comp.localBox.max.z * scaling.z };
        
        return comp;
    }

    inline LoadedMeshComponent CreateFacePlane(float width, float height, Vector3 offset, Color partColor) {
        LoadedMeshComponent comp;
        Mesh planeMesh = GenMeshPlane(width, height, 1, 1);
        
        comp.model = LoadModelFromMesh(planeMesh);
        comp.localOffset = offset;
        comp.scale = (Vector3){ 1.0f, 1.0f, 1.0f };
        comp.color = partColor;
        
        comp.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        
        comp.localBox = (BoundingBox){
            { -width * 0.5f, -height * 0.5f, -0.01f },
            { width * 0.5f, height * 0.5f, 0.01f }
        };
        
        return comp;
    }
    
    inline LoadedMeshComponent LoadModelAsset(const char* filePath, Vector3 offset, Vector3 scaling, Color partColor, Texture texture) {
        LoadedMeshComponent comp = LoadModelAsset(filePath, offset, scaling, partColor);
        comp.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        return comp;
    }

    inline void UnloadModelAsset(LoadedMeshComponent& comp) {
        UnloadModel(comp.model);
    }
}