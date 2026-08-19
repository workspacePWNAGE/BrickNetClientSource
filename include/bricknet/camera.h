#pragma once
#include "../raylib.h"
#include "../raymath.h"

namespace bn_camera {
    struct CustomCamera {
        Camera3D raylibCamera;
        float targetDistance;
        float minDistance;
        float maxDistance;
        float yaw;
        float pitch;
        Vector3 freeCamTarget;
    };

    inline CustomCamera Create(Vector3 startPos) {
        CustomCamera cam = { 0 };
        cam.targetDistance = 15.0f;
        cam.minDistance = 3.0f;
        cam.maxDistance = 40.0f;
        cam.yaw = 0.0f;
        cam.pitch = 0.4f;

        cam.raylibCamera.position = (Vector3){ 0.0f, 0.0f, 0.0f };
        cam.raylibCamera.target = startPos;
        cam.raylibCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        cam.raylibCamera.fovy = 75.0f;
        cam.raylibCamera.projection = CAMERA_PERSPECTIVE;
        cam.freeCamTarget = startPos;

        return cam;
    }

    inline void Update(CustomCamera& cam, Vector3 playerPos, Vector3 movementVector, bool isWorkshop) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && !isWorkshop) {
            cam.targetDistance -= wheel * 1.5f;
            if (cam.targetDistance < cam.minDistance) cam.targetDistance = cam.minDistance;
            if (cam.targetDistance > cam.maxDistance) cam.targetDistance = cam.maxDistance;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            DisableCursor(); 
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 mouseDelta = GetMouseDelta();
            cam.yaw -= mouseDelta.x * 0.005f;
            cam.pitch += mouseDelta.y * 0.005f;

            if (cam.pitch > 1.35f) cam.pitch = 1.35f;
            if (cam.pitch < -1.35f) cam.pitch = -1.35f;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            EnableCursor();
        }

        Vector3 targetPoint;
        
        if (isWorkshop) {
            cam.freeCamTarget = Vector3Add(cam.freeCamTarget, movementVector);
            targetPoint = cam.freeCamTarget;
        } else {
            targetPoint = (Vector3){ playerPos.x, playerPos.y + 3.0f, playerPos.z };
        }
        
        cam.raylibCamera.target = targetPoint;
        
        cam.raylibCamera.position.x = targetPoint.x + cam.targetDistance * cosf(cam.pitch) * sinf(cam.yaw);
        cam.raylibCamera.position.y = targetPoint.y + cam.targetDistance * sinf(cam.pitch);
        cam.raylibCamera.position.z = targetPoint.z + cam.targetDistance * cosf(cam.pitch) * cosf(cam.yaw);
    }

    inline void GetMovementDirections(const CustomCamera& cam, Vector3& outForward, Vector3& outRight, bool isWorkshop) {
        Vector3 forward = Vector3Subtract(cam.raylibCamera.target, cam.raylibCamera.position);

        if (!isWorkshop) {
            forward.y = 0.0f;
        }

        forward = Vector3Normalize(forward);

        Vector3 right = Vector3CrossProduct(forward, cam.raylibCamera.up);
        right = Vector3Normalize(right);

        outForward = forward;
        outRight = right;
    }

    inline bool CheckBoxVisible(const CustomCamera& cam, BoundingBox box) {
        Vector3 boxCenter = {
            (box.min.x + box.max.x) * 0.5f,
            (box.min.y + box.max.y) * 0.5f,
            (box.min.z + box.max.z) * 0.5f
        };

        Vector3 boxSize = {
            box.max.x - box.min.x,
            box.max.y - box.min.y,
            box.max.z - box.min.z
        };
        float boxRadius = Vector3Length(boxSize) * 0.5f;

        Vector3 toCenter = Vector3Subtract(boxCenter, cam.raylibCamera.position);
        float distanceToCenter = Vector3Length(toCenter);
        
        if (distanceToCenter <= boxRadius + 5.0f) {
            return true;
        }

        Vector3 corners[8] = {
            { box.min.x, box.min.y, box.min.z },
            { box.max.x, box.min.y, box.min.z },
            { box.min.x, box.max.y, box.min.z },
            { box.max.x, box.max.y, box.min.z },
            { box.min.x, box.min.y, box.max.z },
            { box.max.x, box.min.y, box.max.z },
            { box.min.x, box.max.y, box.max.z },
            { box.max.x, box.max.y, box.max.z }
        };

        Vector3 lookVec = Vector3Subtract(cam.raylibCamera.target, cam.raylibCamera.position);
        Vector3 camForward;

        if (Vector3Length(lookVec) < 0.001f) {
            camForward = (Vector3){ 0.0f, -1.0f, 0.0f };
        } else {
            camForward = Vector3Normalize(lookVec);
        }

        for (int i = 0; i < 8; i++) {
            Vector3 toCorner = Vector3Subtract(corners[i], cam.raylibCamera.position);
            float distance = Vector3Length(toCorner);
            if (distance < 6.0f) return true;

            toCorner = Vector3Normalize(toCorner);
            float dotProduct = Vector3DotProduct(camForward, toCorner);
            if (dotProduct > 0.50f) return true;
        }

        return false;
    }
}