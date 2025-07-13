
#ifdef USE_OPENGL
#include <glad/glad.h>

#include "imgui_impl_opengl3.h"
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

#include "File.hpp"
#include "GameTimer.hpp"

#ifdef __APPLE__
#ifdef __arm__
#define _XM_ARM_NEON_INTRINSICS_ // Apple architecture
#endif
#endif

#define PAL_STDCPP_COMPAT
#include <DirectXCollision.h>
#include <DirectXColors.h>
#include <DirectXMath.h>

#include "SimpleMath.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

#ifdef USE_METAL
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#endif

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"

static const char* vertex_shader_text
    = "#version 410\n"
      "layout(location = 0) in vec3 vNorm;\n"
      "layout(location = 1) in vec3 vPos;\n"
      "layout(location = 2) in uvec4 vJoints;\n"
      "layout(location = 3) in vec4 vWeights;\n"

      "uniform mat4 world_txfm;\n"
      "uniform mat4 viewport_txfm;\n"
      "uniform uint preview_joint = 2;\n"
      "uniform mat4 inverse_bone_matrix[20];\n"
      "uniform mat4 bone_matrix[20];\n"
      "out vec3 norm;\n"
      "out float joint_color;\n"
      "void main()\n"
      "{\n"
      "    gl_Position = vec4(0.0);\n"
      "    joint_color = 0.0;\n"
      "    for (int i = 0; i < 4; ++i) { \n"
      "        gl_Position += vWeights[i] * (viewport_txfm * world_txfm * "
      "bone_matrix[vJoints[i]] * inverse_bone_matrix[vJoints[i]] * vec4(vPos, "
      "1.0));\n"
      "    }\n"
      "    norm = mat3(world_txfm) * vNorm;\n"
      "}\n";

static const char* fragment_shader_text
    = "#version 410\n"
      "in vec3 norm;\n"
      "in float joint_color;\n"
      "out vec4 fragment;\n"
      "void main()\n"
      "{\n"
      "    vec3 sun_dir = normalize(vec3(0.0, -1.0, -1.0))\n;"
      "    float diffuse = max(dot(norm, -sun_dir), 0.0);\n"
      "    float ambient = 0.1;\n"
      "    fragment = vec4((ambient + diffuse) * vec3(1.0, 1.0, 1.0), 1.0);\n"
      "    //fragment = vec4(vec3(joint_color), 1.0);\n"
      "}\n";

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

static float diff_time(struct timespec last, struct timespec now)
{
    float ret = now.tv_sec - last.tv_sec;
    ret += (float)(now.tv_nsec - last.tv_nsec) / 1e9;
    return ret;
}

struct animation_channel
{
    uint32_t                  target;
    cgltf_animation_path_type path_type;
    std::vector<float>        times;
    std::vector<float>        data;
};

struct animation
{
    std::string                    name;
    std::vector<animation_channel> channels;
};

int animation_channel_components(cgltf_animation_path_type path_type)
{
    switch (path_type)
    {
    case cgltf_animation_path_type_translation:
    case cgltf_animation_path_type_scale:
        return 3;
    case cgltf_animation_path_type_rotation:
        return 4;
    }
}

struct model
{
    cgltf_data*            model_data;
    size_t                 num_indices;
    size_t                 num_nodes;
    std::vector<animation> animations;
    cgltf_node**           joints;
    uint32_t*              joint_ids;
    std::vector<Matrix>    joint_inverse_mats;

#ifdef USE_OPENGL
    GLuint vao;
#endif
};

constexpr int ComponentCount(cgltf_type type)
{
    switch (type)
    {
    case cgltf_type_scalar:
        return 1;
    case cgltf_type_vec2:
        return 2;
    case cgltf_type_vec3:
        return 3;
    case cgltf_type_vec4:
    case cgltf_type_mat2:
        return 4;
    case cgltf_type_mat3:
        return 9;
    case cgltf_type_mat4:
        return 16;
    case cgltf_type_invalid:
    case cgltf_type_max_enum:
        break;
    }

    return -1;
}

template <typename T>
void LoadAttribute(const cgltf_accessor* accessor, T* dest, int numComponents)
{
    cgltf_size n = 0;
    T* buffer = (T*)accessor->buffer_view->buffer->data + accessor->buffer_view->offset / sizeof(T)
        + accessor->offset / sizeof(T);

    for (unsigned int k = 0; k < accessor->count; ++k)
    {
        for (int j = 0; j < numComponents; ++j)
        {
            dest[numComponents * k + j] = buffer[n + j];
        }
        n += accessor->stride / sizeof(T);
    }
}

