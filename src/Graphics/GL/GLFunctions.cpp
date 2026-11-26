#include "Graphics/GL/GLFunctions.hpp"
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Neleo::Graphics::GL {

namespace {

Functions g_gl{};
bool g_loaded = false;

void* ResolveGLProc(const char* name) noexcept {
    if (!name) return nullptr;
#if defined(_WIN32)
    PROC proc = wglGetProcAddress(name);
    // Microsoft OpenGL ICD retorna 0, 1, 2, 3 o -1 en fallos; usar opengl32.dll para funciones 1.0/1.1
    const uintptr_t addr = reinterpret_cast<uintptr_t>(proc);
    if (addr == 0u || addr == 1u || addr == 2u || addr == 3u || addr == static_cast<uintptr_t>(-1)) {
        HMODULE glMod = GetModuleHandleA("opengl32.dll");
        if (glMod) {
            proc = GetProcAddress(glMod, name);
        } else {
            proc = nullptr;
        }
    }
    return reinterpret_cast<void*>(proc);
#else
    return nullptr;
#endif
}

template <typename FnPtr>
bool LoadProc(FnPtr& outFn, const char* name) noexcept {
    void* raw = ResolveGLProc(name);
    std::memcpy(&outFn, &raw, sizeof(outFn));
    return outFn != nullptr;
}

} // namespace

const Functions& GetFunctions() noexcept {
    return g_gl;
}

bool IsLoaded() noexcept {
    return g_loaded;
}

void ResetFunctions() noexcept {
    g_gl = Functions{};
    g_loaded = false;
}

bool LoadFunctions() noexcept {
    g_gl = Functions{};
    g_loaded = false;

    bool ok = true;
    ok = LoadProc(g_gl.Viewport, "glViewport") && ok;
    ok = LoadProc(g_gl.Scissor, "glScissor") && ok;
    ok = LoadProc(g_gl.ClearColor, "glClearColor") && ok;
    ok = LoadProc(g_gl.Clear, "glClear") && ok;
    ok = LoadProc(g_gl.Enable, "glEnable") && ok;
    ok = LoadProc(g_gl.Disable, "glDisable") && ok;
    ok = LoadProc(g_gl.BlendFunc, "glBlendFunc") && ok;
    ok = LoadProc(g_gl.DepthMask, "glDepthMask") && ok;
    ok = LoadProc(g_gl.DepthFunc, "glDepthFunc") && ok;
    ok = LoadProc(g_gl.CullFace, "glCullFace") && ok;
    ok = LoadProc(g_gl.FrontFace, "glFrontFace") && ok;
    ok = LoadProc(g_gl.PolygonOffset, "glPolygonOffset") && ok;
    ok = LoadProc(g_gl.DrawBuffer, "glDrawBuffer") && ok;
    ok = LoadProc(g_gl.ReadBuffer, "glReadBuffer") && ok;
    ok = LoadProc(g_gl.ColorMask, "glColorMask") && ok;
    ok = LoadProc(g_gl.GetIntegerv, "glGetIntegerv") && ok;
    ok = LoadProc(g_gl.DrawArrays, "glDrawArrays") && ok;
    ok = LoadProc(g_gl.DrawElements, "glDrawElements") && ok;
    ok = LoadProc(g_gl.DrawElementsInstanced, "glDrawElementsInstanced") && ok;

    ok = LoadProc(g_gl.CreateShader, "glCreateShader") && ok;
    ok = LoadProc(g_gl.ShaderSource, "glShaderSource") && ok;
    ok = LoadProc(g_gl.CompileShader, "glCompileShader") && ok;
    ok = LoadProc(g_gl.GetShaderiv, "glGetShaderiv") && ok;
    ok = LoadProc(g_gl.GetShaderInfoLog, "glGetShaderInfoLog") && ok;
    ok = LoadProc(g_gl.CreateProgram, "glCreateProgram") && ok;
    ok = LoadProc(g_gl.AttachShader, "glAttachShader") && ok;
    ok = LoadProc(g_gl.LinkProgram, "glLinkProgram") && ok;
    ok = LoadProc(g_gl.GetProgramiv, "glGetProgramiv") && ok;
    ok = LoadProc(g_gl.GetProgramInfoLog, "glGetProgramInfoLog") && ok;
    ok = LoadProc(g_gl.DeleteShader, "glDeleteShader") && ok;
    ok = LoadProc(g_gl.DeleteProgram, "glDeleteProgram") && ok;
    ok = LoadProc(g_gl.UseProgram, "glUseProgram") && ok;

    ok = LoadProc(g_gl.GenVertexArrays, "glGenVertexArrays") && ok;
    ok = LoadProc(g_gl.BindVertexArray, "glBindVertexArray") && ok;
    ok = LoadProc(g_gl.DeleteVertexArrays, "glDeleteVertexArrays") && ok;

    ok = LoadProc(g_gl.GenBuffers, "glGenBuffers") && ok;
    ok = LoadProc(g_gl.BindBuffer, "glBindBuffer") && ok;
    ok = LoadProc(g_gl.BufferData, "glBufferData") && ok;
    ok = LoadProc(g_gl.BufferSubData, "glBufferSubData") && ok;
    ok = LoadProc(g_gl.DeleteBuffers, "glDeleteBuffers") && ok;

    ok = LoadProc(g_gl.EnableVertexAttribArray, "glEnableVertexAttribArray") && ok;
    ok = LoadProc(g_gl.VertexAttribPointer, "glVertexAttribPointer") && ok;
    ok = LoadProc(g_gl.VertexAttribIPointer, "glVertexAttribIPointer") && ok;
    ok = LoadProc(g_gl.VertexAttribDivisor, "glVertexAttribDivisor") && ok;

    ok = LoadProc(g_gl.GetUniformLocation, "glGetUniformLocation") && ok;
    ok = LoadProc(g_gl.Uniform1i, "glUniform1i") && ok;
    ok = LoadProc(g_gl.Uniform1f, "glUniform1f") && ok;
    ok = LoadProc(g_gl.Uniform2f, "glUniform2f") && ok;
    ok = LoadProc(g_gl.Uniform3f, "glUniform3f") && ok;
    ok = LoadProc(g_gl.Uniform3fv, "glUniform3fv") && ok;
    ok = LoadProc(g_gl.Uniform4f, "glUniform4f") && ok;
    ok = LoadProc(g_gl.Uniform4fv, "glUniform4fv") && ok;
    ok = LoadProc(g_gl.UniformMatrix4fv, "glUniformMatrix4fv") && ok;

    ok = LoadProc(g_gl.GenTextures, "glGenTextures") && ok;
    ok = LoadProc(g_gl.BindTexture, "glBindTexture") && ok;
    ok = LoadProc(g_gl.DeleteTextures, "glDeleteTextures") && ok;
    ok = LoadProc(g_gl.TexImage2D, "glTexImage2D") && ok;
    ok = LoadProc(g_gl.TexParameteri, "glTexParameteri") && ok;
    ok = LoadProc(g_gl.TexParameterfv, "glTexParameterfv") && ok;
    ok = LoadProc(g_gl.PixelStorei, "glPixelStorei") && ok;
    ok = LoadProc(g_gl.ActiveTexture, "glActiveTexture") && ok;
    ok = LoadProc(g_gl.GenerateMipmap, "glGenerateMipmap") && ok;

    ok = LoadProc(g_gl.GenFramebuffers, "glGenFramebuffers") && ok;
    ok = LoadProc(g_gl.BindFramebuffer, "glBindFramebuffer") && ok;
    ok = LoadProc(g_gl.DeleteFramebuffers, "glDeleteFramebuffers") && ok;
    ok = LoadProc(g_gl.FramebufferTexture2D, "glFramebufferTexture2D") && ok;
    ok = LoadProc(g_gl.FramebufferRenderbuffer, "glFramebufferRenderbuffer") && ok;
    ok = LoadProc(g_gl.CheckFramebufferStatus, "glCheckFramebufferStatus") && ok;
    ok = LoadProc(g_gl.DrawBuffers, "glDrawBuffers") && ok;
    ok = LoadProc(g_gl.BlitFramebuffer, "glBlitFramebuffer") && ok;

    ok = LoadProc(g_gl.GenRenderbuffers, "glGenRenderbuffers") && ok;
    ok = LoadProc(g_gl.BindRenderbuffer, "glBindRenderbuffer") && ok;
    ok = LoadProc(g_gl.RenderbufferStorage, "glRenderbufferStorage") && ok;
    ok = LoadProc(g_gl.DeleteRenderbuffers, "glDeleteRenderbuffers") && ok;

    g_loaded = ok;
    return ok;
}

} // namespace Neleo::Graphics::GL
