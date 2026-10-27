#include "Video.hpp"

#include "GlFunctions.hpp"
#include "Utf8.hpp"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

// Video.hpp stocke les identifiants OpenGL en "unsigned" pour ne pas inclure
// les en-têtes OpenGL partout. On vérifie ici, à la compilation, que c'est
// bien le même type que GLuint.
static_assert(std::is_same_v<GLuint, unsigned>, "GLuint doit être un unsigned int");

namespace {

// L'instance active, pour les callbacks statiques donnés au core.
Video* g_video = nullptr;

// [C++] R"(...)" est une "chaîne brute" : pas besoin d'échapper les retours
// à la ligne ni les guillemets. Pratique pour écrire des shaders.
//
// Le vertex shader fabrique un rectangle (2 triangles) qui couvre toute la
// zone de dessin, sans aucun buffer de sommets : gl_VertexID vaut 0, 1, 2, 3
// et donne les coins (0,0) (1,0) (0,1) (1,1).
// (OpenGL ES : même langage, mais une autre première ligne, et la précision
// des nombres à virgule à choisir.)
#ifdef NEXO_GLES
#define NEXO_GLSL_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define NEXO_GLSL_HEADER "#version 330 core\n"
#endif
const char* const kVertexShader = NEXO_GLSL_HEADER R"(
uniform vec2 uvScale;  // partie utile de la texture (le core n'utilise pas toujours tout)
uniform float flipY;   // 1.0 si la première ligne de la texture est le HAUT de l'image
out vec2 uv;
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
    uv = vec2(corner.x, mix(corner.y, 1.0 - corner.y, flipY)) * uvScale;
}
)";

// swapRB : OpenGL ES ne sait pas lire les pixels XRGB8888 (rangés B, V, R en
// mémoire) : on les envoie tels quels et c'est le shader qui échange R et B.
const char* const kFragmentShader = NEXO_GLSL_HEADER R"(
uniform sampler2D frame;
uniform float swapRB;
in vec2 uv;
out vec4 color;
void main()
{
    vec3 c = texture(frame, uv).rgb;
    color = vec4(mix(c, c.bgr, swapRB), 1.0);
}
)";

// Comment décrire chaque format de pixels libretro à OpenGL.
struct GlPixelFormat {
    GLint internalFormat;
    GLenum format;
    GLenum type;
    unsigned bytesPerPixel;
};

GlPixelFormat toGl(retro_pixel_format format)
{
#ifdef NEXO_GLES
    // XRGB8888 : lu comme du RGBA (le shader remet les couleurs dans l'ordre) ;
    // 0RGB1555 : converti en RGB565 avant l'envoi (voir Video::upload).
    if (format == RETRO_PIXEL_FORMAT_XRGB8888)
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
    return {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2};
#else
    switch (format) {
    case RETRO_PIXEL_FORMAT_XRGB8888: return {GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4};
    case RETRO_PIXEL_FORMAT_RGB565: return {GL_RGB8, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2};
    default: return {GL_RGBA8, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 2}; // 0RGB1555
    }
#endif
}