void calculate_vertex_info(const cgltf_primitive* primitive, int* numVertices, int* numIndices)
{
    *numIndices += primitive->indices->count;

    // NOTE: this assumes the same number of vertices for all attributes
    for (int32_t j = 0; j < primitive->attributes_count; j++)
    {
        const cgltf_attribute* attribute = &primitive->attributes[j];
        if (attribute->type == cgltf_attribute_type_position)
        {
            *numVertices += attribute->data->count;
        }
    }
}

struct model load_model()
{
    std::filesystem::path path = std::filesystem::path(SDL_GetBasePath()) / "alien-bug.glb";

    cgltf_data*   data = nullptr;
    cgltf_options options = {};
    cgltf_result  result = cgltf_parse_file(&options, path.string().c_str(), &data);
    if (result != cgltf_result_success)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load GLTF file");
        abort();
    }

    if (cgltf_load_buffers(&options, data, path.string().c_str()) != cgltf_result_success)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load GLTF buffers");
        abort();
    }

    int                    numVertices = 0;
    int                    numIndices = 0;
    const cgltf_primitive* prim = &data->meshes[0].primitives[0];
    calculate_vertex_info(prim, &numVertices, &numIndices);

    float*    positions = new float[3 * numVertices];
    float*    normals = new float[3 * numVertices];
    uint8_t*  joints = new uint8_t[4 * numVertices];
    float*    weights = new float[4 * numVertices];
    uint16_t* indices = new uint16_t[numIndices];

    const cgltf_accessor* accessor = nullptr;
    for (int32_t attr = 0; attr < prim->attributes_count; attr++)
    {
        const cgltf_attribute* attribute = &prim->attributes[attr];
        accessor = attribute->data;
        if (attribute->type == cgltf_attribute_type_position)
        {
            assert(accessor->type == cgltf_type_vec3
                && accessor->component_type == cgltf_component_type_r_32f);
            LoadAttribute(accessor, positions, 3);
        }
        else if (attribute->type == cgltf_attribute_type_normal)
        {
            assert(accessor->type == cgltf_type_vec3
                && accessor->component_type == cgltf_component_type_r_32f);
            LoadAttribute(accessor, normals, 3);
        }
        else if (attribute->type == cgltf_attribute_type_joints)
        {
            assert(accessor->type == cgltf_type_vec4
                && accessor->component_type == cgltf_component_type_r_8u);
            LoadAttribute(accessor, joints, 4);
        }
        else if (attribute->type == cgltf_attribute_type_weights)
        {
            assert(accessor->type == cgltf_type_vec4
                && accessor->component_type == cgltf_component_type_r_32f);
            LoadAttribute(accessor, weights, 4);
        }
    }

    if (prim->indices != nullptr)
    {
        accessor = prim->indices;
        LoadAttribute(accessor, &indices[0], 1);
    }

    const cgltf_skin*   skin = &data->skins[0];
    std::vector<Matrix> inverse_bind_matrices(skin->joints_count);

    for (uint32_t i = 0; i < skin->joints_count; ++i)
    {
        printf("joint %d: %s\n", i, skin->joints[i]->name);
        cgltf_node* node = skin->joints[i];
        skin->inverse_bind_matrices[i];
    }

#ifdef USE_OPENGL
    GLuint vertex_array;
    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);

    GLuint position_buffer;
    glGenBuffers(1, &position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
    glBufferData(GL_ARRAY_BUFFER, numVertices * sizeof(float) * 3, positions, GL_STATIC_DRAW);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);

    GLuint normals_buffer;
    glGenBuffers(1, &normals_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, normals_buffer);
    glBufferData(GL_ARRAY_BUFFER, numVertices * sizeof(float) * 3, normals, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);

    GLuint joints_buffer;
    glGenBuffers(1, &joints_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, joints_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(uint8_t) * 4 * numVertices, joints, GL_STATIC_DRAW);

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, 4, 0);

    GLuint weights_buffer;
    glGenBuffers(1, &weights_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, weights_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * numVertices, weights, GL_STATIC_DRAW);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * numIndices, indices, GL_STATIC_DRAW);

    glBindVertexArray(0);
