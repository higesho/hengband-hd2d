#include "app/hd2d_app.h"
#include "app/app_support.h"
#include "app/test_keyboard.h"
#include "model/model_asset.h"
#include "render/model_renderer.h"
#include "render/shadow_map.h"
#include "world/terrain_view.h"
#include "frame/cell_feature_bits.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

namespace hd2d {
namespace {
ModelAsset make_model_ground(const ModelAsset &model)
{
    ModelAsset ground;
    ModelPrimitive primitive;
    primitive.material.color[0] = 0.24f;
    primitive.material.color[1] = 0.27f;
    primitive.material.color[2] = 0.30f;
    primitive.material.metallic = 0.f;
    primitive.material.roughness = 1.f;
    primitive.material.double_sided = true;
    const float span = std::max({model.bounds_max.x - model.bounds_min.x,
        model.bounds_max.y - model.bounds_min.y, model.bounds_max.z - model.bounds_min.z, 0.25f});
    const float x0 = model.bounds_min.x - span;
    const float x1 = model.bounds_max.x + span;
    const float y0 = model.bounds_min.y - span;
    const float y1 = model.bounds_max.y + span;
    const float z = model.bounds_min.z - span * 0.002f;
    for (const Vec3 p : {Vec3{x0,y0,z}, Vec3{x1,y0,z}, Vec3{x1,y1,z},
             Vec3{x0,y0,z}, Vec3{x1,y1,z}, Vec3{x0,y1,z}}) {
        primitive.vertices.push_back(ModelVertex{p, Vec3{0,0,1}, 0, 0});
    }
    ground.vertex_count = primitive.vertices.size();
    ground.primitives.push_back(std::move(primitive));
    return ground;
}
}


int run_model_terrain_check(const AppOptions &options)
{
    int failures=0, total=0;
    const auto check=[&](bool ok,const char *label) {
        ++total; if(!ok) ++failures;
        std::printf("%s %s\n",ok?"PASS":"FAIL",label);
    };
    std::string error;
    ModelAsset wall,floor;
    check(load_glb("assets/models3d/sample_wall_block.glb",wall,error)
        && validate_terrain_model(wall,true,error),"wall block contract");
    check(load_glb("assets/models3d/sample_floor_block.glb",floor,error)
        && validate_terrain_model(floor,false,error),"floor block contract");
    check(!validate_terrain_model(wall,false,error),"wall rejected as floor");
    check(!validate_terrain_model(floor,true,error),"floor rejected as wall");
    ModelAsset bad=wall; bad.bounds_max.x=0.7f;
    check(!validate_terrain_model(bad,true,error),"overhanging module rejected");
    bad=wall; bad.bounds_min.x=0;
    check(!validate_terrain_model(bad,true,error),"off-center or narrow module rejected");
    bad=wall; bad.vertex_count=30001;
    check(!validate_terrain_model(bad,true,error),"repeated model vertex budget");
    PrefabLibrary library;
    if(!library.load(options.voxel_dir,error)) {
        std::fprintf(stderr,"[model-terrain-check] %s\n",error.c_str()); return 1;
    }
    FloorMeaning meaning;
    meaning.width=8; meaning.height=1;
    meaning.identity.kind=static_cast<int>(FloorKind::Dungeon);
    for(auto role:{CellRole::Unknown,CellRole::RoomFloor,CellRole::CorridorFloor,
            CellRole::StructuralWall,CellRole::Bedrock,CellRole::Doorway,CellRole::Stairs,CellRole::RoomFloor})
        meaning.roles.push_back(static_cast<std::uint8_t>(role));
    meaning.marks.assign(8,0); meaning.wall_depth.assign(8,1); meaning.mountains.assign(8,0);
    TerrainMemory memory;
    memory.width=8; memory.height=1;
    memory.ids={0,1,1,56,56,4,6,1302}; // Explicit chasm must retain its dedicated rule.
    memory.flags.assign(8,CELL_FEAT_KNOWN); memory.ascii.assign(8,0); memory.fg.assign(8,0);
    TerrainView before,after;
    const TerrainModels both{1.3f,true};
    auto build=[&](TerrainView &out,const TerrainModels &models,bool blocks=false,
                   Frustum frustum=Frustum::everything()) {
        build_terrain_view(meaning,frustum,out,&memory,&library,nullptr,0,0,false,nullptr,false,false,blocks,models);
    };
    build(before,{}); build(after,both);
    /* RogueClone2 のように terrain_id を持たず CellRole::Stairs だけを送るコアでも、
     * 役割の既定から立体階段が必ず置かれることを固定する。 */
    {
        TerrainMemory role_only_memory = memory;
        role_only_memory.ids[6] = 0;
        TerrainView role_only;
        build_terrain_view(meaning,Frustum::everything(),role_only,&role_only_memory,&library,
            nullptr,0,0,false,nullptr,false,false,false,{});
        const int stair = library.find("stairs_down");
        check(stair >= 0 && static_cast<std::size_t>(stair) < role_only.lib.size()
                && !role_only.lib[static_cast<std::size_t>(stair)].empty(),
            "role-only stairs always submit the 3D stair prefab");
    }
    check(before.model_floors.empty() && before.model_walls.empty(),"default display unchanged");
    check(after.model_floors.size()==2 && after.model_walls.size()==2,"ordinary floors and walls selected");
    check(before.lib_instances==after.lib_instances+4,"old ground and wall meshes replaced once");
    check(after.under_slabs.size()==before.under_slabs.size(),"wall underlay retained for cutaway");
    check(after.model_walls.size()==2 && after.model_walls[0].position.x==3.5f
        && after.model_walls[1].position.x==4.5f,"one centered module per wall cell");
    check(after.model_floors.size()==2 && after.model_floors[0].position.x==1.5f
        && after.model_floors[1].position.x==2.5f,"unknown doors stairs and special floor excluded");
    check(after.occluder_z[3]>=1.3f && after.occluder_z[4]>=1.3f,"model height used for occlusion");
    build(after,both);
    check(after.model_walls.size()==2 && after.model_floors.size()==2,"instances rebuilt without accumulation");
    TerrainView repeat;
    build(repeat,both);
    const auto same_instance=[](const ModelInstance &a,const ModelInstance &b) {
        return a.position.x==b.position.x && a.position.y==b.position.y && a.position.z==b.position.z
            && a.scale.x==b.scale.x && a.scale.y==b.scale.y && a.scale.z==b.scale.z
            && a.yaw==b.yaw && a.tint.x==b.tint.x && a.tint.y==b.tint.y && a.tint.z==b.tint.z;
    };
    check(same_instance(after.model_walls[0],repeat.model_walls[0]),"model variation stable across rebuilds");
    TerrainView clipped;
    build(clipped,both,false,Frustum::slab_xy(3,0,4,1));
    const auto found=std::find_if(clipped.model_walls.begin(),clipped.model_walls.end(),
        [](const ModelInstance &i){return i.position.x==3.5f;});
    check(found!=clipped.model_walls.end() && same_instance(*found,repeat.model_walls[0]),"model variation independent of visible cells");
    check(repeat.model_floors[0].position.z==0.f && repeat.model_floors[0].scale.z==1.f,
        "model floor height and thickness preserved");
    build(after,{1.3f,false});
    check(after.model_walls.size()==2 && after.model_floors.empty(),"wall-only selection");
    build(after,{0,true});
    check(after.model_walls.empty() && after.model_floors.size()==2,"floor-only selection");
    build(after,{1.3f,true,true});
    check(after.model_floors.size()==4 && after.model_walls.size()==2,"force mode preserves chasm shape while replacing doorway bases");
    check(after.under_slabs.empty() && after.model_under_floors.size()==2,"force mode replaces wall underlays");
    build(after,both,true);
    check(after.model_walls.empty() && after.model_floors.empty(),"TRON preserves original block display");
    build(after,both,false,Frustum::slab_xy(100,100,101,101));
    check(after.model_walls.empty() && after.model_floors.empty(),"offscreen terrain is not submitted");
    meaning.identity.kind=static_cast<int>(FloorKind::Surface); build(after,both);
    check(after.model_walls.empty() && after.model_floors.empty(),"surface and town left to existing rules");
    TerrainModels pack=both;pack.replace_all=true;
    std::array<ModelAsset,kTerrainModelCount> part_assets;
    for(std::size_t i=0;i<kTerrainModelCount;++i) {
        pack.parts[i]=load_glb(std::string("assets/models3d/dungeon_stone/")+kTerrainModelFiles[i],part_assets[i],error);
        pack.part_heights[i]=part_assets[i].bounds_max.z;
        check(pack.parts[i],kTerrainModelFiles[i]);
    }
    const auto water=static_cast<std::size_t>(TerrainModelKind::Water);
    const auto lava=static_cast<std::size_t>(TerrainModelKind::Lava);
    const auto shore=static_cast<std::size_t>(TerrainModelKind::Shore);
    const auto rubble=static_cast<std::size_t>(TerrainModelKind::Rubble);
    check(part_assets[water].bounds_max.z<-.15f && part_assets[lava].bounds_max.z<-.15f,"liquid surfaces below floor level");
    check(std::abs(part_assets[shore].bounds_max.z)<.001f && part_assets[shore].bounds_min.z<-.25f,"shore side reaches the floor and liquid depth");
    meaning.identity.kind=static_cast<int>(FloorKind::Dungeon);
    memory.ids[1]=83;memory.ids[2]=84;memory.ids[7]=85;
    memory.flags[1]=memory.flags[2]=CELL_FEAT_KNOWN|CELL_FEAT_WATER;
    memory.flags[7]=CELL_FEAT_KNOWN|CELL_FEAT_LAVA;
    build(after,pack);
    check(after.model_parts[water].size()==2 && after.model_parts[lava].size()==1,"water and lava retain distinct model groups");
    const auto &liquid_instance=after.model_parts[water][0];
    check(liquid_instance.yaw==0.f && liquid_instance.tint.x==1.f && liquid_instance.tint.y==1.f
        && liquid_instance.tint.z==1.f,"liquids excluded from model variation");
    check(after.model_parts[shore].size()==10,"no internal stone divider between adjacent water cells");
    meaning.identity.kind=static_cast<int>(FloorKind::Surface);build(after,pack);
    check(!after.model_ground.empty() && after.model_parts[water].size()==2,"common ground models also apply to surface terrain");
    TerrainView protected_view;protected_view.lib.resize(library.count());
    const int building=library.find("house_wall_n"),ground=library.find("floor_room_01"),rocks=library.find("rubble_pile");
    check(building>=0 && ground>=0 && rocks>=0,"building preservation fixtures exist");
    if(building>=0 && ground>=0 && rocks>=0) {
        protected_view.lib[building].push_back(InstanceData{});
        protected_view.lib[ground].push_back(InstanceData{});
        protected_view.lib[rocks].push_back(InstanceData{});protected_view.lib_instances=3;
        apply_terrain_model_replacements(protected_view,nullptr,&library,pack);
        check(protected_view.lib[building].size()==1,"prefab building retained unchanged");
        check(protected_view.model_ground.size()==1 && protected_view.model_parts[rubble].size()==1,"ordinary ground and rubble converted beside building");
    }
    TerrainView ray_probe;
    ray_probe.occluder_w=10; ray_probe.occluder_h=10; ray_probe.occluder_z.assign(100,0.f);
    ray_probe.occluder_z[6*10+5]=1.036f;
    check(line_of_sight_blocked(ray_probe,Vec3{5.5f,8.5f,3.f},5,5),
        "ray hitting a wall after entering its cell is blocked");
    std::printf("MODEL_TERRAIN_CHECK: %d/%d\n",total-failures,total);
    return failures?1:0;
}

int run_model_view(const AppOptions &options)
{
    ModelAsset asset;
    std::string error;
    if (!load_glb(options.model_view_path, asset, error)) {
        std::fprintf(stderr, "[model-view] %s\n", error.c_str());
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[model-view] SDL: %s\n", SDL_GetError());
        return 1;
    }
    Window window;
    if (!create_window(options, window, error)) {
        std::fprintf(stderr, "[model-view] %s\n", error.c_str());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowTitle(window.window, "3D Model - drag/arrows: rotate, wheel/+/-: zoom, R: reload, Esc: close");
    auto renderer = std::make_unique<ModelRenderer>();
    auto ground = std::make_unique<ModelRenderer>();
    ShadowMap shadow;
    PostChain post;
    TextOverlay text;
    const auto cleanup = [&]() {
        text.shutdown();
        post.shutdown();
        shadow.shutdown();
        ground->shutdown();
        renderer->shutdown();
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
    };
    if (!renderer->init(asset, error) || !ground->init(make_model_ground(asset), error)
        || !shadow.init(1024, error) || !init_post_chain(post, options, error)
        || !text.init(kTextPx, error) || !test_keyboard().configure(options.test_input_file, error)) {
        std::fprintf(stderr, "[model-view] %s\n", error.c_str());
        cleanup();
        return 1;
    }
    WearParams preview_wear;
    if(!options.wear.empty()) parse_wear_amount(options.wear,preview_wear.amount);
    if(!options.wear_materials.empty()) parse_wear_materials(options.wear_materials,preview_wear.materials);
    renderer->set_wear(preview_wear);
    std::fprintf(stderr, "[model-view] loaded vertices=%zu primitives=%zu\n",
        asset.vertex_count, asset.primitives.size());
    float azimuth = 0.9f;
    float elevation = 0.55f;
    float zoom = 1.f;
    std::string status = "Ready";
    bool running = true;
    int result = 0;
    int frames = 0;
    while (running) {
        test_keyboard().poll("model-view");
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_DROPFILE) { SDL_free(event.drop.file); continue; }
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_MOUSEWHEEL) zoom *= std::pow(0.88f, static_cast<float>(event.wheel.y));
            if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK)) {
                azimuth += static_cast<float>(event.motion.xrel) * 0.01f;
                elevation += static_cast<float>(event.motion.yrel) * 0.01f;
            }
            if (event.type == SDL_FINGERMOTION) {
                azimuth += event.tfinger.dx * 5.f;
                elevation += event.tfinger.dy * 3.f;
            }
            if (event.type != SDL_KEYDOWN) continue;
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_LEFT: azimuth -= (event.key.keysym.mod & KMOD_SHIFT) ? 1.57079633f : 0.15f; break;
            case SDLK_RIGHT: azimuth += (event.key.keysym.mod & KMOD_SHIFT) ? 1.57079633f : 0.15f; break;
            case SDLK_UP: elevation += 0.1f; break;
            case SDLK_DOWN: elevation -= 0.1f; break;
            case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: zoom *= 0.88f; break;
            case SDLK_MINUS: case SDLK_KP_MINUS: zoom /= 0.88f; break;
            case SDLK_HOME: azimuth = 0.9f; elevation = 0.55f; zoom = 1.f; break;
            case SDLK_r: {
                ModelAsset candidate;
                auto next = std::make_unique<ModelRenderer>();
                auto next_ground = std::make_unique<ModelRenderer>();
                next->set_wear(preview_wear);
                if (load_glb(options.model_view_path, candidate, error)
                    && next->init(candidate, error) && next_ground->init(make_model_ground(candidate), error)) {
                    renderer->shutdown();
                    ground->shutdown();
                    renderer.swap(next);
                    ground.swap(next_ground);
                    asset = std::move(candidate);
                    status = "Reloaded";
                    std::fprintf(stderr, "[model-view] reloaded vertices=%zu\n", asset.vertex_count);
                } else {
                    status = "Reload failed; previous model retained";
                    std::fprintf(stderr, "[model-view] reload failed: %s\n", error.c_str());
                }
                next->shutdown();
                next_ground->shutdown();
                break;
            }
            default: break;
            }
        }
        if (!running) break;
        elevation = std::clamp(elevation, 0.06f, 1.48f);
        zoom = std::clamp(zoom, 0.2f, 12.f);
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(window.window, &width, &height);
        if (width <= 0 || height <= 0) { SDL_Delay(16); continue; }
        if (!post.resize(width, height, error)) {
            std::fprintf(stderr, "[model-view] %s\n", error.c_str());
            result = 1;
            break;
        }
        const Vec3 center = (asset.bounds_min + asset.bounds_max) * 0.5f;
        const Vec3 size = asset.bounds_max - asset.bounds_min;
        const float span = std::max({size.x, size.y, size.z, 0.25f});
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float distance = span * std::max(2.4f, 1.6f * aspect) * zoom;
        const Vec3 eye = center + Vec3{std::cos(azimuth) * std::cos(elevation),
            std::sin(azimuth) * std::cos(elevation), std::sin(elevation)} * distance;
        const float near_plane = std::max(0.001f, span * 0.01f);
        const float far_plane = distance + span * 12.f;
        const Mat4 vp = perspective_horizontal(0.85f, aspect, near_plane, far_plane)
            * look_at(eye, center, Vec3{0,0,1});
        SceneLighting lighting;
        lighting.sun_dir = normalize(Vec3{0.5f,-0.35f,0.79f});
        if (options.model_test_ambient >= 0.f) {
            lighting.key_color={0,0,0}; lighting.points.clear();
            lighting.sky_color={1,1,1}; lighting.bounce_color={1,1,1};
            lighting.ambient_scale=options.model_test_ambient; lighting.shadow_strength=0;
        }
        const ShadowFit fit = fit_shadow_bounds(asset.bounds_min.x - span, asset.bounds_max.x + span,
            asset.bounds_min.y - span, asset.bounds_max.y + span,
            std::max(asset.bounds_max.z, span), lighting.sun_dir, shadow.side());
        const Mat4 instance = Mat4::identity();
        shadow.begin();
        renderer->draw_depth(fit.view_projection, instance);
        shadow.end(width, height);
        post.begin_scene(0.055f, 0.065f, 0.085f);
        ground->draw(vp, instance, lighting, eye, fit.view_projection, shadow.texture(), shadow.side());
        renderer->draw(vp, instance, lighting, eye, fit.view_projection, shadow.texture(), shadow.side());
        post.end_scene(width, height);
        post.resolve(resolve_post_flags(options), PostParams{}, near_plane, far_plane, distance, width, height);
        text.begin(width, height);
        const TextColor label{0.86f,0.88f,0.92f,1.f};
        char info[240];
        std::snprintf(info, sizeof(info), "3D model: %zu triangles, %.2f x %.2f x %.2f cells | %s",
            asset.vertex_count / 3, size.x, size.y, size.z, status.c_str());
        text.draw_cell(12, 12, 0, 0, info, label);
        text.draw_cell(12, 12, 0, 1, "Drag/arrows: rotate | wheel/+/-: zoom | R: reload | Home: reset | Esc: close", label);
        text.flush();
        const auto gl_error = gl::glGetError();
        if (gl_error != gl::GL_NO_ERROR) {
            std::fprintf(stderr, "[model-view] GL error %u\n", static_cast<unsigned>(gl_error));
            result = 1;
            break;
        }
        if (!options.shot_path.empty() && ++frames >= 3) {
            if (!save_framebuffer_bmp(options.shot_path, width, height)) result = 1;
            break;
        }
        SDL_GL_SwapWindow(window.window);
        SDL_Delay(8);
    }
    cleanup();
    return result;
}
}
