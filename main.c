#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include <time.h>


typedef struct Vertex
{
    float pos[3];
    float col[3];
} Vertex;

//static const Vertex vertices[4] =
//{
//    { { -0.5f, -0.5f, 0.0f }, { 0.f, 0.f, 0.f } },
//    { {  0.5f, -0.5f, 0.0f }, { 1.f, 0.f, 0.f } },
//    { { -0.5f,  0.5f, 0.0f }, { 0.f, 1.f, 0.f } },
//    { {  0.5f,  0.5f, 0.0f }, { 1.f, 1.f, 0.f } },
//};
//
//static const uint32_t indices[6] = {
//    0, 1, 2,
//    1, 3, 2,
//};

static const char* vertex_shader_text =
"#version 460\n"
"layout(location = 0) in vec3 vNorm;\n"
"layout(location = 1) in vec3 vPos;\n"
"layout(location = 0) uniform mat4 world_txfm;\n"
"layout(location = 1) uniform mat4 viewport_txfm;\n"
"out vec3 norm;\n"
"void main()\n"
"{\n"
"    gl_Position = viewport_txfm * world_txfm * vec4(vPos, 1.0);\n"
"    norm = mat3(world_txfm) * vNorm;\n"
"}\n";

static const char* fragment_shader_text =
"#version 460\n"
"in vec3 norm;\n"
"out vec4 fragment;\n"
"void main()\n"
"{\n"
"    vec3 sun_dir = normalize(vec3(0.0, -1.0, -1.0))\n;"
"    float diffuse = max(dot(norm, -sun_dir), 0.0);\n"
"    float ambient = 0.1;\n"
"    fragment = vec4((ambient + diffuse) * vec3(1.0, 1.0, 1.0), 1.0);\n"
"}\n";


struct mat4x4 {
    float data[16];
};

struct mat4x4 mat4x4_rot_x(float angle) {
    float c = cos(angle);
    float s = sin(angle);

    return (struct mat4x4) {
        1.0, 0.0,  0.0, 0.0,
        0.0,   c,   -s, 0.0,
        0.0,   s,    c, 0.0,
        0.0, 0.0,  0.0, 1.0,
    };
}

struct mat4x4 mat4x4_rot_z(float angle) {
    float c = cos(angle);
    float s = sin(angle);

    return (struct mat4x4) {
          c,   -s, 0.0, 0.0,
          s,    c, 0.0, 0.0,
        0.0,  0.0, 1.0, 0.0,
        0.0,  0.0, 0.0, 1.0,
    };
}

struct mat4x4 mat4x4_translate(float x, float y, float z) {
    return (struct mat4x4) {
        1.0,  0.0, 0.0,   x,
        0.0,  1.0, 0.0,   y,
        0.0,  0.0, 1.0,   z,
        0.0,  0.0, 0.0, 1.0,
    };
}

struct vec4 {
    float data[4];
};

struct vec4 mat4x4_row(struct mat4x4 mat, int row) {
    struct vec4 ret;

    int start_idx = row * 4;
    ret.data[0] = mat.data[start_idx];
    ret.data[1] = mat.data[start_idx + 1];
    ret.data[2] = mat.data[start_idx + 2];
    ret.data[3] = mat.data[start_idx + 3];
    return ret;
}

struct vec4 mat4x4_col(struct mat4x4 mat, int col) {
    struct vec4 ret;

    ret.data[0] = mat.data[col];
    ret.data[1] = mat.data[4 + col];
    ret.data[2] = mat.data[8 + col];
    ret.data[3] = mat.data[12 + col];
    return ret;
}

float vec4_dot(struct vec4 a, struct vec4 b) {
    return a.data[0] * b.data[0] +
           a.data[1] * b.data[1] +
           a.data[2] * b.data[2] +
           a.data[3] * b.data[3];
}

struct mat4x4 mat4x4_mul(struct mat4x4 a, struct mat4x4 b) {
    struct mat4x4 ret;

    for (int i = 0; i < 16; ++i) {
        int row = i / 4;
        int col = i % 4;

        struct vec4 a_row = mat4x4_row(a, row);
        struct vec4 b_col = mat4x4_col(b, col);

        ret.data[i] = vec4_dot(a_row, b_col);
    }

