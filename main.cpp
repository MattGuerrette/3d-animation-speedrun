// #define GL_GLEXT_PROTOTYPES
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>

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

// #define SDL_MAIN_USE_CALLBACKS
// #include <SDL3/SDL.h>
// #include <SDL3/SDL_main.h>

typedef struct Vertex
{
    float pos[3];
    float col[3];
} Vertex;

// static const Vertex vertices[4] =
//{
//     { { -0.5f, -0.5f, 0.0f }, { 0.f, 0.f, 0.f } },
//     { {  0.5f, -0.5f, 0.0f }, { 1.f, 0.f, 0.f } },
//     { { -0.5f,  0.5f, 0.0f }, { 0.f, 1.f, 0.f } },
//     { {  0.5f,  0.5f, 0.0f }, { 1.f, 1.f, 0.f } },
// };
//
// static const uint32_t indices[6] = {
//     0, 1, 2,
//     1, 3, 2,
// };

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
// static const char* vertex_shader_text =
//"#version 460\n"
//"layout(location = 0) in vec3 vNorm;\n"
//"layout(location = 1) in vec3 vPos;\n"
//"layout(location = 2) in uvec4 vJoints;\n"
//"layout(location = 3) in vec4 vWeights;\n"
//"layout(location = 0) uniform mat4 world_txfm;\n"
//"layout(location = 1) uniform mat4 viewport_txfm;\n"
//"layout(location = 2) uniform uint preview_joint = 1;\n"
//"out vec3 norm;\n"
//"out float joint_color;\n"
//"void main()\n"
//"{\n"
//"    gl_Position = viewport_txfm * world_txfm * vec4(vPos, 1.0);\n"
//"    joint_color = 0.0;\n"
//"    for (int i = 0; i < 4; ++i) { \n"
//"        if (vJoints[i] == preview_joint && vWeights[i] > 0) joint_color =
// vWeights[i]; \n" "    }\n" "    norm = mat3(world_txfm) * vNorm;\n"
//"}\n";

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

struct mat4x4
{
    float data[16];
};

struct mat4x4 mat4x4_rot_x(float angle)
{
    float c = cos(angle);
    float s = sin(angle);

    return (struct mat4x4) {
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        c,
        -s,
        0.0,
        0.0,
        s,
        c,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
}

struct mat4x4 mat4x4_transpose(struct mat4x4 in)
{
    struct mat4x4 ret;
    for (int i = 0; i < 16; ++i)
    {
        int row = i / 4;
        int col = i % 4;

        ret.data[col * 4 + row] = in.data[row * 4 + col];
    }
    return ret;
}

struct mat4x4 mat4x4_rot_y(float angle)
{
    float c = cos(angle);
    float s = sin(angle);

    return (struct mat4x4) {
        c,
        0.0,
        -s,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        s,
        0.0,
        c,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
}

struct mat4x4 mat4x4_rot_z(float angle)
{
    float c = cos(angle);
    float s = sin(angle);

    return (struct mat4x4) {
        c,
        -s,
        0.0,
        0.0,
        s,
        c,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
}

struct mat4x4 mat4x4_from_quat(float* quat)
{
    float x = quat[0];
    float y = quat[1];
    float z = quat[2];
    float w = quat[3];

    float x2 = x * x;
    float y2 = y * y;
    float z2 = z * z;
    float w2 = w * w;

    float xy = 2.0f * x * y;
    float xz = 2.0f * x * z;
    float xw = 2.0f * x * w;
    float yz = 2.0f * y * z;
    float yw = 2.0f * y * w;
    float zw = 2.0f * z * w;

    return (struct mat4x4) { w2 + x2 - y2 - z2, xy - zw, xz + yw, 0.0f, xy + zw, w2 - x2 + y2 - z2,
        yz - xw, 0.0f, xz - yw, yz + xw, w2 - x2 - y2 + z2, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
}

struct mat4x4 mat4x4_translate(float x, float y, float z)
{
    return (struct mat4x4) {
        1.0,
        0.0,
        0.0,
        x,
        0.0,
        1.0,
        0.0,
        y,
        0.0,
        0.0,
        1.0,
        z,
        0.0,
        0.0,
        0.0,
        1.0,
    };
}

struct vec4
{
    float data[4];
};

struct vec4 mat4x4_row(struct mat4x4 mat, int row)
{
    struct vec4 ret;

