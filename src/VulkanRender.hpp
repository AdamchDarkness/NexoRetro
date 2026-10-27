#pragma once
// VulkanRender : le rendu matériel Vulkan pour les cores qui le demandent
// (ParaLLEl-RDP pour la N64, Beetle PSX HW, PPSSPP, Dolphin...).
//
// Notre interface est dessinée avec OpenGL. Pour un core Vulkan :
//  1. on crée un "device" Vulkan (la carte graphique vue par Vulkan), ou on
//     laisse le core le créer lui-même s'il a des besoins particuliers
//     (interface de "négociation") ;
//  2. le core dessine chaque image dans SA propre image Vulkan, puis nous
//     la confie avec set_image() ;
//  3. on recopie cette image dans la mémoire du PC, d'où Video l'envoie dans
//     une texture OpenGL, comme une image de console 2D.
//
// La recopie coûte un peu (quelques Mo par image), mais elle évite de mêler
// Vulkan et OpenGL dans la même fenêtre. Largement suffisant en résolution
// native ; c'est la piste d'optimisation si un jour on rend en 4K.
//
// vulkan-1.dll (installée avec le pilote graphique) est chargée à
// l'exécution : sans elle, seuls les cores Vulkan sont indisponibles.

#define VK_NO_PROTOTYPES // on charge nous-mêmes les fonctions Vulkan
#include <libretro_vulkan.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class VulkanRender {
public:
    VulkanRender() = default;
    ~VulkanRender();
    VulkanRender(const VulkanRender&) = delete;
    VulkanRender& operator=(const VulkanRender&) = delete;

    // Vulkan est-il installé sur ce PC (vulkan-1.dll, fournie par le pilote) ?
    static bool available();

    // Le core décrit comment il veut créer son device (facultatif).
    void setNegotiation(const retro_hw_render_context_negotiation_interface_vulkan* negotiation)
    {
        m_negotiation = negotiation;
    }
    // Crée l'instance et le device. false (avec error()) si Vulkan est absent.
    bool create();
    void destroy();
    bool ready() const { return m_device != VK_NULL_HANDLE; }
    const std::string& error() const { return m_error; }
    const std::string& gpuName() const { return m_gpuName; }

    // Ce que GET_HW_RENDER_INTERFACE donne au core.
    retro_hw_render_interface_vulkan* interface() { return ready() ? &m_interface : nullptr; }

    // Recopie la dernière image confiée par le core, au format XRGB8888
    // (octets B, G, R, X : celui des cores 2D en 32 bits).
    bool readFrame(unsigned width, unsigned height, std::vector<uint32_t>& pixels);

private:
    // Callbacks donnés au core (handle = this).
    static void setImage(void* handle, const retro_vulkan_image* image, uint32_t numSemaphores,
                         const VkSemaphore* semaphores, uint32_t srcQueueFamily);
    static uint32_t getSyncIndex(void* handle);
    static uint32_t getSyncIndexMask(void* handle);
    static void setCommandBuffers(void* handle, uint32_t num, const VkCommandBuffer* buffers);
    static void waitSyncIndex(void* handle);
    static void lockQueue(void* handle);
    static void unlockQueue(void* handle);
    static void setSignalSemaphore(void* handle, VkSemaphore semaphore);
    // Ce que le core appelle à la place de vkCreateInstance / vkCreateDevice.
    static VkInstance createInstanceWrapper(void* opaque, const VkInstanceCreateInfo* info);
    static VkDevice createDeviceWrapper(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info);

    bool loadLibrary();
    bool createDefaultDevice();
    bool ensureStaging(VkDeviceSize size);
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;

    const retro_hw_render_context_negotiation_interface_vulkan* m_negotiation = nullptr;
    bool m_negotiatedDevice = false;
    std::string m_error;
    std::string m_gpuName;

    void* m_library = nullptr; // vulkan-1.dll (Windows) ou libvulkan.so (Android)
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_gpu = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    retro_hw_render_interface_vulkan m_interface{};
    std::mutex m_queueMutex; // lock_queue / unlock_queue

    // Fonctions Vulkan chargées à la main (VK_NO_PROTOTYPES).
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkDestroyInstance vkDestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkCreateDevice vkCreateDevice = nullptr;
    PFN_vkDestroyDevice vkDestroyDevice = nullptr;
    PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
    PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
    PFN_vkQueueSubmit vkQueueSubmit = nullptr;
    PFN_vkCreateFence vkCreateFence = nullptr;
    PFN_vkDestroyFence vkDestroyFence = nullptr;
    PFN_vkWaitForFences vkWaitForFences = nullptr;
    PFN_vkResetFences vkResetFences = nullptr;
    PFN_vkCreateBuffer vkCreateBuffer = nullptr;
    PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory vkAllocateMemory = nullptr;
    PFN_vkFreeMemory vkFreeMemory = nullptr;
    PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
    PFN_vkMapMemory vkMapMemory = nullptr;
    PFN_vkUnmapMemory vkUnmapMemory = nullptr;

    // La recopie : une "commande" GPU, une barrière d'attente (fence) et un
    // tampon lisible par le processeur (staging).
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkBuffer m_staging = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize m_stagingSize = 0;
    void* m_stagingMap = nullptr;

    // Ce que le core nous a confié pour l'image en cours.
    bool m_hasImage = false;
    retro_vulkan_image m_image{};
    uint32_t m_imageQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    std::vector<VkSemaphore> m_waitSemaphores;
    std::vector<VkCommandBuffer> m_coreCommands;
    VkSemaphore m_signalSemaphore = VK_NULL_HANDLE;
    bool m_warnedFormat = false;
    bool m_warnedNoImage = false; // messages de diagnostic, une seule fois
    bool m_loggedFirst = false;
};
