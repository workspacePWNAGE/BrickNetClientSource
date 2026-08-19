#pragma once
#include "colors.h"
#include "../raylib.h"
#include "../rlgl.h"

namespace bn_part {

inline BoundingBox GetPartBounds(Vector3 pos, Vector3 size) {
    return {
        { pos.x - size.x * 0.5f, pos.y - size.y * 0.5f, pos.z - size.z * 0.5f },
        { pos.x + size.x * 0.5f, pos.y + size.y * 0.5f, pos.z + size.z * 0.5f }
    };
}

// no comments here cuz i took like 99% of this from a forum
// idk what it does but it works
inline void CreatePart(Vector3 pos, Vector3 size, Color color, Texture2D top, Texture2D bottom) {
    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;
    
    float bias = 0.001f;
    float hyb = hy + bias;

    auto Shade = [](Color c, float f) -> Color
    {
        return {
            (unsigned char)(c.r * f),
            (unsigned char)(c.g * f),
            (unsigned char)(c.b * f),
            c.a
        };
    };

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);

    rlBegin(RL_QUADS);

    Color topColor = Shade(color, 1.0f);
    rlColor4ub(topColor.r, topColor.g, topColor.b, topColor.a);
    rlNormal3f(0.0f, 0.0f, 1.0f);
    rlVertex3f(-hx,-hy, hz); rlVertex3f( hx,-hy, hz); rlVertex3f( hx, hy, hz); rlVertex3f(-hx, hy, hz);
    rlNormal3f(0.0f, 0.0f, -1.0f);
    rlVertex3f( hx,-hy,-hz); rlVertex3f(-hx,-hy,-hz); rlVertex3f(-hx, hy,-hz); rlVertex3f( hx, hy,-hz);

    rlNormal3f(-1.0f, 0.0f, 0.0f);
    rlVertex3f(-hx,-hy,-hz); rlVertex3f(-hx,-hy, hz); rlVertex3f(-hx, hy, hz); rlVertex3f(-hx, hy,-hz);
    rlNormal3f(1.0f, 0.0f, 0.0f);
    rlVertex3f( hx,-hy, hz); rlVertex3f( hx,-hy,-hz); rlVertex3f( hx, hy,-hz); rlVertex3f( hx, hy, hz);

    rlNormal3f(0.0f, 1.0f, 0.0f);
    rlVertex3f(-hx, hy,  hz); rlVertex3f( hx, hy,  hz); rlVertex3f( hx, hy, -hz); rlVertex3f(-hx, hy, -hz);

    rlNormal3f(0.0f, -1.0f, 0.0f);
    rlVertex3f(-hx, -hy, -hz); rlVertex3f( hx, -hy, -hz); rlVertex3f( hx, -hy,  hz); rlVertex3f(-hx, -hy,  hz);

    rlEnd();

    if (top.id > 0)
    {
        rlSetTexture(top.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlNormal3f(0.0f, 1.0f, 0.0f);
        rlTexCoord2f(0.0f,   0.0f);   rlVertex3f(-hx,  hyb,  hz);
        rlTexCoord2f(size.x, 0.0f);   rlVertex3f( hx,  hyb,  hz);
        rlTexCoord2f(size.x, size.z); rlVertex3f( hx,  hyb, -hz);
        rlTexCoord2f(0.0f,   size.z); rlVertex3f(-hx,  hyb, -hz);
        rlEnd();
    }

    if (bottom.id > 0)
    {
        rlSetTexture(bottom.id);
        rlBegin(RL_QUADS);
        rlColor4ub(128, 128, 128, 255);
        rlNormal3f(0.0f, -1.0f, 0.0f);
        rlTexCoord2f(0.0f,   0.0f);   rlVertex3f(-hx, -hyb, -hz);
        rlTexCoord2f(size.x, 0.0f);   rlVertex3f( hx, -hyb, -hz);
        rlTexCoord2f(size.x, size.z); rlVertex3f( hx, -hyb,  hz);
        rlTexCoord2f(0.0f,   size.z); rlVertex3f(-hx, -hyb,  hz);
        rlEnd();
    }

    rlSetTexture(0);
    rlDrawRenderBatchActive();

    rlPopMatrix();
}

}