    int start_idx = row * 4;
    ret.data[0] = mat.data[start_idx];
    ret.data[1] = mat.data[start_idx + 1];
    ret.data[2] = mat.data[start_idx + 2];
    ret.data[3] = mat.data[start_idx + 3];
    return ret;
}

struct vec4 mat4x4_col(struct mat4x4 mat, int col)
{
    struct vec4 ret;

    ret.data[0] = mat.data[col];
    ret.data[1] = mat.data[4 + col];
    ret.data[2] = mat.data[8 + col];
    ret.data[3] = mat.data[12 + col];
    return ret;
}

float vec4_dot(struct vec4 a, struct vec4 b)
{
    return a.data[0] * b.data[0] + a.data[1] * b.data[1] + a.data[2] * b.data[2]
        + a.data[3] * b.data[3];
}

struct mat4x4 mat4x4_scale(float x, float y, float z)
{
    return (struct mat4x4) {
        x,
        0.0,
        0.0,
        0.0,
        0.0,
        y,
        0.0,
        0.0,
        0.0,
        0.0,
        z,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
}

struct mat4x4 mat4x4_mul(struct mat4x4 a, struct mat4x4 b)
{
    struct mat4x4 ret;

    for (int i = 0; i < 16; ++i)
    {
        int row = i / 4;
        int col = i % 4;

        struct vec4 a_row = mat4x4_row(a, row);
        struct vec4 b_col = mat4x4_col(b, col);

        ret.data[i] = vec4_dot(a_row, b_col);
    }

    return ret;
}

struct mat4x4 mat4x4_perspective(float n, float f)
{
    // (an + b) / n = 0
    // (af + b) / f = 1.0
    //
    // an + b = 0
    // -an = b
    //
    // (af - an) / f = 1.0
    // a(f - n) / f = 1.0
    // a(f-n) = f
    //
    // a = f / (f -n)
    // b = -fn / (f - n)

    float a = -f / (f - n);
    float b = -f * n / (f - n);

    return (struct mat4x4) {
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        a,
        b,
        0.0,
        0.0,
        -1.0,
        0.0,
    };
}

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

struct buffer
{
    char*  data;
    size_t len;
};

struct buffer load_file(const char* path, struct buffer* buf)
{
    FILE* f = fopen(path, "rb");
    assert(f);
    size_t read_len = fread(buf->data, 1, buf->len, f);
    assert(read_len > 0);
    fclose(f);

    struct buffer ret = { buf->data, read_len };
    buf->data += read_len;
    buf->len -= read_len;
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

struct animation_channel
{
    uint32_t            target;
    enum animation_type animation_type;
    size_t              num_timesteps;
    float*              times;
    float*              data;
};

int animation_channel_components(enum animation_type animation_type)
{
    switch (animation_type)
    {
    case animation_type_translation:
    case animation_type_scale:
        return 3;
    case animation_type_rotation:
        return 4;
    }
}

struct model
{
    GLuint                    vao;
    size_t                    num_indices;
    struct node*              nodes;
    size_t                    num_nodes;
    struct animation_channel* animation_channels;
    size_t                    num_animations;
    uint32_t*                 joint_ids;
    struct mat4x4*            joint_inverse_mats;
    size_t                    num_joints;
};

struct model load_model(struct buffer buf)
{

    struct buffer positions_buf = load_file("positions.bin", &buf);
    struct buffer normals_buf = load_file("normals.bin", &buf);
    struct buffer index_buf = load_file("indices.bin", &buf);
    struct buffer nodes_buf = load_file("nodes.bin", &buf);
    struct buffer vert_joints_buf = load_file("vert_joints.bin", &buf);
    struct buffer vert_weights_buf = load_file("vert_weights.bin", &buf);
    struct buffer joint_info_buf = load_file("joint_info.bin", &buf);

    GLuint vertex_array;
    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);