#endif

    std::vector<animation> animations(data->animations_count);
    for (uint32_t i = 0; i < data->animations_count; ++i)
    {
        const cgltf_animation* animation = &data->animations[i];
        animations[i].name = animation->name ? animation->name : "Unnamed Animation";

        animations[i].channels.resize(animation->channels_count);
        for (uint32_t j = 0; j < animation->channels_count; ++j)
        {
            const cgltf_animation_channel* channel = &animation->channels[j];

            uint32_t                  target_node = (ptrdiff_t)(channel->target_node - data->nodes);
            cgltf_animation_path_type target_path = channel->target_path;

            cgltf_size         num_timesteps = channel->sampler->input->count;
            std::vector<float> times(channel->sampler->input->count);
            LoadAttribute(channel->sampler->input, times.data(), 1);

            int components = 3;
            if (target_path == cgltf_animation_path_type_rotation)
            {
                components = 4;
            }

            std::vector<float> data(num_timesteps * components);
            LoadAttribute(channel->sampler->output, data.data(), components);

            animations[i].channels[j] = animation_channel {
                target_node,
                target_path,
                times,
                data,
            };
        }
    }

    uint32_t* joint_ids = new uint32_t[skin->joints_count];
    for (int i = 0; i < skin->joints_count; ++i)
    {
        cgltf_node* joint = skin->joints[i];
        uint32_t    joint_id = (ptrdiff_t)(joint - data->nodes);
        joint_ids[i] = joint_id;
    }

    float* inverse_bind_matrices_buf = new float[skin->joints_count * 16];
    LoadAttribute(skin->inverse_bind_matrices, inverse_bind_matrices_buf, 16);

    for (int i = 0; i < skin->joints_count; ++i)
    {
        inverse_bind_matrices[i] = Matrix(inverse_bind_matrices_buf + (i * 16));
    }

    struct model model = {};
    model.model_data = data;
    model.num_indices = (size_t)numIndices;
    model.num_nodes = (size_t)data->nodes_count;
    model.animations = animations;
    model.joints = skin->joints;
    model.joint_ids = joint_ids;
    model.joint_inverse_mats = inverse_bind_matrices;
#ifdef USE_OPENGL
    model.vao = vertex_array;
#endif

    delete[] positions;
    delete[] normals;
    delete[] joints;
    delete[] weights;
    delete[] indices;
    delete[] inverse_bind_matrices_buf;

    return model;
}

#ifdef USE_OPENGL
GLuint compile_shader(const char* shader_text, GLenum shader_type)
{
    const GLuint vertex_shader = glCreateShader(shader_type);
    glShaderSource(vertex_shader, 1, &shader_text, NULL);
    glCompileShader(vertex_shader);

    int status;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);

    if (!status)
    {
        char buf[4096];

        GLsizei len;
        glGetShaderInfoLog(vertex_shader, 4096, &len, buf);
        printf("%*s\n", len, buf);
    }
    return vertex_shader;
}

GLuint make_bone(void)
{
    GLuint vertex_array;
    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);

    float verts[6] = {
        0.0,
        0.0,
        0.0,
        0.0,
        0.1,
        0.0,
    };

    GLuint position_buffer;
    glGenBuffers(1, &position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);

    return vertex_array;
}
#endif

Matrix node_world_txfm(cgltf_node* node)
{
    Vector3 scale = Vector3(node->scale[0], node->scale[1], node->scale[2]);
    Vector3 translation = Vector3(node->translation[0], node->translation[1], node->translation[2]);
    Quaternion rotation
        = Quaternion(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);

    Matrix node_txfm = Matrix::CreateScale(scale);
    node_txfm *= Matrix::CreateFromQuaternion(rotation);
    node_txfm *= Matrix::CreateTranslation(translation);

    if (node->parent != nullptr)
    {
        node_txfm *= node_world_txfm(node->parent);
    }

    return node_txfm;
}

float lerp(float a, float b, float t)
{
    return a * (1.0 - t) + b * t;
}