GLuint compileShader(GLenum type, const char* source)
{
    const GLuint shader = gl::CreateShader(type);
    gl::ShaderSource(shader, 1, &source, nullptr);
    gl::CompileShader(shader);
    GLint ok = GL_FALSE;
    gl::GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_Log("Erreur de compilation du shader : %s", log);
        gl::DeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Video::~Video()
{
    destroyContext();
    if (m_window)
        SDL_DestroyWindow(m_window);
    if (g_video == this)
        g_video = nullptr;
}

bool Video::open(const char* title, int width, int height, bool autoSize)
{
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef NEXO_GLES
    // Android choisit le format de l'écran à la création de la fenêtre : il
    // doit déjà savoir qu'on dessinera avec OpenGL ES 3.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
#ifdef __ANDROID__
    // Android : tout l'écran, sans la barre d'état ni les boutons du système
    // (ils réapparaissent en glissant le doigt depuis le bord).
    m_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN);
#else
    m_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
#endif
    if (!m_window) {
        SDL_Log("Création de la fenêtre impossible : %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(m_window, 640, 400);
    m_windowSized = !autoSize;
    g_video = this;
    return true;
}

void Video::destroyContext()
{
    if (m_context) {
        SDL_GL_MakeCurrent(m_window, m_context);
        destroyFramebuffer();
        if (m_coreContext) {
            SDL_GL_DestroyContext(m_coreContext);
            m_coreContext = nullptr;
            SDL_GL_MakeCurrent(m_window, m_context);
        }
        if (m_frameTexture)
            glDeleteTextures(1, &m_frameTexture);
        if (m_vao)
            gl::DeleteVertexArrays(1, &m_vao);
        if (m_program)
            gl::DeleteProgram(m_program);
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    m_frameTexture = m_vao = m_program = 0;
    m_fboWidth = m_fboHeight = 0;

    // Retour aux valeurs par défaut pour le prochain core.
    m_hw = {};
    m_hwRender = false;
    m_hwContextAlive = false;
    m_vulkan.destroy();
    m_vulkan.setNegotiation(nullptr);
    m_hwVulkan = false;
    m_pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
    m_textureFormat = RETRO_PIXEL_FORMAT_UNKNOWN;
    m_textureWidth = m_textureHeight = 0;
    m_hasFrame = false;
    m_frameWidth = m_frameHeight = 0;
    m_aspect = 0.0f;
}

void Video::setTitle(const char* title)
{
    SDL_SetWindowTitle(m_window, title);
}

void Video::toggleFullscreen()
{
    const bool fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(m_window, !fullscreen);
}

void Video::setFullscreen(bool fullscreen)
{
    SDL_SetWindowFullscreen(m_window, fullscreen);
}

bool Video::setPixelFormat(retro_pixel_format format)
{
    switch (format) {
    case RETRO_PIXEL_FORMAT_0RGB1555:
    case RETRO_PIXEL_FORMAT_XRGB8888:
    case RETRO_PIXEL_FORMAT_RGB565:
        m_pixelFormat = format;
        return true;
    default:
        return false;
    }
}

bool Video::requestHwRender(retro_hw_render_callback& callback)
{
    switch (callback.context_type) {
#ifdef NEXO_GLES
    // Android : les cores sont compilés pour OpenGL ES, comme notre fenêtre.
    case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
        break;
#else
    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
        break;
#endif
    case RETRO_HW_CONTEXT_VULKAN:
        // Le core dessine avec Vulkan : notre fenêtre reste en OpenGL (pour
        // l'interface), et VulkanRender recopie ses images (voir createContext).
        m_hw = callback;
        m_hwRender = true;
        m_hwVulkan = true;
        // (Pour Vulkan, version_major contient une version Vulkan "codée" :
        // VK_API_VERSION_1_1, etc.)
        SDL_Log("Le core demande Vulkan %u.%u", VK_API_VERSION_MAJOR(callback.version_major),
                VK_API_VERSION_MINOR(callback.version_major));
        return true;
    default:
        // Direct3D, OpenGL ES : pas encore. En répondant false, le core
        // essaie souvent une autre méthode de rendu.
        SDL_Log("Type de rendu matériel non supporté : %d", static_cast<int>(callback.context_type));
        return false;
    }

    // On complète la structure avec nos propres fonctions...
    callback.get_current_framebuffer = &Video::getCurrentFramebuffer;
    callback.get_proc_address = &Video::getProcAddress;
    // ...et on en garde une copie pour appeler context_reset plus tard.
    m_hw = callback;
    m_hwRender = true;
    SDL_Log("Le core demande OpenGL %s %u.%u",
            callback.context_type == RETRO_HW_CONTEXT_OPENGL_CORE ? "core"
            : callback.context_type == RETRO_HW_CONTEXT_OPENGL    ? "compat"
                                                                   : "ES",
            callback.version_major, callback.version_minor);
    return true;
}

bool Video::createContext(const retro_game_geometry& geometry)
{
    // Notre propre shader a besoin d'OpenGL 3.3 minimum.
    const bool glCore = m_hwRender && !m_hwVulkan; // le core dessine-t-il en OpenGL ?
    if (glCore && m_hw.debug_context)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#ifdef NEXO_GLES
    // OpenGL ES : la plus récente des versions 3.x (3.2 pour les cores qui
    // s'en servent, 3.0 au minimum pour notre interface).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    for (int minor : {2, 1, 0}) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        m_context = SDL_GL_CreateContext(m_window);
        if (m_context)
            break;
    }
#else
    if (glCore && m_hw.context_type == RETRO_HW_CONTEXT_OPENGL) {
        // Profil "compatibilité" : l'ancien OpenGL reste disponible. Sans
        // version précise, les pilotes donnent la plus récente possible.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        m_context = SDL_GL_CreateContext(m_window);
    } else {
        // Profil "core" : la version minimale demandée (3.3 pour nous). Mais
        // un core qui demande 3.3 se sert souvent de fonctions plus récentes
        // quand la carte les a (PCSX2 : les "storage buffers" d'OpenGL 4.3).
        // Beaucoup de pilotes donnent d'eux-mêmes la version la plus récente ;
        // celui d'AMD donne exactement ce qu'on demande, et les shaders de
        // PCSX2 ne compilaient plus. On demande donc la plus récente possible,
        // en descendant jusqu'au minimum (une 4.6 sait tout faire d'une 3.3).
        unsigned minMajor = 3;
        unsigned minMinor = 3;
        if (glCore && (m_hw.version_major > 3 || (m_hw.version_major == 3 && m_hw.version_minor > 3))) {
            minMajor = m_hw.version_major;
            minMinor = m_hw.version_minor;
        }
        static const unsigned versions[][2] = {{4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0}, {3, 3}};
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        for (const auto& v : versions) {
            if (v[0] < minMajor || (v[0] == minMajor && v[1] < minMinor))
                break; // en dessous du minimum demandé
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, static_cast<int>(v[0]));
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, static_cast<int>(v[1]));
            m_context = SDL_GL_CreateContext(m_window);
            if (m_context)
                break;
        }
    }
#endif

    if (!m_context) {
        SDL_Log("Création du contexte OpenGL impossible : %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_context);
    // Pas de VSync : c'est la boucle principale qui cadence au rythme exact
    // de la console (60,0988 images/s pour une SNES, pas 60).
    SDL_GL_SetSwapInterval(0);

    SDL_Log("OpenGL %s sur %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    if (!gl::load() || !createProgram())
        return false;

    if (m_hwVulkan) {
        // Le device Vulkan du core, puis "ton contexte est prêt".
        if (!m_vulkan.create()) {
            SDL_Log("Vulkan : %s", m_vulkan.error().c_str());
            return false;
        }
        if (m_hw.context_reset)
            m_hw.context_reset();
        m_hwContextAlive = true;
    } else if (m_hwRender) {
        // Un contexte rien qu'au core, partagé avec le nôtre. Sans lui, le
        // core et notre interface se partageaient les réglages d'OpenGL :
        // Dolphin, qui retient l'état qu'il croit avoir laissé, dessinait
        // alors un écran noir ou violet. (RetroArch fait de même.)
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
        m_coreContext = SDL_GL_CreateContext(m_window);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
        if (!m_coreContext)
            SDL_Log("Contexte OpenGL séparé impossible (%s) : le core partagera le nôtre", SDL_GetError());
        SDL_GL_MakeCurrent(m_window, m_context);
        if (!createFramebuffer(geometry.max_width, geometry.max_height))
            return false;
        // "Ton contexte est prêt" : le core crée ses textures, shaders...
        beginCore();
        if (m_hw.context_reset)
            m_hw.context_reset();
        endCore();
        m_hwContextAlive = true;
    }
    return true;
}

void Video::beginCore()
{
    if (m_coreContext)
        SDL_GL_MakeCurrent(m_window, m_coreContext);
}

void Video::endCore()
{
    if (!m_coreContext)
        return;
    // Les commandes du core doivent être parties vers la carte graphique
    // avant que notre contexte ne lise son image.
    glFlush();
    SDL_GL_MakeCurrent(m_window, m_context);
}

void Video::destroyHwContext()
{
    if (m_hwContextAlive && m_hw.context_destroy) {
        SDL_GL_MakeCurrent(m_window, m_coreContext ? m_coreContext : m_context);
        m_hw.context_destroy();
        SDL_GL_MakeCurrent(m_window, m_context);
    }
    m_hwContextAlive = false;
    if (m_hwVulkan)
        m_vulkan.destroy(); // après context_destroy : le core a rendu ses ressources
}

bool Video::createProgram()
{
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vertex || !fragment)
        return false;

    m_program = gl::CreateProgram();
    gl::AttachShader(m_program, vertex);
    gl::AttachShader(m_program, fragment);
    gl::LinkProgram(m_program);
    // Une fois liés dans le programme, les shaders séparés ne servent plus.
    gl::DeleteShader(vertex);
    gl::DeleteShader(fragment);

    GLint ok = GL_FALSE;
    gl::GetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        SDL_Log("Erreur d'édition de liens du shader : %s", log);
        return false;
    }

    gl::UseProgram(m_program);
    gl::Uniform1i(gl::GetUniformLocation(m_program, "frame"), 0); // texture n°0
    m_uvScaleLocation = gl::GetUniformLocation(m_program, "uvScale");
    m_flipYLocation = gl::GetUniformLocation(m_program, "flipY");
    m_swapRBLocation = gl::GetUniformLocation(m_program, "swapRB");
    gl::UseProgram(0);

    // OpenGL "core" exige un Vertex Array Object pour dessiner, même vide.
    gl::GenVertexArrays(1, &m_vao);

    glGenTextures(1, &m_frameTexture);
    glBindTexture(GL_TEXTURE_2D, m_frameTexture);
    // Agrandissement "au plus proche voisin" : des pixels nets, pas flous.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool Video::createFramebuffer(unsigned width, unsigned height)
{
    // Le framebuffer est créé dans le contexte du core (un framebuffer
    // n'est PAS partagé entre contextes, contrairement aux textures).
    SDL_GLContext previous = SDL_GL_GetCurrentContext();
    beginCore();

    // La texture qui recevra l'image dessinée par le core.
    glGenTextures(1, &m_fboTexture);
    glBindTexture(GL_TEXTURE_2D, m_fboTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // Image 3D souvent agrandie : un filtrage linéaire la lisse un peu.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl::GenFramebuffers(1, &m_fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fboTexture, 0);

    // Tampon de profondeur (et de stencil) si le core en veut : sans lui,
    // les objets 3D lointains s'afficheraient devant les proches.
    if (m_hw.depth) {
        gl::GenRenderbuffers(1, &m_fboDepth);
        gl::BindRenderbuffer(GL_RENDERBUFFER, m_fboDepth);
        gl::RenderbufferStorage(GL_RENDERBUFFER, m_hw.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
                                static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, m_hw.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                    GL_RENDERBUFFER, m_fboDepth);
        gl::BindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (m_coreContext)
        glFlush();

    // Dans NOTRE contexte, un second framebuffer sur la même texture : c'est
    // lui qu'on copie à l'écran.
    SDL_GL_MakeCurrent(m_window, m_context);
    gl::GenFramebuffers(1, &m_uiFbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fboTexture, 0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (previous)
        SDL_GL_MakeCurrent(m_window, previous);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("Framebuffer incomplet : 0x%x", status);
        return false;
    }

    m_fboWidth = width;
    m_fboHeight = height;
    SDL_Log("Framebuffer de rendu : %ux%u", width, height);
    return true;
}

void Video::destroyFramebuffer()
{
    SDL_GLContext previous = SDL_GL_GetCurrentContext();
    if (m_uiFbo) {
        SDL_GL_MakeCurrent(m_window, m_context);
        gl::DeleteFramebuffers(1, &m_uiFbo);
    }
    beginCore(); // le framebuffer du core vit dans son contexte
    if (m_fbo)
        gl::DeleteFramebuffers(1, &m_fbo);
    if (m_fboDepth)
        gl::DeleteRenderbuffers(1, &m_fboDepth);
    if (m_fboTexture)
        glDeleteTextures(1, &m_fboTexture);
    m_fbo = m_fboDepth = m_fboTexture = m_uiFbo = 0;
    if (previous)
        SDL_GL_MakeCurrent(m_window, previous);
}

void Video::setGeometry(const retro_game_geometry& geometry)
{
    m_aspect = geometry.aspect_ratio > 0.0f
                   ? geometry.aspect_ratio
                   : static_cast<float>(geometry.base_width) / static_cast<float>(geometry.base_height);

    // Si le jeu passe à une résolution plus grande que prévu, on agrandit le
    // framebuffer prêté au core.
    if (m_hwRender && m_context && (geometry.max_width > m_fboWidth || geometry.max_height > m_fboHeight)) {
        destroyFramebuffer();
        createFramebuffer(std::max(geometry.max_width, m_fboWidth), std::max(geometry.max_height, m_fboHeight));
    }

    // Au premier lancement, on agrandit la fenêtre (3x au plus), sans
    // dépasser 80 % de la hauteur de l'écran.
    if (m_window && !m_windowSized && geometry.base_height > 0) {
        SDL_Rect usable{};
        int scale = 3;
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(m_window), &usable))
            scale = std::clamp(static_cast<int>(usable.h * 0.8f) / static_cast<int>(geometry.base_height), 1, 3);
        const int height = static_cast<int>(geometry.base_height) * scale;
        SDL_SetWindowSize(m_window, static_cast<int>(static_cast<float>(height) * m_aspect), height);
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        m_windowSized = true;
    }
}

void Video::upload(const void* pixels, unsigned width, unsigned height, size_t pitch)
{
    const GlPixelFormat fmt = toGl(m_pixelFormat);
#ifdef NEXO_GLES
    if (m_pixelFormat == RETRO_PIXEL_FORMAT_0RGB1555) {
        // OpenGL ES ne lit pas ce format : chaque pixel 0RRRRRVVVVVBBBBB
        // devient un RRRRRVVVVVVBBBBB (le vert gagne un bit, recopié du plus fort).
        m_converted.resize(size_t(width) * height);
        for (unsigned y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(pixels) + y * pitch);
            uint16_t* out = &m_converted[size_t(y) * width];
            for (unsigned x = 0; x < width; ++x) {
                const uint16_t p = row[x];
                const uint16_t g = (p >> 5) & 0x1F;
                out[x] = static_cast<uint16_t>(((p & 0x7C00) << 1) | (g << 6) | ((g >> 4) << 5) | (p & 0x1F));
            }
        }
        pixels = m_converted.data();
        pitch = size_t(width) * 2;
    }
#endif
    glBindTexture(GL_TEXTURE_2D, m_frameTexture);
    // pitch = nombre d'octets entre deux lignes (peut dépasser width * bpp).
    // OpenGL veut cette valeur en pixels.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pitch / fmt.bytesPerPixel));

    // La résolution peut changer en cours de jeu (menus, cinématiques...) :
    // on réalloue alors la texture, sinon on remplace juste son contenu.
    if (width != m_textureWidth || height != m_textureHeight || m_pixelFormat != m_textureFormat) {
        glTexImage2D(GL_TEXTURE_2D, 0, fmt.internalFormat, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                     0, fmt.format, fmt.type, pixels);
        m_textureWidth = width;
        m_textureHeight = height;
        m_textureFormat = m_pixelFormat;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), fmt.format,
                        fmt.type, pixels);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_hasFrame = true;
    m_frameIsHw = false;
    m_frameWidth = width;
    m_frameHeight = height;
}