    GLuint position_buffer;
    glGenBuffers(1, &position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
    glBufferData(GL_ARRAY_BUFFER, positions_buf.len, positions_buf.data, GL_STATIC_DRAW);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);

    GLuint normals_buffer;
    glGenBuffers(1, &normals_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, normals_buffer);
    glBufferData(GL_ARRAY_BUFFER, normals_buf.len, normals_buf.data, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);

    GLuint joints_buffer;
    glGenBuffers(1, &joints_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, joints_buffer);
    glBufferData(GL_ARRAY_BUFFER, vert_joints_buf.len, vert_joints_buf.data, GL_STATIC_DRAW);

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, 4, 0);

    GLuint weights_buffer;
    glGenBuffers(1, &weights_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, weights_buffer);
    glBufferData(GL_ARRAY_BUFFER, vert_weights_buf.len, vert_weights_buf.data, GL_STATIC_DRAW);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_buf.len, index_buf.data, GL_STATIC_DRAW);

    glBindVertexArray(0);

    // 3 floats translation
    // 4 floats rotation
    // 3 floats scale
    // 1 uint32 parent
#define ELEMS_PER_NODE (11)
#define NODE_SIZE (ELEMS_PER_NODE * 4)

    size_t       num_nodes = nodes_buf.len / NODE_SIZE;
    struct node* nodes = (node*)malloc(num_nodes * NODE_SIZE);

    float*    nodes_float = (float*)nodes_buf.data;
    uint32_t* nodes_u = (uint32_t*)nodes_buf.data;

    for (int i = 0; i < nodes_buf.len / NODE_SIZE; ++i)
    {
        nodes[i].translation[0] = nodes_float[ELEMS_PER_NODE * i];
        nodes[i].translation[1] = nodes_float[ELEMS_PER_NODE * i + 1];
        nodes[i].translation[2] = nodes_float[ELEMS_PER_NODE * i + 2];
        nodes[i].rotation[0] = nodes_float[ELEMS_PER_NODE * i + 3];
        nodes[i].rotation[1] = nodes_float[ELEMS_PER_NODE * i + 4];
        nodes[i].rotation[2] = nodes_float[ELEMS_PER_NODE * i + 5];
        nodes[i].rotation[3] = nodes_float[ELEMS_PER_NODE * i + 6];
        nodes[i].scale[0] = nodes_float[ELEMS_PER_NODE * i + 7];
        nodes[i].scale[1] = nodes_float[ELEMS_PER_NODE * i + 8];
        nodes[i].scale[2] = nodes_float[ELEMS_PER_NODE * i + 9];
        nodes[i].parent = nodes_u[ELEMS_PER_NODE * i + 10];
    }

    size_t                    num_animations = 29;
    struct animation_channel* animations
        = (animation_channel*)malloc(num_animations * sizeof(struct animation_channel));

    for (int animation_idx = 0; animation_idx < num_animations; ++animation_idx)
    {
        char path_buf[4096];
        sprintf(path_buf, "animations_%d.bin", animation_idx);

        struct buffer animation_data = load_file(path_buf, &buf);
        uint32_t      target_node = *(uint32_t*)animation_data.data;
        uint32_t      target_path = *(uint32_t*)(animation_data.data + 4);
        uint32_t      num_timesteps = *(uint32_t*)(animation_data.data + 8);
        uint32_t      cursor = 12;

        float* times = new float[num_timesteps];
        for (int i = 0; i < num_timesteps; ++i)
        {
            times[i] = *(float*)(animation_data.data + cursor);
            cursor += 4;
        }

        int components = 3;
        if (target_path == animation_type_rotation)
        {
            components = 4;
        }

        float* data = new float[num_timesteps * components];
        for (int i = 0; i < num_timesteps; ++i)
        {
            float* start = data + (i * components);
            for (int j = 0; j < components; ++j)
            {
                start[j] = *(float*)(animation_data.data + cursor);
                cursor += 4;
            }
        }

        animations[animation_idx] = (struct animation_channel) {
            target_node,
            (enum animation_type)target_path,
            num_timesteps,
            times,
            data,
        };
    }

