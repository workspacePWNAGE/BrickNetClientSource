#pragma once
#include "../group.h"
#include "../../raylib.h"
#include "../colors.h"

namespace group_player {
    inline bn_group::BrickGroup createPlayerGroup(Texture top, Texture bottom, bn_mesh::LoadedMeshComponent hatData, Color larm, Color rarm, Color torso, Color head, Color lleg, Color rleg) {
        bn_group::BrickGroup player((Vector3){ 0.0f, 0.0f, 0.0f });
        
        bn_mesh::LoadedMeshComponent headMesh = bn_mesh::LoadModelAsset("assets/meshes/head.obj", Vector3{0.0f, 3.65f, 0.0f}, Vector3{0.65f, 0.65f, 0.65f}, head);
        bn_mesh::LoadedMeshComponent playerHat = hatData;
        bn_mesh::LoadedMeshComponent facePlane = bn_mesh::CreateFacePlane(1.2f, 1.2f, Vector3{0.0f, 3.65f, 0.65f}, WHITE);
        bn_mesh::LoadedMeshComponent shirtPlane = bn_mesh::CreateFacePlane(2.0f, 2.0f, Vector3{0.0f, 2.0f, 0.51f}, WHITE);

        player.AddPart((Vector3){ 0.0f, 2.0f, 0.0f }, (Vector3){ 2.0f, 2.0f, 1.0f }, torso, top, bottom);
        
        player.AddPart((Vector3){ -1.5f, 2.0f, 0.0f }, (Vector3){ 1.0f, 2.0f, 1.0f }, larm, top, bottom, bn_group::LEFT_ARM, 2.75f, 2.75f);
        player.AddPart((Vector3){ 1.5f, 2.0f, 0.0f }, (Vector3){ 1.0f, 2.0f, 1.0f }, rarm, top, bottom, bn_group::RIGHT_ARM, 2.75f, 2.75f);
        
        player.AddPart((Vector3){ -0.5f, 0.0f, 0.0f }, (Vector3){ 1.0f, 2.0f, 1.0f }, lleg, top, bottom, bn_group::LEFT_LEG, 1.0f, 1.0f);
        player.AddPart((Vector3){ 0.5f, 0.0f, 0.0f }, (Vector3){ 1.0f, 2.0f, 1.0f }, rleg, top, bottom, bn_group::RIGHT_LEG, 1.0f, 1.0f);

        player.AddMesh(headMesh, false);
        player.AddMesh(facePlane, true);
        player.AddMesh(playerHat, false);
        player.AddMesh(shirtPlane, false, true);

        return player;
    }
}