    return ret;
}

struct mat4x4 mat4x4_perspective(float n, float f) {
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
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0,   a,   b,
        0.0, 0.0,-1.0, 0.0,
    };
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

static float diff_time(struct timespec last, struct timespec now) {
    float ret = now.tv_sec - last.tv_sec;
    ret += (float)(now.tv_nsec - last.tv_nsec) / 1e9;
    return ret;
}


struct buffer {
    char* data;
    size_t len;
};

struct buffer load_file(const char* path, struct buffer* buf) {
    FILE* f = fopen(path, "rb");
    assert(f);
    ssize_t read_len = fread(buf->data, 1, buf->len, f);
    assert(read_len >= 0);
    fclose(f);

    struct buffer ret = {buf->data, read_len};
    buf->data += read_len;
    buf->len -= read_len;
    return ret;
}

struct model {
    GLuint vao;
    size_t num_indices;
};
struct model load_model(void) {
    char buf_data[4096];
    struct buffer buf = { buf_data, 4096 };

    struct buffer positions_buf = load_file("positions.bin", &buf);
    struct buffer normals_buf = load_file("normals.bin", &buf);
    struct buffer index_buf = load_file("indices.bin", &buf);

    float* normals = (float*)normals_buf.data;
    for (int i = 0; i < normals_buf.len / 4; i += 3) {
        printf("%f %f %f\n", normals[i], normals[i + 1], normals[i + 2]);
    }

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

    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_buf.len, index_buf.data, GL_STATIC_DRAW);

    glBindVertexArray(0);
    return (struct model){vertex_array, index_buf.len / 2 };

}


GLuint compile_shader(const char* shader_text, GLenum shader_type) {
    const GLuint vertex_shader = glCreateShader(shader_type);
    glShaderSource(vertex_shader, 1, &shader_text, NULL);
    glCompileShader(vertex_shader);

    int status;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);

    if (!status) {
        char buf[4096];

        GLsizei len;
        glGetShaderInfoLog(vertex_shader, 4096, &len, buf);
        printf("%*s\n", len, buf);
    }
    return vertex_shader;
}

int main(void)
{
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(500, 500, "OpenGL Triangle", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // NOTE: OpenGL error checks have been omitted for brevity

    struct model model = load_model();

    const GLuint vertex_shader = compile_shader(vertex_shader_text, GL_VERTEX_SHADER);
    const GLuint fragment_shader = compile_shader(fragment_shader_text, GL_FRAGMENT_SHADER);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    //GLuint vertex_buffer;
    //glGenBuffers(1, &vertex_buffer);
    //glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    //glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);


    //GLuint vertex_array;
    //glGenVertexArrays(1, &vertex_array);
    //glBindVertexArray(vertex_array);
    //glEnableVertexAttribArray(1);
    //glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
    //                      sizeof(Vertex), (void*) offsetof(Vertex, pos));
    //glEnableVertexAttribArray(0);
    //glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
    //                      sizeof(Vertex), (void*) offsetof(Vertex, col));

    //GLuint ebo;
    //glGenBuffers(1, &ebo);
    //glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    //glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    float angle = 0;

    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window))
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        float delta_s = diff_time(last, now);
        angle += 2.0f * M_PI * delta_s * 0.5;
        angle = fmod(angle, 2 * M_PI);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        const float ratio = width / (float) height;

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(model.vao);

        struct mat4x4 world_txfm = mat4x4_translate(0, 1.0, 0.0);
        world_txfm = mat4x4_mul(mat4x4_rot_x(angle), world_txfm);
        world_txfm = mat4x4_mul(mat4x4_translate(0.0, 0.0, -5.0), world_txfm);

        struct mat4x4 viewport_txfm = mat4x4_perspective(0.1, 10.0);

        glUniformMatrix4fv(0, 1, true, world_txfm.data);
        glUniformMatrix4fv(1, 1, true, viewport_txfm.data);

        //glDrawArrays(GL_TRIANGLES, 0, 6);
        glDrawElements(GL_TRIANGLES, model.num_indices, GL_UNSIGNED_SHORT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        last = now;
    }

    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