    uint32_t       num_joints = *(uint32_t*)joint_info_buf.data;
    uint32_t*      joint_ids = new uint32_t[num_joints];
    struct mat4x4* inverse_bind_matrices = new mat4x4[num_joints];

    uint32_t cursor = 4;
    for (int i = 0; i < num_joints; ++i)
    {
        joint_ids[i] = *(((uint32_t*)(joint_info_buf.data + cursor)));
        printf("joint: %d\n", joint_ids[i]);
        cursor += 4;
    }

    for (int i = 0; i < num_joints; ++i)
    {
        printf("hi mom\n");
        memcpy(&inverse_bind_matrices[i], joint_info_buf.data + cursor, 4 * 16);
        // inverse_bind_matrices[i] = *(((struct mat4x4*)joint_info_buf.data +
        // cursor));
        inverse_bind_matrices[i] = mat4x4_transpose(inverse_bind_matrices[i]);
        for (int j = 0; j < 16; ++j)
        {
            if (j % 4 == 0)
                printf("\n");
            printf("%f ", inverse_bind_matrices[i].data[j]);
        }
        cursor += 4 * 16;
    }

    return (struct model) { vertex_array, index_buf.len / 2, nodes, num_nodes, animations,
        num_animations, joint_ids, inverse_bind_matrices, num_joints };
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

struct mat4x4 node_world_txfm(struct node* nodes, size_t idx)
{

    struct node   node = nodes[idx];
    struct mat4x4 node_txfm = mat4x4_scale(node.scale[0], node.scale[1], node.scale[2]);

    node_txfm = mat4x4_mul(mat4x4_from_quat(node.rotation), node_txfm);

    node_txfm = mat4x4_mul(
        mat4x4_translate(node.translation[0], node.translation[1], node.translation[2]), node_txfm);

    if (node.parent != UINT32_MAX)
    {
        node_txfm = mat4x4_mul(node_world_txfm(nodes, node.parent), node_txfm);
    }