void apply_animation(float time_since_start, struct model* model, int index)
{
    assert(index < model->animations.size());

    animation* animation = &model->animations[index];

    for (int channel_idx = 0; channel_idx < animation->channels.size(); ++channel_idx)
    {
        struct animation_channel* channel = &animation->channels[channel_idx];
        float                     rel_time_since_start
            = fmod(time_since_start, channel->times[channel->times.size() - 1]);

        int last_timestep = 0;
        for (int i = 0; i < channel->times.size(); ++i)
        {
            if (rel_time_since_start < channel->times[i])
                break;
            last_timestep = i;
        }

        int next_timestep = (last_timestep + 1) % channel->times.size();

        float last_time = channel->times[last_timestep];
        float next_time = channel->times[next_timestep];

        float t = (rel_time_since_start - last_time) / (next_time - last_time);

        int    components = animation_channel_components(channel->path_type);
        float* last_data = &channel->data[last_timestep * components];
        float* next_data = &channel->data[next_timestep * components];

        float out[4];
        for (int i = 0; i < components; ++i)
        {
            out[i] = lerp(last_data[i], next_data[i], t);
        }

        cgltf_node* nodes = model->model_data->nodes;
        switch (channel->path_type)
        {
        case cgltf_animation_path_type_translation:
            nodes[channel->target].translation[0] = out[0];
            nodes[channel->target].translation[1] = out[1];
            nodes[channel->target].translation[2] = out[2];
            break;
        case cgltf_animation_path_type_rotation:
            nodes[channel->target].rotation[0] = out[0];
            nodes[channel->target].rotation[1] = out[1];
            nodes[channel->target].rotation[2] = out[2];
            nodes[channel->target].rotation[3] = out[3];
            break;
        case cgltf_animation_path_type_scale:
            nodes[channel->target].scale[0] = out[0];
            nodes[channel->target].scale[1] = out[1];
            nodes[channel->target].scale[2] = out[2];
            break;
        default:
            break;
        }
    }
}

struct SDLWindowDeleter
{
    void operator()(SDL_Window* window) const
    {
        if (window)
        {
            SDL_DestroyWindow(window);
        }
        window = nullptr;
    }
};
using SDLWindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;

static constexpr int BUFFER_COUNT = 3;
static constexpr int MULTISAMPLE_COUNT = 4;

#ifndef USE_METAL
struct App
#else
struct App : public CA::MetalDisplayLinkDelegate
#endif
{
    SDLWindowPtr window;
    model        model;
    GameTimer    timer;
    float        angle;
    int          animation_idx = 0;

    void tick();
    void render();

#ifdef USE_METAL
    NS::SharedPtr<MTL::Device>            mtlDevice;
    NS::SharedPtr<MTL::CommandQueue>      mtlCommandQueue;
    NS::SharedPtr<CA::MetalDisplayLink>   mtlDisplayLink;
    NS::SharedPtr<MTL::Texture>           mtlMsaaTexture;
    NS::SharedPtr<MTL::Texture>           mtlDepthStencilTexture;
    NS::SharedPtr<MTL::DepthStencilState> mtlDepthStencilState;
    NS::SharedPtr<MTL::Library>           mtlPipelineLibrary;
    MTL::PixelFormat                      mtlFrameBufferPixelFormat;
    uint32_t                              frameIndex = 0;
    dispatch_semaphore_t                  frameSemaphore;

    void metalDisplayLinkNeedsUpdate(
        CA::MetalDisplayLink* displayLink, CA::MetalDisplayLinkUpdate* update) override;

    void createDepthStencil();

    void render_metal(MTL::RenderCommandEncoder* encoder);
#endif

#ifdef USE_OPENGL
    SDL_GLContext context;
    GLuint        bone_vao;
    GLuint        vertex_shader;
    GLuint        fragment_shader;
    GLuint        program;

    void render_opengl();
#endif
};

void App::tick()
{
}