void Video::hwFrameReady(unsigned width, unsigned height)
{
    if (m_hwVulkan) {
        // L'image Vulkan du core, recopiée en mémoire puis envoyée dans notre
        // texture OpenGL comme une image 2D (32 bits, sans retournement).
        if (m_vulkan.readFrame(width, height, m_vulkanPixels)) {
            m_pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
            upload(m_vulkanPixels.data(), width, height, size_t(width) * 4);
        }
        return;
    }
    if (SDL_getenv("EMU_CORE_LOG")) {
        // Diagnostic : la couleur au centre de notre framebuffer, juste après
        // le dessin du core (noir partout = le core n'y a rien dessiné).
        static int calls = 0;
        if (++calls % 300 == 0) {
            GLint previous = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
            gl::BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            unsigned char px[4] = {};
            glReadPixels(static_cast<GLint>(width / 2), static_cast<GLint>(height / 2), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            SDL_Log("Framebuffer après le core (lié : %d, le nôtre : %u) : centre = %u,%u,%u", previous, m_fbo, px[0], px[1],
                    px[2]);
            gl::BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
        }
    }
    m_hasFrame = true;
    m_frameIsHw = true;
    m_frameWidth = width;
    m_frameHeight = height;
}

void Video::resetGlState()
{
    // Les cores 3D dessinent dans NOTRE contexte OpenGL et y laissent leurs
    // réglages. Tout ce qui pourrait fausser notre dessin (ou celui de
    // l'interface) est remis à l'état par défaut. Dolphin, par exemple,
    // émule les "opérations logiques" de la GameCube : laissées actives,
    // elles transformaient l'image du jeu en écran noir.
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
#ifndef NEXO_GLES // (OpenGL ES n'a ni opérations logiques ni glPolygonMode)
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(0x8DB9); // GL_FRAMEBUFFER_SRGB : pas de conversion de couleurs
    glDisable(0x8C89); // GL_RASTERIZER_DISCARD : sinon rien n'est dessiné
    glDisable(0x8F9D); // GL_PRIMITIVE_RESTART
    glDisable(0x8E51); // GL_SAMPLE_MASK
    for (GLenum i = 0; i < 8; ++i)
        glDisable(0x3000 + i); // GL_CLIP_DISTANCE0..7
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#else
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_RASTERIZER_DISCARD);
#endif
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // Un "sampler" lié à l'unité 0 remplacerait les réglages de nos textures
    // (un filtrage avec mipmaps sur une texture qui n'en a pas = noir).
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindSampler(0, 0);
    // Un tampon de pixels lié détournerait nos glTexSubImage2D.
    gl::BindBuffer(0x88EC, 0); // GL_PIXEL_UNPACK_BUFFER
}

