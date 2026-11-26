#pragma once

#include <cstddef>
#include <cstdint>

namespace Neleo::Graphics::GL {

// Tipos básicos compatibles con OpenGL 3.3 Core (sin depender de <GL/gl.h> ni loaders externos)
using GLboolean = unsigned char;
using GLchar = char;
using GLbyte = signed char;
using GLubyte = unsigned char;
using GLshort = short;
using GLushort = unsigned short;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

inline constexpr GLboolean GL_FALSE_VAL = 0u;
inline constexpr GLboolean GL_TRUE_VAL  = 1u;
inline constexpr GLenum GL_NONE_VAL     = 0u;

inline constexpr GLbitfield GL_DEPTH_BUFFER_BIT_VAL = 0x00000100u;
inline constexpr GLbitfield GL_COLOR_BUFFER_BIT_VAL = 0x00004000u;

inline constexpr GLenum GL_TRIANGLES_VAL                = 0x0004u;
inline constexpr GLenum GL_ONE_VAL                      = 0x0001u;
inline constexpr GLenum GL_LESS_VAL                     = 0x0201u;
inline constexpr GLenum GL_EQUAL_VAL                    = 0x0202u;
inline constexpr GLenum GL_LEQUAL_VAL                   = 0x0203u;
inline constexpr GLenum GL_ALWAYS_VAL                   = 0x0207u;
inline constexpr GLenum GL_SRC_ALPHA_VAL                = 0x0302u;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA_VAL      = 0x0303u;
inline constexpr GLenum GL_FRONT_VAL                    = 0x0404u;
inline constexpr GLenum GL_BACK_VAL                     = 0x0405u;
inline constexpr GLenum GL_CW_VAL                       = 0x0900u;
inline constexpr GLenum GL_CCW_VAL                      = 0x0901u;
inline constexpr GLenum GL_CULL_FACE_VAL                = 0x0B44u;
inline constexpr GLenum GL_DEPTH_TEST_VAL               = 0x0B71u;
inline constexpr GLenum GL_VIEWPORT_VAL                 = 0x0BA2u;
inline constexpr GLenum GL_BLEND_VAL                    = 0x0BE2u;
inline constexpr GLenum GL_SCISSOR_TEST_VAL             = 0x0C11u;
inline constexpr GLenum GL_TEXTURE_BORDER_COLOR_VAL     = 0x1004u;
inline constexpr GLenum GL_UNPACK_ALIGNMENT_VAL         = 0x0CF5u;
inline constexpr GLenum GL_TEXTURE_2D_VAL               = 0x0DE1u;
inline constexpr GLenum GL_UNSIGNED_BYTE_VAL            = 0x1401u;
inline constexpr GLenum GL_UNSIGNED_SHORT_VAL           = 0x1403u;
inline constexpr GLenum GL_UNSIGNED_INT_VAL             = 0x1405u;
inline constexpr GLenum GL_FLOAT_VAL                    = 0x1406u;
inline constexpr GLenum GL_HALF_FLOAT_VAL               = 0x140Bu;
inline constexpr GLenum GL_DEPTH_COMPONENT_VAL          = 0x1902u;
inline constexpr GLenum GL_RGBA_VAL                     = 0x1908u;
inline constexpr GLenum GL_NEAREST_VAL                  = 0x2600u;
inline constexpr GLenum GL_LINEAR_VAL                   = 0x2601u;
inline constexpr GLenum GL_NEAREST_MIPMAP_NEAREST_VAL   = 0x2700u;
inline constexpr GLenum GL_NEAREST_MIPMAP_LINEAR_VAL    = 0x2702u;
inline constexpr GLenum GL_LINEAR_MIPMAP_LINEAR_VAL     = 0x2703u;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER_VAL       = 0x2800u;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER_VAL       = 0x2801u;
inline constexpr GLenum GL_TEXTURE_WRAP_S_VAL           = 0x2802u;
inline constexpr GLenum GL_TEXTURE_WRAP_T_VAL           = 0x2803u;
inline constexpr GLenum GL_REPEAT_VAL                   = 0x2901u;
inline constexpr GLenum GL_POLYGON_OFFSET_FILL_VAL      = 0x8037u;
inline constexpr GLenum GL_RGBA8_VAL                    = 0x8058u;
inline constexpr GLenum GL_CLAMP_TO_BORDER_VAL          = 0x812Du;
inline constexpr GLenum GL_CLAMP_TO_EDGE_VAL            = 0x812Fu;
inline constexpr GLenum GL_TEXTURE_MAX_LEVEL_VAL        = 0x813Du;
inline constexpr GLenum GL_DEPTH_COMPONENT24_VAL        = 0x81A6u;
inline constexpr GLenum GL_MIRRORED_REPEAT_VAL          = 0x8370u;
inline constexpr GLenum GL_TEXTURE0_VAL                 = 0x84C0u;
inline constexpr GLenum GL_TEXTURE1_VAL                 = 0x84C1u;
inline constexpr GLenum GL_TEXTURE2_VAL                 = 0x84C2u;
inline constexpr GLenum GL_RGBA16F_VAL                  = 0x881Au;
inline constexpr GLenum GL_ARRAY_BUFFER_VAL             = 0x8892u;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_VAL     = 0x8893u;
inline constexpr GLenum GL_STATIC_DRAW_VAL              = 0x88E4u;
inline constexpr GLenum GL_DYNAMIC_DRAW_VAL             = 0x88E8u;
inline constexpr GLenum GL_FRAGMENT_SHADER_VAL          = 0x8B30u;
inline constexpr GLenum GL_VERTEX_SHADER_VAL            = 0x8B31u;
inline constexpr GLenum GL_COMPILE_STATUS_VAL           = 0x8B81u;
inline constexpr GLenum GL_LINK_STATUS_VAL              = 0x8B82u;
inline constexpr GLenum GL_DEPTH_COMPONENT32F_VAL       = 0x8CACu;
inline constexpr GLenum GL_READ_FRAMEBUFFER_VAL         = 0x8CA8u;
inline constexpr GLenum GL_DRAW_FRAMEBUFFER_VAL         = 0x8CA9u;
inline constexpr GLenum GL_COLOR_ATTACHMENT0_VAL        = 0x8CE0u;
inline constexpr GLenum GL_DEPTH_ATTACHMENT_VAL         = 0x8D00u;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE_VAL     = 0x8CD5u;
inline constexpr GLenum GL_FRAMEBUFFER_VAL              = 0x8D40u;
inline constexpr GLenum GL_RENDERBUFFER_VAL             = 0x8D41u;

#if defined(_WIN32)
#define WIIU_GL_APIENTRY __stdcall
#else
#define WIIU_GL_APIENTRY
#endif

// Funciones GL 1.1 presentes en opengl32.dll + extensiones GL 3.3 Core cargadas en tiempo de ejecución
struct Functions {
    void (WIIU_GL_APIENTRY* Viewport)(GLint x, GLint y, GLsizei width, GLsizei height){nullptr};
    void (WIIU_GL_APIENTRY* Scissor)(GLint x, GLint y, GLsizei width, GLsizei height){nullptr};
    void (WIIU_GL_APIENTRY* ClearColor)(GLfloat r, GLfloat g, GLfloat b, GLfloat a){nullptr};
    void (WIIU_GL_APIENTRY* Clear)(GLbitfield mask){nullptr};
    void (WIIU_GL_APIENTRY* Enable)(GLenum cap){nullptr};
    void (WIIU_GL_APIENTRY* Disable)(GLenum cap){nullptr};
    void (WIIU_GL_APIENTRY* BlendFunc)(GLenum sfactor, GLenum dfactor){nullptr};
    void (WIIU_GL_APIENTRY* DepthMask)(GLboolean flag){nullptr};
    void (WIIU_GL_APIENTRY* DepthFunc)(GLenum func){nullptr};
    void (WIIU_GL_APIENTRY* CullFace)(GLenum mode){nullptr};
    void (WIIU_GL_APIENTRY* FrontFace)(GLenum mode){nullptr};
    void (WIIU_GL_APIENTRY* PolygonOffset)(GLfloat factor, GLfloat units){nullptr};
    void (WIIU_GL_APIENTRY* DrawBuffer)(GLenum buf){nullptr};
    void (WIIU_GL_APIENTRY* ReadBuffer)(GLenum src){nullptr};
    void (WIIU_GL_APIENTRY* ColorMask)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha){nullptr};
    void (WIIU_GL_APIENTRY* GetIntegerv)(GLenum pname, GLint* data){nullptr};
    void (WIIU_GL_APIENTRY* DrawArrays)(GLenum mode, GLint first, GLsizei count){nullptr};
    void (WIIU_GL_APIENTRY* DrawElements)(GLenum mode, GLsizei count, GLenum type, const void* indices){nullptr};
    void (WIIU_GL_APIENTRY* DrawElementsInstanced)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount){nullptr};

    GLuint (WIIU_GL_APIENTRY* CreateShader)(GLenum type){nullptr};
    void (WIIU_GL_APIENTRY* ShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length){nullptr};
    void (WIIU_GL_APIENTRY* CompileShader)(GLuint shader){nullptr};
    void (WIIU_GL_APIENTRY* GetShaderiv)(GLuint shader, GLenum pname, GLint* params){nullptr};
    void (WIIU_GL_APIENTRY* GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog){nullptr};
    GLuint (WIIU_GL_APIENTRY* CreateProgram)(){nullptr};
    void (WIIU_GL_APIENTRY* AttachShader)(GLuint program, GLuint shader){nullptr};
    void (WIIU_GL_APIENTRY* LinkProgram)(GLuint program){nullptr};
    void (WIIU_GL_APIENTRY* GetProgramiv)(GLuint program, GLenum pname, GLint* params){nullptr};
    void (WIIU_GL_APIENTRY* GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog){nullptr};
    void (WIIU_GL_APIENTRY* DeleteShader)(GLuint shader){nullptr};
    void (WIIU_GL_APIENTRY* DeleteProgram)(GLuint program){nullptr};
    void (WIIU_GL_APIENTRY* UseProgram)(GLuint program){nullptr};

    void (WIIU_GL_APIENTRY* GenVertexArrays)(GLsizei n, GLuint* arrays){nullptr};
    void (WIIU_GL_APIENTRY* BindVertexArray)(GLuint array){nullptr};
    void (WIIU_GL_APIENTRY* DeleteVertexArrays)(GLsizei n, const GLuint* arrays){nullptr};

    void (WIIU_GL_APIENTRY* GenBuffers)(GLsizei n, GLuint* buffers){nullptr};
    void (WIIU_GL_APIENTRY* BindBuffer)(GLenum target, GLuint buffer){nullptr};
    void (WIIU_GL_APIENTRY* BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage){nullptr};
    void (WIIU_GL_APIENTRY* BufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data){nullptr};
    void (WIIU_GL_APIENTRY* DeleteBuffers)(GLsizei n, const GLuint* buffers){nullptr};

    void (WIIU_GL_APIENTRY* EnableVertexAttribArray)(GLuint index){nullptr};
    void (WIIU_GL_APIENTRY* VertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer){nullptr};
    void (WIIU_GL_APIENTRY* VertexAttribIPointer)(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer){nullptr};
    void (WIIU_GL_APIENTRY* VertexAttribDivisor)(GLuint index, GLuint divisor){nullptr};

    GLint (WIIU_GL_APIENTRY* GetUniformLocation)(GLuint program, const GLchar* name){nullptr};
    void (WIIU_GL_APIENTRY* Uniform1i)(GLint location, GLint v0){nullptr};
    void (WIIU_GL_APIENTRY* Uniform1f)(GLint location, GLfloat v0){nullptr};
    void (WIIU_GL_APIENTRY* Uniform2f)(GLint location, GLfloat v0, GLfloat v1){nullptr};
    void (WIIU_GL_APIENTRY* Uniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2){nullptr};
    void (WIIU_GL_APIENTRY* Uniform3fv)(GLint location, GLsizei count, const GLfloat* value){nullptr};
    void (WIIU_GL_APIENTRY* Uniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3){nullptr};
    void (WIIU_GL_APIENTRY* Uniform4fv)(GLint location, GLsizei count, const GLfloat* value){nullptr};
    void (WIIU_GL_APIENTRY* UniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value){nullptr};

    void (WIIU_GL_APIENTRY* GenTextures)(GLsizei n, GLuint* textures){nullptr};
    void (WIIU_GL_APIENTRY* BindTexture)(GLenum target, GLuint texture){nullptr};
    void (WIIU_GL_APIENTRY* DeleteTextures)(GLsizei n, const GLuint* textures){nullptr};
    void (WIIU_GL_APIENTRY* TexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels){nullptr};
    void (WIIU_GL_APIENTRY* TexParameteri)(GLenum target, GLenum pname, GLint param){nullptr};
    void (WIIU_GL_APIENTRY* TexParameterfv)(GLenum target, GLenum pname, const GLfloat* params){nullptr};
    void (WIIU_GL_APIENTRY* PixelStorei)(GLenum pname, GLint param){nullptr};
    void (WIIU_GL_APIENTRY* ActiveTexture)(GLenum texture){nullptr};
    void (WIIU_GL_APIENTRY* GenerateMipmap)(GLenum target){nullptr};

    void (WIIU_GL_APIENTRY* GenFramebuffers)(GLsizei n, GLuint* framebuffers){nullptr};
    void (WIIU_GL_APIENTRY* BindFramebuffer)(GLenum target, GLuint framebuffer){nullptr};
    void (WIIU_GL_APIENTRY* DeleteFramebuffers)(GLsizei n, const GLuint* framebuffers){nullptr};
    void (WIIU_GL_APIENTRY* FramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level){nullptr};
    void (WIIU_GL_APIENTRY* FramebufferRenderbuffer)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer){nullptr};
    GLenum (WIIU_GL_APIENTRY* CheckFramebufferStatus)(GLenum target){nullptr};
    void (WIIU_GL_APIENTRY* DrawBuffers)(GLsizei n, const GLenum* bufs){nullptr};
    void (WIIU_GL_APIENTRY* BlitFramebuffer)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter){nullptr};

    void (WIIU_GL_APIENTRY* GenRenderbuffers)(GLsizei n, GLuint* renderbuffers){nullptr};
    void (WIIU_GL_APIENTRY* BindRenderbuffer)(GLenum target, GLuint renderbuffer){nullptr};
    void (WIIU_GL_APIENTRY* RenderbufferStorage)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height){nullptr};
    void (WIIU_GL_APIENTRY* DeleteRenderbuffers)(GLsizei n, const GLuint* renderbuffers){nullptr};
};

[[nodiscard]] const Functions& GetFunctions() noexcept;
[[nodiscard]] bool IsLoaded() noexcept;
bool LoadFunctions() noexcept;
void ResetFunctions() noexcept;

} // namespace Neleo::Graphics::GL