#ifdef USE_OPENGL
void App::render_opengl()
{
    int width, height;
    SDL_GetWindowSizeInPixels(window.get(), &width, &height);
    const float ratio = width / (float)height;

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(program);
    glBindVertexArray(model.vao);

    Matrix world_txfm = Matrix::CreateTranslation(0, -0.0, 0.0);
    world_txfm *= Matrix::CreateRotationY(DirectX::XMConvertToRadians(45.0f));
    world_txfm *= Matrix::CreateTranslation(0.0, 0.0, -1.0);
    world_txfm = world_txfm.Transpose();

    Matrix viewport_txfm
        = Matrix::CreatePerspectiveFieldOfView(XMConvertToRadians(90.0f), ratio, 0.1f, 10.0f);
    viewport_txfm = viewport_txfm.Transpose();

    Matrix bone_matrices[72];
    Matrix inverse_bone_matrices[72];
    for (int i = 0; i < model.joint_inverse_mats.size(); i++)
    {
        inverse_bone_matrices[i] = model.joint_inverse_mats[i];
        inverse_bone_matrices[i] = inverse_bone_matrices[i].Transpose();

        bone_matrices[i] = node_world_txfm(&model.model_data->nodes[model.joint_ids[i]]);
        bone_matrices[i] = bone_matrices[i].Transpose();
    }

    GLuint world_txfm_loc = glGetUniformLocation(program, "world_txfm");
    GLuint viewport_txfm_loc = glGetUniformLocation(program, "viewport_txfm");
    GLuint preview_joint_loc = glGetUniformLocation(program, "preview_joint");
    GLuint inverse_bone_matrix_loc = glGetUniformLocation(program, "inverse_bone_matrix");
    GLuint bone_matrix_loc = glGetUniformLocation(program, "bone_matrix");

    glUniformMatrix4fv(world_txfm_loc, 1, true, reinterpret_cast<float*>(&world_txfm));
    glUniformMatrix4fv(viewport_txfm_loc, 1, true, reinterpret_cast<float*>(&viewport_txfm));
    glUniformMatrix4fv(
        inverse_bone_matrix_loc, 20, true, reinterpret_cast<float*>(&inverse_bone_matrices));
    glUniformMatrix4fv(bone_matrix_loc, 20, true, reinterpret_cast<float*>(&bone_matrices));

    glDrawElements(GL_TRIANGLES, model.num_indices, GL_UNSIGNED_SHORT, 0);

    // ImGui rendering
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0);
    ImGui::SetNextWindowPos(ImVec2(10, 20));
    // ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin(
        "Animation Example", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
    ImGui::Text("%s (%.1d fps)", SDL_GetWindowTitle(window.get()), timer.framesPerSecond());
    // std::vector<std::string> animations = m_asset->animations();

    if (ImGui::Combo(
            "Animation", &animation_idx,
            [](void* data, int index) -> const char* {
                animation* animations = static_cast<animation*>(data);
                return animations[index].name.c_str();
            },
            model.animations.data(), model.animations.size()))
    {
    }
    ImGui::PopStyleVar();

    ImGui::End();

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window.get());
}
#endif

#ifdef USE_METAL

void App::render_metal(MTL::RenderCommandEncoder* encoder)
{
}

void App::createDepthStencil()
{
    int32_t frameWidth = 0;
    int32_t frameHeight = 0;
    SDL_GetWindowSizeInPixels(window.get(), &frameWidth, &frameHeight);

    // Create a multisample texture
    MTL::TextureDescriptor* msaaTextureDescriptor = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatBGRA8Unorm_sRGB, frameWidth, frameHeight, false);
    msaaTextureDescriptor->setTextureType(MTL::TextureType2DMultisample);
    msaaTextureDescriptor->setSampleCount(MULTISAMPLE_COUNT); // Set sample count for MSAA
    msaaTextureDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    msaaTextureDescriptor->setStorageMode(MTL::StorageModePrivate);

    mtlMsaaTexture = NS::TransferPtr(mtlDevice->newTexture(msaaTextureDescriptor));
    msaaTextureDescriptor->release();

    mtlDepthStencilState.reset();

    MTL::DepthStencilDescriptor* depthStencilDescriptor
        = MTL::DepthStencilDescriptor::alloc()->init();
    depthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunctionLess);
    depthStencilDescriptor->setDepthWriteEnabled(true);

    mtlDepthStencilState = NS::TransferPtr(mtlDevice->newDepthStencilState(depthStencilDescriptor));

    depthStencilDescriptor->release();

    MTL::TextureDescriptor* textureDescriptor = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatDepth32Float_Stencil8, frameWidth, frameHeight, false);
    textureDescriptor->setTextureType(MTL::TextureType2DMultisample);
    textureDescriptor->setSampleCount(MULTISAMPLE_COUNT);
    textureDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    textureDescriptor->setResourceOptions(
        MTL::ResourceOptionCPUCacheModeDefault | MTL::ResourceStorageModePrivate);
    textureDescriptor->setStorageMode(MTL::StorageModeMemoryless);

    mtlDepthStencilTexture = NS::TransferPtr(mtlDevice->newTexture(textureDescriptor));

    textureDescriptor->release();
}