void Video::renderGame()
{
    if (!m_context)
        return;

    int outW = 0;
    int outH = 0;
    SDL_GetWindowSizeInPixels(m_window, &outW, &outH);

    // Le core a pu modifier l'état d'OpenGL à sa guise : on remet ce dont on
    // a besoin dans un état connu avant de dessiner.
    resetGlState();
    glViewport(0, 0, outW, outH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_hasFrame && m_frameWidth > 0 && m_frameHeight > 0) {
        // On remplit la fenêtre au maximum sans déformer l'image (bandes
        // noires sur les côtés ou en haut/bas si besoin).
        float aspect = m_aspect > 0.0f ? m_aspect : static_cast<float>(m_frameWidth) / m_frameHeight;
        if (m_aspectOverride > 0.0f)
            aspect = m_aspectOverride;
        float w = static_cast<float>(outW);
        float h = w / aspect;
        if (h > static_cast<float>(outH)) {
            h = static_cast<float>(outH);
            w = h * aspect;
        }
        const GLint x0 = static_cast<GLint>((outW - w) / 2.0f);
        const GLint y0 = static_cast<GLint>((outH - h) / 2.0f);
        glViewport(x0, y0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        // Retenu en points SDL (comme la souris) : l'image est centrée, donc
        // le même calcul vaut en partant du haut de la fenêtre.
        const float density = SDL_GetWindowPixelDensity(m_window) > 0.0f ? SDL_GetWindowPixelDensity(m_window) : 1.0f;
        m_gameRect[0] = static_cast<float>(x0) / density;
        m_gameRect[1] = static_cast<float>(y0) / density;
        m_gameRect[2] = w / density;
        m_gameRect[3] = h / density;
        m_gamePixels[0] = x0;
        m_gamePixels[1] = y0;
        m_gamePixels[2] = static_cast<int>(w);
        m_gamePixels[3] = static_cast<int>(h);

        if (m_frameIsHw) {
            // Image dessinée par le core dans notre framebuffer : copiée
            // directement à l'écran ("blit"). Contrairement à un dessin avec
            // notre shader, une copie ne dépend presque pas des réglages
            // qu'un core laisse derrière lui (Dolphin : écran noir sinon).
            // OpenGL range ses lignes de bas en haut : on retourne l'image en
            // échangeant le haut et le bas de la destination, sauf si le core
            // dit avoir déjà dessiné dans ce sens.
            const GLint x1 = x0 + static_cast<GLint>(w);
            const GLint y1 = y0 + static_cast<GLint>(h);
            const bool flip = !m_hw.bottom_left_origin;
            gl::BindFramebuffer(0x8CA8, m_uiFbo); // GL_READ_FRAMEBUFFER (notre copie du framebuffer du core)
            gl::BindFramebuffer(0x8CA9, 0);     // GL_DRAW_FRAMEBUFFER
            gl::BlitFramebuffer(0, 0, static_cast<GLint>(m_frameWidth), static_cast<GLint>(m_frameHeight), x0,
                                flip ? y1 : y0, x1, flip ? y0 : y1, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            if (SDL_getenv("EMU_CORE_LOG")) {
                static int calls = 0;
                if (++calls % 300 == 0) {
                    unsigned char src[4] = {}, dst[4] = {};
                    glReadPixels(static_cast<GLint>(m_frameWidth / 2), static_cast<GLint>(m_frameHeight / 2), 1, 1, GL_RGBA,
                                 GL_UNSIGNED_BYTE, src); // (lu dans le framebuffer du core, lié en lecture)
                    gl::BindFramebuffer(0x8CA8, 0);
                    glReadPixels(outW / 2, outH / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, dst);
                    SDL_Log("Copie à l'écran : source %u,%u,%u -> écran %u,%u,%u (erreur GL 0x%x, %ux%u dans %ux%u)", src[0],
                            src[1], src[2], dst[0], dst[1], dst[2], glGetError(), m_frameWidth, m_frameHeight, m_fboWidth,
                            m_fboHeight);
                }
            }
            gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, outW, outH);
            return;
        }

        gl::UseProgram(m_program);
        gl::BindVertexArray(m_vao);
        gl::ActiveTexture(GL_TEXTURE0);
        if (m_frameIsHw) {
            // Le core n'a dessiné que dans un coin du framebuffer.
            glBindTexture(GL_TEXTURE_2D, m_fboTexture);
            gl::Uniform2f(m_uvScaleLocation, static_cast<float>(m_frameWidth) / m_fboWidth,
                          static_cast<float>(m_frameHeight) / m_fboHeight);
            // OpenGL range les lignes de bas en haut, sauf si le core dit
            // avoir dessiné à l'envers.
            gl::Uniform1f(m_flipYLocation, m_hw.bottom_left_origin ? 0.0f : 1.0f);
        } else {
            // Les pixels logiciels arrivent ligne du haut en premier.
            glBindTexture(GL_TEXTURE_2D, m_frameTexture);
            // Réglage "Agrandissement" : pixels nets ou image lissée.
            const GLint filter = m_smooth ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            gl::Uniform2f(m_uvScaleLocation, 1.0f, 1.0f);
            gl::Uniform1f(m_flipYLocation, 1.0f);
#ifdef NEXO_GLES
            gl::Uniform1f(m_swapRBLocation, m_textureFormat == RETRO_PIXEL_FORMAT_XRGB8888 ? 1.0f : 0.0f);
#endif
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glBindTexture(GL_TEXTURE_2D, 0);
        gl::BindVertexArray(0);
        gl::UseProgram(0);
    }

    // L'interface dessinera sur toute la fenêtre.
    glViewport(0, 0, outW, outH);
}

void Video::swap()
{
    if (!m_context)
        return;
    if (!m_screenshotPath.empty()) {
        int outW = 0;
        int outH = 0;
        SDL_GetWindowSizeInPixels(m_window, &outW, &outH);
        saveScreenshot(outW, outH);
    }
    SDL_GL_SwapWindow(m_window);
}

bool Video::readGameArea(std::vector<uint8_t>& out, int& width, int& height)
{
    if (!m_context || !m_hasFrame || m_gamePixels[2] <= 0 || m_gamePixels[3] <= 0)
        return false;
    width = m_gamePixels[2];
    height = m_gamePixels[3];
    out.resize(static_cast<size_t>(width) * height * 4);
    gl::BindFramebuffer(0x8CA8, 0); // GL_READ_FRAMEBUFFER : l'image de la fenêtre
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
#ifdef NEXO_GLES
    glReadPixels(m_gamePixels[0], m_gamePixels[1], width, height, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
#else
    // BGRA : l'ordre attendu par l'encodeur de Windows (pas de conversion à faire).
    glReadPixels(m_gamePixels[0], m_gamePixels[1], width, height, GL_BGRA, GL_UNSIGNED_BYTE, out.data());
#endif
    return true;
}

void Video::saveScreenshot(int width, int height)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL lit l'image de bas en haut : on remet les lignes dans l'ordre.
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> flipped(pixels.size());
    for (int y = 0; y < height; ++y)
        std::memcpy(&flipped[y * rowBytes], &pixels[(height - 1 - y) * rowBytes], rowBytes);

    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, flipped.data(),
                                                 static_cast<int>(rowBytes));
    const std::string path = toUtf8(m_screenshotPath);
    if (surface && SDL_SaveBMP(surface, path.c_str()))
        SDL_Log("Capture d'écran : %s", path.c_str());
    else
        SDL_Log("Capture d'écran impossible : %s", SDL_GetError());
    SDL_DestroySurface(surface);
    m_screenshotPath.clear();
}

uintptr_t Video::getCurrentFramebuffer()
{
    return g_video ? g_video->m_fbo : 0;
}

retro_proc_address_t Video::getProcAddress(const char* symbol)
{
    return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(symbol));
}
