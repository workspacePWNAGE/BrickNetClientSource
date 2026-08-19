#pragma once

#include "../raylib.h"
#include "../rlgl.h"
#include "camera.h"
#include "colors.h"
#include "mesh.h"
#include "part.h"
#include <cmath>
#include <vector>

namespace bn_group {

enum AnimationType { NONE, LEFT_ARM, RIGHT_ARM, LEFT_LEG, RIGHT_LEG };

struct GroupedPart {
    AnimationType animType;
    Vector3 localOffset;
    Vector3 size;
    float pivotY;
    float jumpPivotY;
    Color color;
    Texture2D textureStuds;
    Texture2D textureInlet;
    BoundingBox worldBox;
    float swingAngle = 0.0f;
};

struct GroupedMesh {
    bn_mesh::LoadedMeshComponent meshAsset;
    BoundingBox worldBox;
    bool isFacePlane;
    bool isShirtPlane;
};

class BrickGroup {
private:
    Vector3 m_origin;
    std::vector<GroupedPart> m_parts;
    std::vector<GroupedMesh> m_meshes;

    float m_targetAngle = 0.0f;
    float m_currentAngle = 0.0f;
    float m_rotationSpeed = 10.0f;
    float m_animTimer = 0.0f;

    Vector3 boundsMin = { 0, 0, 0 };
    Vector3 boundsMax = { 0, 0, 0 };
    bool boundsInit = false;

    void UpdateTransforms(float angle) {
        float renderAngle = angle + (PI * 0.5f);
        float c = cosf(renderAngle);
        float s = sinf(renderAngle);

        for (auto &part : m_parts) {
            Vector3 worldPos = {
                m_origin.x + (part.localOffset.x * c - part.localOffset.z * s),
                m_origin.y + part.localOffset.y,
                m_origin.z + (part.localOffset.x * s + part.localOffset.z * c)
            };
            part.worldBox = bn_part::GetPartBounds(worldPos, part.size);
        }

        for (auto &mesh : m_meshes) {
            Vector3 worldPos = {
                m_origin.x + (mesh.meshAsset.localOffset.x * c - mesh.meshAsset.localOffset.z * s),
                m_origin.y + mesh.meshAsset.localOffset.y,
                m_origin.z + (mesh.meshAsset.localOffset.x * s + mesh.meshAsset.localOffset.z * c)
            };

            Vector3 halfSize = {
                (mesh.meshAsset.localBox.max.x - mesh.meshAsset.localBox.min.x) * 0.5f,
                (mesh.meshAsset.localBox.max.y - mesh.meshAsset.localBox.min.y) * 0.5f,
                (mesh.meshAsset.localBox.max.z - mesh.meshAsset.localBox.min.z) * 0.5f
            };

            mesh.worldBox = (BoundingBox){
                { worldPos.x - halfSize.x, worldPos.y - halfSize.y, worldPos.z - halfSize.z },
                { worldPos.x + halfSize.x, worldPos.y + halfSize.y, worldPos.z + halfSize.z }
            };
        }
    }

public:
    BrickGroup(Vector3 startPosition) : m_origin(startPosition) {}

    void Tick(float deltaTime, bool isMoving, bool isGrounded) {
        float diff = m_targetAngle - m_currentAngle;
        
        if (diff > PI) diff -= 2 * PI;
        else if (diff < -PI) diff += 2 * PI;

        if (std::abs(diff) > 0.001f) {
            m_currentAngle += diff * m_rotationSpeed * deltaTime;
        }

        if (!isGrounded) {
            for (auto &part : m_parts) {
                part.swingAngle = (part.animType == LEFT_ARM || part.animType == RIGHT_ARM) ? 3.14f : 0.0f;
            }
        } else if (isMoving) {
            m_animTimer += deltaTime * 12.0f;
            float swing = sinf(m_animTimer) * 0.5f;
            for (auto &part : m_parts) {
                if (part.animType == LEFT_ARM || part.animType == RIGHT_LEG)
                    part.swingAngle = swing;
                else if (part.animType == RIGHT_ARM || part.animType == LEFT_LEG)
                    part.swingAngle = -swing;
            }
        } else {
            m_animTimer = 0.0f;
            for (auto &part : m_parts) part.swingAngle = 0.0f;
        }
        
        UpdateTransforms(m_currentAngle);
    }