void App::metalDisplayLinkNeedsUpdate(
    CA::MetalDisplayLink* displayLink, CA::MetalDisplayLinkUpdate* update)
{
    timer.tick([this]() { tick(); });

    frameIndex = (frameIndex + 1) % BUFFER_COUNT;

    MTL::CommandBuffer* commandBuffer = mtlCommandQueue->commandBuffer();

    dispatch_semaphore_wait(frameSemaphore, DISPATCH_TIME_FOREVER);
    commandBuffer->addCompletedHandler(
        [this](MTL::CommandBuffer* /*buffer*/) { dispatch_semaphore_signal(frameSemaphore); });

    CA::MetalDrawable* drawable = update->drawable();
    if (drawable != nullptr)
    {
        // Update depth stencil texture if necessary¬
        if (drawable->texture()->width() != mtlDepthStencilTexture->width()
            || drawable->texture()->height() != mtlDepthStencilTexture->height())
        {
            int32_t frameWidth = 0;
            int32_t frameHeight = 0;
            SDL_GetWindowSizeInPixels(window.get(), &frameWidth, &frameHeight);

            mtlMsaaTexture.reset();

            // Create a multisample texture
            MTL::TextureDescriptor* msaaTextureDescriptor
                = MTL::TextureDescriptor::texture2DDescriptor(
                    MTL::PixelFormatBGRA8Unorm_sRGB, frameWidth, frameHeight, false);
            msaaTextureDescriptor->setTextureType(MTL::TextureType2DMultisample);
            msaaTextureDescriptor->setSampleCount(MULTISAMPLE_COUNT); // Set sample count for MSAA
            msaaTextureDescriptor->setUsage(MTL::TextureUsageRenderTarget);
            msaaTextureDescriptor->setStorageMode(MTL::StorageModePrivate);

            mtlMsaaTexture = NS::TransferPtr(mtlDevice->newTexture(msaaTextureDescriptor));

            mtlDepthStencilTexture.reset();

            MTL::TextureDescriptor* textureDescriptor = MTL::TextureDescriptor::texture2DDescriptor(

                MTL::PixelFormatDepth32Float_Stencil8, frameWidth, frameHeight, false);
            textureDescriptor->setSampleCount(MULTISAMPLE_COUNT);
            textureDescriptor->setTextureType(MTL::TextureType2DMultisample);
            textureDescriptor->setUsage(MTL::TextureUsageRenderTarget);
            textureDescriptor->setResourceOptions(
                MTL::ResourceOptionCPUCacheModeDefault | MTL::ResourceStorageModePrivate);
            textureDescriptor->setStorageMode(MTL::StorageModeMemoryless);

            mtlDepthStencilTexture = NS::TransferPtr(mtlDevice->newTexture(textureDescriptor));
        }

        MTL::RenderPassDescriptor* passDescriptor
            = MTL::RenderPassDescriptor::renderPassDescriptor();
        passDescriptor->colorAttachments()->object(0)->setResolveTexture(drawable->texture());
        passDescriptor->colorAttachments()->object(0)->setTexture(mtlMsaaTexture.get());
        passDescriptor->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        passDescriptor->colorAttachments()->object(0)->setStoreAction(
            MTL::StoreActionMultisampleResolve);
        passDescriptor->colorAttachments()->object(0)->setClearColor(
            MTL::ClearColor(.39, .58, .92, 1.0));
        passDescriptor->depthAttachment()->setTexture(mtlDepthStencilTexture.get());
        passDescriptor->depthAttachment()->setLoadAction(MTL::LoadActionClear);
        passDescriptor->depthAttachment()->setStoreAction(MTL::StoreActionDontCare);
        passDescriptor->depthAttachment()->setClearDepth(1.0);
        passDescriptor->stencilAttachment()->setTexture(mtlDepthStencilTexture.get());
        passDescriptor->stencilAttachment()->setLoadAction(MTL::LoadActionClear);
        passDescriptor->stencilAttachment()->setStoreAction(MTL::StoreActionDontCare);
        passDescriptor->stencilAttachment()->setClearStencil(0);

        MTL::RenderCommandEncoder* commandEncoder
            = commandBuffer->renderCommandEncoder(passDescriptor);

        commandEncoder->pushDebugGroup(MTLSTR("SAMPLE RENDERING"));

        render_metal(commandEncoder);

        commandEncoder->popDebugGroup();

        //        // ImGui rendering
        //        ImGui_ImplMetal_NewFrame(passDescriptor);
        //        ImGui_ImplSDL3_NewFrame();
        //        ImGui::NewFrame();
        //
        //        onSetupUi(m_timer);
        //
        //        commandEncoder->pushDebugGroup(MTLSTR("IMGUI RENDERING"));
        //
        //        // Rendering
        //        ImGui::Render();
        //        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer,
        //        commandEncoder);

        commandEncoder->popDebugGroup();

        commandEncoder->endEncoding();

        commandBuffer->presentDrawable(drawable);
        commandBuffer->commit();
    }
}

