#pragma once
#include "model/model_asset.h"
#include "render/lighting.h"
#include "render/voxel_renderer.h"

namespace hd2d {
class ModelRenderer {
public:
    ModelRenderer() = default;
    ModelRenderer(const ModelRenderer &) = delete;
    ModelRenderer &operator=(const ModelRenderer &) = delete;
    // GL context must be current. Call shutdown before destroying that context.
    bool init(const ModelAsset &asset, std::string &error);
    void shutdown();
    bool ready() const { return program_ != 0; }
    void set_wear(const WearParams &wear,MaterialClass material=MaterialClass::Stone) { wear_=wear;wear_material_=material; }
    // instance must be a rigid transform with an optional positive uniform scale.
    void draw(const Mat4 &view_projection, const Mat4 &instance, const SceneLighting &lighting,
        const Vec3 &eye, const Mat4 &light_view_projection = Mat4::identity(),
        gl::GLuint shadow_texture = 0, int shadow_side = 1,
        const Vec3 *offsets = nullptr, std::size_t count = 1,
        const Cutaway *cutaways = nullptr, std::size_t cutaway_count = 0,
        const ModelInstance *instances = nullptr);
    void draw_depth(const Mat4 &light_view_projection, const Mat4 &instance,
        const Vec3 *offsets = nullptr, std::size_t count = 1, const ModelInstance *instances = nullptr);
private:
    struct Mesh { gl::GLuint vao{}, vbo{}; int count{}; ModelMaterial material; };
    std::vector<Mesh> meshes_;
    std::vector<gl::GLuint> textures_;
    std::vector<gl::GLuint> occlusion_textures_;
    gl::GLuint program_{}, depth_program_{}, white_{};
    LightUniforms lights_;
    WearUniforms wear_uniforms_;
    WearParams wear_;
    MaterialClass wear_material_{MaterialClass::Clean};
    gl::GLuint offsets_{};
    std::vector<ModelInstance> placement_buffer_;
    void upload_offsets(const Vec3 *offsets, std::size_t count, const ModelInstance *instances);
    void bind_offsets(gl::GLuint vao);
};
}
