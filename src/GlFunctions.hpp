#pragma once
// Chargement des fonctions OpenGL modernes.
//
// Windows ne fournit directement qu'OpenGL 1.1 (qui date de 1997).
// Toutes les fonctions plus récentes (shaders, framebuffers...) doivent être
// demandées au pilote de la carte graphique pendant l'exécution, une fois un
// contexte OpenGL créé. C'est le même principe que d'aller chercher les
// fonctions retro_* dans la DLL d'un core.
//
// Les fonctions d'OpenGL 1.1 (glClear, glViewport, glTexImage2D...) restent
// appelables directement : elles viennent de opengl32.lib.
//
// Android n'a qu'OpenGL ES 3 (NEXO_GLES), la version d'OpenGL des téléphones :
// presque les mêmes fonctions, toutes fournies directement par libGLESv3.so.
// On les range quand même dans gl:: pour que le code soit le même partout.

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#define NEXO_GLES 1
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

// "X-macro" : la liste n'est écrite qu'une fois, puis la macro X est
// redéfinie pour la "déplier" de deux façons différentes (déclaration des
// variables, puis chargement). Une technique héritée du C, toujours utile.
#define GL_FUNCTION_LIST(X)                                    \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                   \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)               \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)         \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)               \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D)     \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers)             \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)       \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer)             \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage)       \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer) \
    X(PFNGLCREATESHADERPROC, CreateShader)                     \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)                     \
    X(PFNGLCOMPILESHADERPROC, CompileShader)                   \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)                       \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)             \
    X(PFNGLDELETESHADERPROC, DeleteShader)                     \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)                   \
    X(PFNGLATTACHSHADERPROC, AttachShader)                     \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)                       \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)                     \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)           \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                         \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)                   \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)         \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                           \
    X(PFNGLUNIFORM1FPROC, Uniform1f)                           \
    X(PFNGLUNIFORM2FPROC, Uniform2f)                           \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)               \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)               \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays)         \
    X(PFNGLBINDSAMPLERPROC, BindSampler)                       \
    X(PFNGLBINDBUFFERPROC, BindBuffer)                         \
    X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer)

// [C++] Les fonctions sont rangées dans un "namespace" : on écrit
// gl::CreateShader(...), ce qui évite tout conflit de noms.
namespace gl {

// [C++] "inline" sur une variable globale (C++17) : on peut la définir dans
// un .hpp inclus par plusieurs .cpp sans erreur "symbole défini deux fois".
#ifdef NEXO_GLES
// (OpenGL ES ne définit pas les types PFNGL...PROC : on prend celui de la fonction.)
#define GLFN_DECLARE(type, name) inline decltype(&::gl##name) name = nullptr;
#else
#define GLFN_DECLARE(type, name) inline type name = nullptr;
#endif
GL_FUNCTION_LIST(GLFN_DECLARE)
#undef GLFN_DECLARE

// Remplit tous les pointeurs. Un contexte OpenGL doit être actif.
inline bool load()
{
    bool ok = true;
#define GLFN_LOAD(type, name)                                                  \
    name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!name) {                                                             \
        SDL_Log("Fonction OpenGL absente : gl%s", #name);                    \
        ok = false;                                                          \
    }
    GL_FUNCTION_LIST(GLFN_LOAD)
#undef GLFN_LOAD
    return ok;
}

} // namespace gl
