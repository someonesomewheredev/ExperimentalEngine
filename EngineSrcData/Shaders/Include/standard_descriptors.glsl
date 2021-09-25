#ifndef STANDARD_DESCRIPTORS_H
#define STANDARD_DESCRIPTORS_H
#include <aobox.glsl>
#include <aosphere.glsl>
#include <light.glsl>
#include <material.glsl>

layout(binding = 0) uniform MultiVP {
    mat4 view[2];
    mat4 projection[2];
    vec4 viewPos[2];
};

layout(std430, binding = 1) readonly buffer LightBuffer {
    mat4 otherShadowMatrices[4];
    // (light count, yzw cascade texels per unit)
    vec4 pack0;
    // (ao box count, ao sphere count, zw unused)
    vec4 pack1;
    mat4 dirShadowMatrices[3];
	Light lights[256];
    AOBox aoBox[16];
	AOSphere aoSphere[16];
	uint sphereIds[16];
};

layout(std140, binding = 2) readonly buffer MaterialSettingsBuffer {
    Material materials[256];
};

layout(std140, binding = 3) readonly buffer ModelMatrices {
    mat4 modelMatrices[];
};

layout (binding = 4) uniform sampler2D tex2dSampler[];
layout (binding = 5) uniform sampler2DArrayShadow shadowSampler;
layout (binding = 6) uniform samplerCube cubemapSampler[];
layout (binding = 7) uniform sampler2D brdfLutSampler;
layout (binding = 8) uniform sampler2DShadow additionalShadowSampler[];

struct LightingTile {
    uint lightIdMasks[8];
};

layout (binding = 9) readonly uniform LightTileInfo {
    uint tileSize;
    uint tilesPerEye;
    uint numTilesX;
    uint numTilesY;
} buf_LightTileInfo;

layout (binding = 10) readonly buffer TileLightCounts {
    uint tileLightCounts[];
} buf_LightTileLightCounts;

layout (binding = 11) readonly buffer TileLightTiles {
    LightingTile tiles[];
} buf_LightTiles;

layout(std430, binding = 12) writeonly buffer PickingBuffer {
    uint objectID;
} pickBuf;
#endif
