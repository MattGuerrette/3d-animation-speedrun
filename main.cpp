
#include <glad/glad.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>

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

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

typedef struct Vertex
{
    float pos[3];
    float col[3];
} Vertex;

struct gltf_node
{
    Vector3    translation;
    Vector3    scale;
    Quaternion rotation;
    int        idx;
    gltf_node* parent;
    int        parentIdx;
};

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

struct node
{
    float    translation[3];
    float    rotation[4];
    float    scale[3];
    uint32_t parent;
};

enum animation_type : uint32_t
{
    animation_type_translation = 0,
    animation_type_rotation = 1,
    animation_type_scale = 2,
};

struct animation
{
    std::string               name;
    struct animation_channel* channels;
    size_t                    num_channels;
};

struct animation_channel
{
    uint32_t                  target;
    cgltf_animation_path_type path_type;
    size_t                    num_timesteps;
    float*                    times;
    float*                    data;
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
    cgltf_data*  model_data;
    GLuint       vao;
    size_t       num_indices;
    size_t       num_nodes;
    animation*   animations;
    size_t       num_animations;
    cgltf_node** joints;
    uint32_t*    joint_ids;
    Matrix*      joint_inverse_mats;
    size_t       num_joints;
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

gltf_node* build_node(cgltf_data* data, cgltf_node* node)
{
    gltf_node* g_node = new gltf_node;
    g_node->translation = Vector3(node->translation[0], node->translation[1], node->translation[2]);
    g_node->scale = Vector3(node->scale[0], node->scale[1], node->scale[2]);
    g_node->rotation
        = Quaternion(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);
    g_node->idx = (ptrdiff_t)(node - data->nodes);

    if (node->parent != nullptr)
    {
        g_node->parent = build_node(data, node->parent);
        g_node->parentIdx = (ptrdiff_t)(node->parent - data->nodes);
    }

    return g_node;
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

    const cgltf_skin* skin = &data->skins[0];
    Matrix*           inverse_bind_matrices = new Matrix[skin->joints_count];

    for (uint32_t i = 0; i < skin->joints_count; ++i)
    {
        printf("joint %d: %s\n", i, skin->joints[i]->name);
        cgltf_node* node = skin->joints[i];
        skin->inverse_bind_matrices[i];
    }

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

    animation* animations = new animation[data->animations_count];
    for (uint32_t i = 0; i < data->animations_count; ++i)
    {
        const cgltf_animation* animation = &data->animations[i];
        animations[i].name = animation->name ? animation->name : "Unnamed Animation";

        animations[i].channels = (animation_channel*)malloc(
            animation->channels_count * sizeof(struct animation_channel));
        animations[i].num_channels = animation->channels_count;
        for (uint32_t j = 0; j < animation->channels_count; ++j)
        {
            const cgltf_animation_channel* channel = &animation->channels[j];

            uint32_t                  target_node = (ptrdiff_t)(channel->target_node - data->nodes);
            cgltf_animation_path_type target_path = channel->target_path;

            cgltf_size num_timesteps = channel->sampler->input->count;
            float*     times = new float[channel->sampler->input->count];
            LoadAttribute(channel->sampler->input, times, 1);

            int components = 3;
            if (target_path == cgltf_animation_path_type_rotation)
            {
                components = 4;
            }

            float* data = new float[num_timesteps * components];
            LoadAttribute(channel->sampler->output, data, components);

            animations[i].channels[j] = (struct animation_channel) {
                target_node,
                target_path,
                num_timesteps,
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

    return (struct model) { data, vertex_array, (size_t)numIndices, data->nodes_count, animations,
        data->animations_count, skin->joints, joint_ids, inverse_bind_matrices,
        skin->joints_count };
}

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

Matrix node_world_txfm(gltf_node* node)
{
    // struct node node = nodes[idx];
    Matrix node_txfm = Matrix::CreateScale(node->scale);
    node_txfm *= Matrix::CreateFromQuaternion(node->rotation);
    node_txfm *= Matrix::CreateTranslation(node->translation);

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
    assert(index < model->num_animations);

    animation* animation = &model->animations[index];

    for (int channel_idx = 0; channel_idx < animation->num_channels; ++channel_idx)
    {
        struct animation_channel* channel = &animation->channels[channel_idx];
        float                     rel_time_since_start
            = fmod(time_since_start, channel->times[channel->num_timesteps - 1]);

        int last_timestep = 0;
        for (int i = 0; i < channel->num_timesteps; ++i)
        {
            if (rel_time_since_start < channel->times[i])
                break;
            last_timestep = i;
        }

        int next_timestep = (last_timestep + 1) % channel->num_timesteps;

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

// void apply_animation(float time_since_start, struct model* model, int index)
//{
//     assert(index <= model->num_animations);
//
//     animation* animation = &model->animations[index];
//
//     for (int channel_idx = 0; channel_idx < animation->num_channels; ++channel_idx)
//     {
//         struct animation_channel* channel = &animation->channels[channel_idx];
//         float                     rel_time_since_start
//             = fmod(time_since_start, channel->times[channel->num_timesteps - 1]);
//
//         int last_timestep;
//         // 0, am i bigger? yes? sick
//         for (last_timestep = channel->num_timesteps - 1; last_timestep >= 0; --last_timestep)
//         {
//             if (rel_time_since_start >= channel->times[last_timestep])
//                 break;
//         }
//
//         int next_timestep = last_timestep + 1;
//         if (next_timestep >= channel->num_timesteps)
//         {
//             next_timestep = last_timestep; // Prevent out-of-bounds access
//         }
//
//         float out[4];
//         int   components = animation_channel_components(channel->path_type);
//
//         float* last_data = &channel->data[last_timestep * components];
//         float* next_data = &channel->data[next_timestep * components];
//
//         float last_time = channel->times[last_timestep];
//         float next_time = channel->times[next_timestep];
//
//         for (int i = 0; i < components; ++i)
//         {
//             out[i] = lerp(last_data[i], next_data[i],
//                 (rel_time_since_start - last_time) / (next_time - last_time));
//         }
//
//         cgltf_node* nodes = model->model_data->nodes;
//         switch (channel->path_type)
//         {
//         case cgltf_animation_path_type_translation:
//             nodes[channel->target].translation[0] = out[0];
//             nodes[channel->target].translation[1] = out[1];
//             nodes[channel->target].translation[2] = out[2];
//             break;
//         case cgltf_animation_path_type_rotation:
//             nodes[channel->target].rotation[0] = out[0];
//             nodes[channel->target].rotation[1] = out[1];
//             nodes[channel->target].rotation[2] = out[2];
//             nodes[channel->target].rotation[3] = out[3];
//             break;
//         case cgltf_animation_path_type_scale:
//             nodes[channel->target].scale[0] = out[0];
//             nodes[channel->target].scale[1] = out[1];
//             nodes[channel->target].scale[2] = out[2];
//
//             break;
//         }
//     }
// }

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

struct App
{
    SDLWindowPtr  window;
    SDL_GLContext context;
    model         model;
    GLuint        bone_vao;
    GLuint        vertex_shader;
    GLuint        fragment_shader;
    GLuint        program;
    GameTimer     timer;
    float         angle;
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{

    // SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "waitevent");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SDL: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    App* app = new App;
    app->window.reset(
        SDL_CreateWindow("OpenGL Triangle", 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));
    if (!app->window)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

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

    app->model = load_model();

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

    app->timer.setFixedTimeStep(false);

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
            apply_animation(accum, &app->model, 3);

            app->angle += 2.0f * M_PI * elapsed * 0.5;
            app->angle = fmod(app->angle, 2 * M_PI);

            int width, height;
            SDL_GetWindowSizeInPixels(app->window.get(), &width, &height);
            const float ratio = width / (float)height;

            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glUseProgram(app->program);
            glBindVertexArray(app->model.vao);

            Matrix world_txfm = Matrix::CreateTranslation(0, -0.0, 0.0);
            world_txfm *= Matrix::CreateRotationY(DirectX::XMConvertToRadians(45.0f));
            world_txfm *= Matrix::CreateTranslation(0.0, 0.0, -1.0);
            world_txfm = world_txfm.Transpose();

            Matrix viewport_txfm = Matrix::CreatePerspectiveFieldOfView(
                XMConvertToRadians(90.0f), ratio, 0.1f, 10.0f);
            viewport_txfm = viewport_txfm.Transpose();

            Matrix bone_matrices[72];
            Matrix inverse_bone_matrices[72];
            for (int i = 0; i < app->model.num_joints; i++)
            {
                inverse_bone_matrices[i] = app->model.joint_inverse_mats[i];
                inverse_bone_matrices[i] = inverse_bone_matrices[i].Transpose();

                bone_matrices[i]
                    = node_world_txfm(&app->model.model_data->nodes[app->model.joint_ids[i]]);
                bone_matrices[i] = bone_matrices[i].Transpose();
            }

            GLuint world_txfm_loc = glGetUniformLocation(app->program, "world_txfm");
            GLuint viewport_txfm_loc = glGetUniformLocation(app->program, "viewport_txfm");
            GLuint preview_joint_loc = glGetUniformLocation(app->program, "preview_joint");
            GLuint inverse_bone_matrix_loc
                = glGetUniformLocation(app->program, "inverse_bone_matrix");
            GLuint bone_matrix_loc = glGetUniformLocation(app->program, "bone_matrix");

            glUniformMatrix4fv(world_txfm_loc, 1, true, reinterpret_cast<float*>(&world_txfm));
            glUniformMatrix4fv(
                viewport_txfm_loc, 1, true, reinterpret_cast<float*>(&viewport_txfm));
            glUniformMatrix4fv(inverse_bone_matrix_loc, 20, true,
                reinterpret_cast<float*>(&inverse_bone_matrices));
            glUniformMatrix4fv(bone_matrix_loc, 20, true, reinterpret_cast<float*>(&bone_matrices));

            glDrawElements(GL_TRIANGLES, app->model.num_indices, GL_UNSIGNED_SHORT, 0);

            // glBindVertexArray(app->bone_vao);

            //            for (int i = 0; i < app->model.num_nodes; i++)
            //            {
            //                Matrix bone_txfm = node_world_txfm(&app->model.model_data->nodes[i]);
            //
            //                bone_txfm *= world_txfm;
            //                glUniformMatrix4fv(
            //                    preview_joint_loc, 1, true, reinterpret_cast<float*>(&bone_txfm));
            //                glDrawArrays(GL_LINES, 0, 2);
            //            }

            SDL_GL_SwapWindow(app->window.get());
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

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    App* app = static_cast<App*>(appstate);
    delete app;
}
