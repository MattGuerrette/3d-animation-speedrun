#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

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

static const Vertex vertices[3] =
{
    { { -0.6f, -0.4f, 0.0f }, { 1.f, 0.f, 0.f } },
    { {  0.6f, -0.4f, 0.0f }, { 0.f, 1.f, 0.f } },
    { {   0.f,  0.6f, 0.0f }, { 0.f, 0.f, 1.f } }
};

static const char* vertex_shader_text =
"#version 460\n"
"layout(location = 0) in vec3 vCol;\n"
"layout(location = 1) in vec3 vPos;\n"
"layout(location = 0) uniform mat4 txfm;\n"
"out vec3 color;\n"
"void main()\n"
"{\n"
"    gl_Position = txfm * vec4(vPos, 1.0);\n"
"    color = vCol;\n"
"}\n";

static const char* fragment_shader_text =
"#version 460\n"
"in vec3 color;\n"
"out vec4 fragment;\n"
"void main()\n"
"{\n"
"    fragment = vec4(color, 1.0);\n"
"}\n";


struct mat4x4 {
    float data[16];
};

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

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

static float diff_time(struct timespec last, struct timespec now) {
    float ret = now.tv_sec - last.tv_sec;
    ret += (float)(now.tv_nsec - last.tv_nsec) / 1e9;
    return ret;
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

    GLuint vertex_buffer;
    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_text, NULL);
    glCompileShader(vertex_shader);

    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_text, NULL);
    glCompileShader(fragment_shader);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLuint vertex_array;
    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*) offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*) offsetof(Vertex, col));

    float angle = 0;

    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);

    while (!glfwWindowShouldClose(window))
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        float delta_s = diff_time(last, now);
        angle += 2.0f * M_PI * delta_s;
        angle = fmod(angle, 2 * M_PI);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        const float ratio = width / (float) height;

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vertex_array);

        struct mat4x4 txfm = mat4x4_translate(0, 1.0, 0.0);
        txfm = mat4x4_mul(mat4x4_rot_z(angle), txfm);
        glUniformMatrix4fv(0, 1, true, txfm.data);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);
        glfwPollEvents();

        last = now;
    }

    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