    return node_txfm;
}

float lerp(float a, float b, float t)
{
    return a * (1.0 - t) + b * t;
}

void apply_animation(float time_since_start, struct model* model)
{
    for (int animation_idx = 0; animation_idx < model->num_animations; ++animation_idx)
    {

        struct animation_channel* channel = &model->animation_channels[animation_idx];
        float                     rel_time_since_start
            = fmod(time_since_start, channel->times[channel->num_timesteps - 1]);

        int last_timestep;
        // 0, am i bigger? yes? sick
        for (last_timestep = channel->num_timesteps - 1; last_timestep >= 0; --last_timestep)
        {
            if (rel_time_since_start >= channel->times[last_timestep])
                break;
        }

        int next_timestep = last_timestep + 1;

        float out[4];
        int   components = animation_channel_components(channel->animation_type);

        float* last_data = &channel->data[last_timestep * components];
        float* next_data = &channel->data[next_timestep * components];

        float last_time = channel->times[last_timestep];
        float next_time = channel->times[next_timestep];

        for (int i = 0; i < components; ++i)
        {
            out[i] = lerp(last_data[i], next_data[i],
                (rel_time_since_start - last_time) / (next_time - last_time));
        }

        struct node* node = &model->nodes[channel->target];
        switch (channel->animation_type)
        {
        case animation_type_translation:
            node->translation[0] = out[0];
            node->translation[1] = out[1];
            node->translation[2] = out[2];
            break;
        case animation_type_rotation:
            node->rotation[0] = out[0];
            node->rotation[1] = out[1];
            node->rotation[2] = out[2];
            node->rotation[3] = out[3];
            break;
        case animation_type_scale:
            node->scale[0] = out[0];
            node->scale[1] = out[1];
            node->scale[2] = out[2];
            break;
        }
    }
}


int main(void)
{

    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(500, 500, "OpenGL Triangle", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        glfwTerminate();
        fprintf(stderr, "Failed to init GLEW\n");
        exit(EXIT_FAILURE);
    }

    // NOTE: OpenGL error checks have been omitted for brevity

    struct buffer buf = { new char[10 * 1024 * 1024], 10 * 1024 * 1024 };
    struct model  model = load_model(buf);

    for (int i = 0; i < model.num_nodes; ++i)
    {
        printf("%f %f %f (%d)\n", model.nodes[i].translation[0], model.nodes[i].translation[1],
            model.nodes[i].translation[2], model.nodes[i].parent);
    }

    const GLuint vertex_shader = compile_shader(vertex_shader_text, GL_VERTEX_SHADER);
    const GLuint fragment_shader = compile_shader(fragment_shader_text, GL_FRAGMENT_SHADER);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    // GLuint vertex_buffer;
    // glGenBuffers(1, &vertex_buffer);
    // glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    // glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
    // GL_STATIC_DRAW);

    // GLuint vertex_array;
    // glGenVertexArrays(1, &vertex_array);
    // glBindVertexArray(vertex_array);
    // glEnableVertexAttribArray(1);
    // glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
    //                       sizeof(Vertex), (void*) offsetof(Vertex, pos));
    // glEnableVertexAttribArray(0);
    // glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
    //                       sizeof(Vertex), (void*) offsetof(Vertex, col));

    // GLuint ebo;
    // glGenBuffers(1, &ebo);
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    // glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
    // GL_STATIC_DRAW);

    float angle = 0;

    GLuint bone_vao = make_bone();

    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
    struct timespec start = last;

    glEnable(GL_DEPTH_TEST);

    glLineWidth(5);

    while (!glfwWindowShouldClose(window))
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        float delta_s = diff_time(last, now);
        apply_animation(diff_time(start, now), &model);

        angle += 2.0f * M_PI * delta_s * 0.5;
        angle = fmod(angle, 2 * M_PI);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        const float ratio = width / (float)height;

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(model.vao);

        struct mat4x4 world_txfm = mat4x4_translate(0, -0.0, 0.0);
        world_txfm = mat4x4_mul(mat4x4_rot_y(angle), world_txfm);
        world_txfm = mat4x4_mul(mat4x4_translate(0.0, 0.0, -1.0), world_txfm);

        struct mat4x4 viewport_txfm = mat4x4_perspective(0.1, 10.0);

        struct mat4x4 bone_matrices[20];
        struct mat4x4 inverse_bone_matrices[20];
        for (int i = 0; i < model.num_joints; i++)
        {
            inverse_bone_matrices[i] = model.joint_inverse_mats[i];
            bone_matrices[i] = node_world_txfm(model.nodes, model.joint_ids[i]);
            // bone_matrices[i] = mat4x4_translate(0, 0, 0);
        }

        GLuint world_txfm_loc = glGetUniformLocation(program, "world_txfm");
        GLuint viewport_txfm_loc = glGetUniformLocation(program, "viewport_txfm");
        GLuint preview_joint_loc = glGetUniformLocation(program, "preview_joint");
        GLuint inverse_bone_matrix_loc = glGetUniformLocation(program, "inverse_bone_matrix");
        GLuint bone_matrix_loc = glGetUniformLocation(program, "bone_matrix");

        glUniformMatrix4fv(world_txfm_loc, 1, true, world_txfm.data);
        glUniformMatrix4fv(viewport_txfm_loc, 1, true, viewport_txfm.data);
        glUniformMatrix4fv(inverse_bone_matrix_loc, 20, true, reinterpret_cast<GLfloat*>(&inverse_bone_matrices));
        glUniformMatrix4fv(bone_matrix_loc, 20, true, reinterpret_cast<GLfloat*>(&bone_matrices));

        glDrawElements(GL_TRIANGLES, model.num_indices, GL_UNSIGNED_SHORT, 0);

        glBindVertexArray(bone_vao);

        for (int i = 0; i < model.num_nodes; i++)
        {
            struct mat4x4 bone_txfm = node_world_txfm(model.nodes, i);
            bone_txfm = mat4x4_mul(world_txfm, bone_txfm);
            glUniformMatrix4fv(preview_joint_loc, 1, true, bone_txfm.data);
            glDrawArrays(GL_LINES, 0, 2);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        last = now;
    }

    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}
