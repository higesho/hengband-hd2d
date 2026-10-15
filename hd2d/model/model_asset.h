#pragma once
#include "render/math3d.h"
#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace hd2d {
// Static GLB data. Positions and normals are already in the UI's Z-up coordinates.
struct ModelVertex { Vec3 position; Vec3 normal; float u{}, v{}; };
struct ModelImage { std::vector<std::uint8_t> bytes; int width{}, height{}; };
struct ModelInstance {
    Vec3 position{};
    Vec3 scale{1,1,1};
    float yaw{};
    Vec3 tint{1,1,1};
};

enum class TerrainModelKind : std::size_t { Rubble, Water, Lava, Shore, Count };
constexpr std::size_t kTerrainModelCount=static_cast<std::size_t>(TerrainModelKind::Count);
inline constexpr const char *kTerrainModelFiles[kTerrainModelCount]={
    "dungeon_rubble.glb","dungeon_water.glb","dungeon_lava.glb","dungeon_shore.glb"};
struct ModelTextureSlot {
    int image{-1};
    int wrap_s{10497},wrap_t{10497},min_filter{9729},mag_filter{9729};
};
struct ModelMaterial {
    ModelTextureSlot occlusion;
    float occlusion_strength{1.f};
    float emissive[3]{0,0,0};
    float color[4]{1,1,1,1};
    float metallic{1}, roughness{1};
    int image{-1};
    int wrap_s{10497}, wrap_t{10497};
    int min_filter{9729}, mag_filter{9729};
    bool double_sided{};
};
struct ModelPrimitive { std::vector<ModelVertex> vertices; ModelMaterial material; };
struct ModelAsset {
    std::vector<ModelPrimitive> primitives;
    std::vector<ModelImage> images;
    Vec3 bounds_min{}, bounds_max{};
    std::size_t vertex_count{};
};
// Transactional: failure leaves out unchanged. Does not access external URIs or GL.
bool load_glb(const std::string &path, ModelAsset &out, std::string &error);
// Terrain modules use a centered one-cell footprint, with ground level at Z=0.
bool validate_terrain_model(const ModelAsset &asset, bool wall, std::string &error);
}