    void AddPart(Vector3 localOffset, Vector3 size, Color color, Texture2D studs, Texture2D inlet, AnimationType anim = NONE, float pivotY = 0.0f, float jumpPivotY = 0.0f) {
        m_parts.push_back({anim, localOffset, size, pivotY, jumpPivotY, color, studs, inlet, {}, 0.0f});
        UpdateTransforms(m_currentAngle);
    }

    void AddMesh(bn_mesh::LoadedMeshComponent meshAsset, bool isFacePlane = false, bool isShirtPlane = false) {
        m_meshes.push_back({meshAsset, {}, isFacePlane, isShirtPlane});
        UpdateTransforms(m_currentAngle);
    }

    void SetPosition(Vector3 newPosition) {
        m_origin = newPosition;
        UpdateTransforms(m_currentAngle);
    }

    void SetRotation(float radians) { m_targetAngle = radians; }

    Vector3 GetPosition() const { return m_origin; }

    void Draw(const bn_camera::CustomCamera &camera, Shader lightingShader, Texture faceTexture, Texture shirtTexture) const {
        float renderAngle = m_currentAngle + (PI * 0.5f);

        BeginShaderMode(lightingShader);
        for (const auto &part : m_parts) {
            if (bn_camera::CheckBoxVisible(camera, part.worldBox)) {
                rlPushMatrix();
                rlTranslatef(m_origin.x, m_origin.y, m_origin.z);
                rlRotatef(renderAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);

                if (part.animType != NONE) {
                    float activePivot = (std::abs(part.swingAngle - 3.14f) < 0.01f) ? part.jumpPivotY : part.pivotY;
                    rlTranslatef(0, activePivot, 0);
                    rlRotatef(part.swingAngle * RAD2DEG, 1.0f, 0.0f, 0.0f);
                    rlTranslatef(part.localOffset.x, part.localOffset.y - activePivot, part.localOffset.z);
                } else {
                    rlTranslatef(part.localOffset.x, part.localOffset.y, part.localOffset.z);
                }

                bn_part::CreatePart({0, 0, 0}, part.size, part.color, part.textureStuds, part.textureInlet);
                rlPopMatrix();
            }
        }
        EndShaderMode();

        for (const auto &meshComp : m_meshes) {
            if (bn_camera::CheckBoxVisible(camera, meshComp.worldBox)) {
                rlPushMatrix();
                rlTranslatef(m_origin.x, m_origin.y, m_origin.z);
                rlRotatef(renderAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
                rlTranslatef(meshComp.meshAsset.localOffset.x, meshComp.meshAsset.localOffset.y, meshComp.meshAsset.localOffset.z);

                if (meshComp.isFacePlane || meshComp.isShirtPlane) rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
                rlScalef(meshComp.meshAsset.scale.x, meshComp.meshAsset.scale.y, meshComp.meshAsset.scale.z);

                for (int i = 0; i < meshComp.meshAsset.model.materialCount; i++) {
                    meshComp.meshAsset.model.materials[i].shader = lightingShader;
                    if (meshComp.isFacePlane) {
                        meshComp.meshAsset.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = faceTexture;
                        meshComp.meshAsset.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
                    } else if (meshComp.isShirtPlane) {
                        meshComp.meshAsset.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = shirtTexture;
                        meshComp.meshAsset.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
                    } else {
                        meshComp.meshAsset.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = meshComp.meshAsset.color;
                    }
                }

                if (meshComp.isFacePlane || meshComp.isShirtPlane) {
                    rlDisableDepthMask();
                    DrawModel(meshComp.meshAsset.model, {0, 0, 0}, 1.0f, WHITE);
                    rlEnableDepthMask();
                } else {
                    DrawModel(meshComp.meshAsset.model, {0, 0, 0}, 1.0f, WHITE);
                }
                rlPopMatrix();
            }
        }
    }

    BoundingBox GetBoundingBox() const {
        BoundingBox box;

        bool initialized = false;

        auto expand = [&](const BoundingBox& b) {
            if (!initialized) {
                box = b;
               initialized = true;
                return;
            }

            box.min.x = std::min(box.min.x, b.min.x);
            box.min.y = std::min(box.min.y, b.min.y);
            box.min.z = std::min(box.min.z, b.min.z);

            box.max.x = std::max(box.max.x, b.max.x);
            box.max.y = std::max(box.max.y, b.max.y);
            box.max.z = std::max(box.max.z, b.max.z);
        };

        for (const auto& p : m_parts) expand(p.worldBox);
        for (const auto& m : m_meshes) expand(m.worldBox);

        if (!initialized) {
            return {{0,0,0},{0,0,0}};
        }

        return box;
    }
};

}