#endif

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.IniSavingRate = 0.0F;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    ImGui::StyleColorsDark();

    // SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "waitevent");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SDL: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int flags = SDL_WINDOW_RESIZABLE;
#ifdef USE_OPENGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    flags |= SDL_WINDOW_OPENGL;
#else
    flags |= SDL_WINDOW_METAL;
#endif
    App* app = new App;
    app->window.reset(SDL_CreateWindow("OpenGL Triangle", 800, 600, flags));
    if (!app->window)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

#ifdef USE_OPENGL
    app->context = SDL_GL_CreateContext(app->window.get());
    if (!app->context)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION, "Failed to create OpenGL context: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init GLAD");
        return SDL_APP_FAILURE;
    }
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    SDL_GL_MakeCurrent(app->window.get(), app->context);
    SDL_GL_SwapWindow(app->window.get());

    ImGui_ImplOpenGL3_Init();
    ImGui_ImplSDL3_InitForOpenGL(app->window.get(), app->context);
#else
    SDL_MetalView mtlView = SDL_Metal_CreateView(app->window.get());

    app->mtlDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());

    auto* layer = static_cast<CA::MetalLayer*>((SDL_Metal_GetLayer(mtlView)));
    app->mtlFrameBufferPixelFormat = MTL::PixelFormatBGRA8Unorm_sRGB;
    layer->setPixelFormat(app->mtlFrameBufferPixelFormat);
    layer->setDevice(app->mtlDevice.get());

    app->mtlCommandQueue = NS::TransferPtr(app->mtlDevice->newCommandQueue());

    app->createDepthStencil();

    // Load Pipeline Library
    // TODO: Showcase how to use Metal archives to erase compilation
    app->mtlPipelineLibrary = NS::TransferPtr(app->mtlDevice->newDefaultLibrary());

    app->frameSemaphore = dispatch_semaphore_create(BUFFER_COUNT);

    app->mtlDisplayLink = NS::TransferPtr(CA::MetalDisplayLink::alloc()->init(layer));
    // Enable 120HZ refresh for devices that support Pro Motion
    //    app->mtlDisplayLink->setPreferredFrameRateRange({ 60, mode->refresh_rate,
    //    mode->refresh_rate });
    app->mtlDisplayLink->setDelegate(app);

#endif

    app->model = load_model();

#ifdef USE_OPENGL
    app->vertex_shader = compile_shader(vertex_shader_text, GL_VERTEX_SHADER);
    app->fragment_shader = compile_shader(fragment_shader_text, GL_FRAGMENT_SHADER);

    app->program = glCreateProgram();
    glAttachShader(app->program, app->vertex_shader);
    glAttachShader(app->program, app->fragment_shader);
    glLinkProgram(app->program);

    app->angle = 0;

    app->bone_vao = make_bone();

    glEnable(GL_DEPTH_TEST);

    glLineWidth(5);
#else

#endif

    app->timer.setFixedTimeStep(false);
    app->timer.resetElapsedTime();

    *appstate = app;

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    App* app = static_cast<App*>(appstate);
    if (app)
    {
        app->timer.tick([&] {
            float elapsed = app->timer.elapsedSeconds();

            static float accum = 0.0f;
            accum += elapsed;
            apply_animation(accum, &app->model, app->animation_idx);

            app->angle += 2.0f * DirectX::XM_PI * elapsed * 0.5;
            app->angle = fmod(app->angle, 2 * DirectX::XM_PI);

#ifdef USE_OPENGL
            app->render_opengl();
#endif
        });
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    App* app = static_cast<App*>(appstate);
    switch (event->type)
    {
    case SDL_EVENT_TERMINATING:
    case SDL_EVENT_QUIT: {
        // game->quit();
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        // game->onKeyEvent(event);
        break;
    }

    default:
        break;
    }

    ImGui_ImplSDL3_ProcessEvent(event);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    App* app = static_cast<App*>(appstate);
    delete app;
}
