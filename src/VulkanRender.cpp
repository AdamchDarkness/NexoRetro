#include "VulkanRender.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstring>

namespace {

// Nos infos d'application, si le core ne donne pas les siennes.
const VkApplicationInfo kAppInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Nexo Retro", 1, "Nexo Retro", 1,
                                 VK_API_VERSION_1_1};

} // namespace

VulkanRender::~VulkanRender()
{
    destroy();
}

// --- Chargement de vulkan-1.dll -----------------------------------------------------------

bool VulkanRender::available()
{
    // Vérifié une seule fois : la DLL reste chargée (Windows la partage).
    static const bool present = LoadLibraryW(L"vulkan-1.dll") != nullptr;
    return present;
}

bool VulkanRender::loadLibrary()
{
    if (!m_library)
        m_library = LoadLibraryW(L"vulkan-1.dll");
    if (!m_library) {
        m_error = "Vulkan n'est pas disponible sur ce PC (pilote graphique trop ancien ?)";
        return false;
    }
    // [C++] GetProcAddress renvoie un pointeur de fonction "générique" : on
    // le convertit vers le bon type avec reinterpret_cast.
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(static_cast<HMODULE>(m_library), "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) {
        m_error = "vulkan-1.dll est incomplète";
        return false;
    }
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    return vkCreateInstance != nullptr;
}

VkInstance VulkanRender::createInstanceWrapper(void* opaque, const VkInstanceCreateInfo* info)
{
    // Le core a préparé sa demande (extensions...) ; c'est nous qui créons.
    auto* self = static_cast<VulkanRender*>(opaque);
    VkInstance instance = VK_NULL_HANDLE;
    if (self->vkCreateInstance(info, nullptr, &instance) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return instance;
}

VkDevice VulkanRender::createDeviceWrapper(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info)
{
    auto* self = static_cast<VulkanRender*>(opaque);
    VkDevice device = VK_NULL_HANDLE;
    if (self->vkCreateDevice(gpu, info, nullptr, &device) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return device;
}

// --- Création ----------------------------------------------------------------------------------

bool VulkanRender::create()
{
    destroy();
    if (!loadLibrary())
        return false;
    const bool v2 = m_negotiation && m_negotiation->interface_version >= 2;
    const VkApplicationInfo* app =
        m_negotiation && m_negotiation->get_application_info ? m_negotiation->get_application_info() : nullptr;
    if (!app)
        app = &kAppInfo;

    // 1. L'instance : la porte d'entrée de Vulkan.
    if (v2 && m_negotiation->create_instance) {
        m_instance = m_negotiation->create_instance(vkGetInstanceProcAddr, app, &createInstanceWrapper, this);
    } else {
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = app;
        if (vkCreateInstance(&info, nullptr, &m_instance) != VK_SUCCESS)
            m_instance = VK_NULL_HANDLE;
    }
    if (!m_instance) {
        m_error = "Création de l'instance Vulkan impossible";
        return false;
    }
    // Les fonctions "d'instance", maintenant qu'on en a une.
#define LOAD_INSTANCE(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(m_instance, #name))
    LOAD_INSTANCE(vkDestroyInstance);
    LOAD_INSTANCE(vkEnumeratePhysicalDevices);
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    LOAD_INSTANCE(vkCreateDevice);
    LOAD_INSTANCE(vkGetDeviceProcAddr);
#undef LOAD_INSTANCE

    // 2. La carte graphique : de préférence une carte "dédiée" (pas celle
    //    intégrée au processeur, moins puissante).
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(m_instance, &count, gpus.data());
    if (gpus.empty()) {
        m_error = "Aucune carte graphique Vulkan";
        return false;
    }
    m_gpu = gpus.front();
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_gpu = gpu;
            break;
        }
    }

    // 3. Le device et sa file de commandes : par le core s'il le souhaite
    //    (il active alors les fonctions dont il a besoin), sinon par nous.
    retro_vulkan_context context{};
    bool created = false;
    if (v2 && m_negotiation->create_device2)
        created = m_negotiation->create_device2(&context, m_instance, m_gpu, VK_NULL_HANDLE, vkGetInstanceProcAddr,
                                                &createDeviceWrapper, this);
    else if (m_negotiation && m_negotiation->create_device)
        created = m_negotiation->create_device(&context, m_instance, m_gpu, VK_NULL_HANDLE, vkGetInstanceProcAddr, nullptr,
                                               0, nullptr, 0, nullptr);
    if (created) {
        m_negotiatedDevice = true;
        m_gpu = context.gpu;
        m_device = context.device;
        m_queue = context.queue;
        m_queueFamily = context.queue_family_index;
    } else if (!createDefaultDevice()) {
        return false;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_gpu, &props);
    m_gpuName = props.deviceName;

    // Les fonctions "de device".
#define LOAD_DEVICE(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(m_device, #name))
    LOAD_DEVICE(vkDestroyDevice);
    LOAD_DEVICE(vkGetDeviceQueue);
    LOAD_DEVICE(vkDeviceWaitIdle);
    LOAD_DEVICE(vkCreateCommandPool);
    LOAD_DEVICE(vkDestroyCommandPool);
    LOAD_DEVICE(vkAllocateCommandBuffers);
    LOAD_DEVICE(vkResetCommandBuffer);
    LOAD_DEVICE(vkBeginCommandBuffer);
    LOAD_DEVICE(vkEndCommandBuffer);
    LOAD_DEVICE(vkCmdPipelineBarrier);
    LOAD_DEVICE(vkCmdCopyImageToBuffer);
    LOAD_DEVICE(vkQueueSubmit);
    LOAD_DEVICE(vkCreateFence);
    LOAD_DEVICE(vkDestroyFence);
    LOAD_DEVICE(vkWaitForFences);
    LOAD_DEVICE(vkResetFences);
    LOAD_DEVICE(vkCreateBuffer);
    LOAD_DEVICE(vkDestroyBuffer);
    LOAD_DEVICE(vkGetBufferMemoryRequirements);
    LOAD_DEVICE(vkAllocateMemory);
    LOAD_DEVICE(vkFreeMemory);
    LOAD_DEVICE(vkBindBufferMemory);
    LOAD_DEVICE(vkMapMemory);
    LOAD_DEVICE(vkUnmapMemory);
#undef LOAD_DEVICE

    // 4. De quoi recopier les images : une commande réutilisable et une fence.
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS ||
        (cmdInfo.commandPool = m_pool, vkAllocateCommandBuffers(m_device, &cmdInfo, &m_cmd) != VK_SUCCESS) ||
        vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        m_error = "Préparation de la recopie Vulkan impossible";
        destroy();
        return false;
    }

    // 5. L'interface que le core récupérera avec GET_HW_RENDER_INTERFACE.
    m_interface = {};
    m_interface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    m_interface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    m_interface.handle = this;
    m_interface.instance = m_instance;
    m_interface.gpu = m_gpu;
    m_interface.device = m_device;
    m_interface.get_device_proc_addr = vkGetDeviceProcAddr;
    m_interface.get_instance_proc_addr = vkGetInstanceProcAddr;
    m_interface.queue = m_queue;
    m_interface.queue_index = m_queueFamily;
    m_interface.set_image = &setImage;
    m_interface.get_sync_index = &getSyncIndex;
    m_interface.get_sync_index_mask = &getSyncIndexMask;
    m_interface.set_command_buffers = &setCommandBuffers;
    m_interface.wait_sync_index = &waitSyncIndex;
    m_interface.lock_queue = &lockQueue;
    m_interface.unlock_queue = &unlockQueue;
    m_interface.set_signal_semaphore = &setSignalSemaphore;
    SDL_Log("Vulkan prêt sur %s%s", m_gpuName.c_str(), m_negotiatedDevice ? " (device créé par le core)" : "");
    return true;
}

bool VulkanRender::createDefaultDevice()
{
    // Une file capable de dessiner ET de calculer (GRAPHICS | COMPUTE) :
    // c'est ce que libretro garantit aux cores.
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &count, families.data());
    bool found = false;
    for (uint32_t i = 0; i < count && !found; ++i)
        if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            m_queueFamily = i;
            found = true;
        }
    if (!found) {
        m_error = "Aucune file graphique Vulkan";
        return false;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    if (vkCreateDevice(m_gpu, &info, nullptr, &m_device) != VK_SUCCESS) {
        m_device = VK_NULL_HANDLE;
        m_error = "Création du device Vulkan impossible";
        return false;
    }
    auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(vkGetDeviceProcAddr(m_device, "vkGetDeviceQueue"));
    getQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

void VulkanRender::destroy()
{
    if (m_device) {
        vkDeviceWaitIdle(m_device);
        if (m_stagingMap)
            vkUnmapMemory(m_device, m_stagingMemory);
        if (m_staging)
            vkDestroyBuffer(m_device, m_staging, nullptr);
        if (m_stagingMemory)
            vkFreeMemory(m_device, m_stagingMemory, nullptr);
        if (m_fence)
            vkDestroyFence(m_device, m_fence, nullptr);
        if (m_pool)
            vkDestroyCommandPool(m_device, m_pool, nullptr);
    }
    // Le core libère ses ressources annexes AVANT que le device et
    // l'instance disparaissent (règle de l'interface de négociation).
    if (m_negotiatedDevice && m_negotiation && m_negotiation->destroy_device)
        m_negotiation->destroy_device();
    if (m_device)
        vkDestroyDevice(m_device, nullptr);
    if (m_instance && vkDestroyInstance)
        vkDestroyInstance(m_instance, nullptr);
    m_stagingMap = nullptr;
    m_staging = VK_NULL_HANDLE;
    m_stagingMemory = VK_NULL_HANDLE;
    m_stagingSize = 0;
    m_fence = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_cmd = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_gpu = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;
    m_negotiatedDevice = false;
    m_hasImage = false;
    m_waitSemaphores.clear();
    m_coreCommands.clear();
    m_signalSemaphore = VK_NULL_HANDLE;
    m_interface = {};
}

// --- Callbacks du core ---------------------------------------------------------------------

void VulkanRender::setImage(void* handle, const retro_vulkan_image* image, uint32_t numSemaphores,
                            const VkSemaphore* semaphores, uint32_t srcQueueFamily)
{
    auto* self = static_cast<VulkanRender*>(handle);
    self->m_hasImage = image != nullptr;
    if (image)
        self->m_image = *image;
    self->m_imageQueueFamily = srcQueueFamily;
    // "Attends ces signaux avant de lire l'image" : le core n'a peut-être
    // pas fini de la dessiner.
    self->m_waitSemaphores.assign(semaphores, semaphores + numSemaphores);
}

// On recopie chaque image avant de rendre la main au core : une seule
// "image en vol" suffit (index 0, masque 1).
uint32_t VulkanRender::getSyncIndex(void*)
{
    return 0;
}

uint32_t VulkanRender::getSyncIndexMask(void*)
{
    return 1;
}

void VulkanRender::setCommandBuffers(void* handle, uint32_t num, const VkCommandBuffer* buffers)
{
    // Des commandes du core que NOUS devons envoyer à la carte graphique.
    auto* self = static_cast<VulkanRender*>(handle);
    self->m_coreCommands.assign(buffers, buffers + num);
}

void VulkanRender::waitSyncIndex(void*)
{
    // Rien à attendre : la recopie de l'image précédente est déjà finie.
}

void VulkanRender::lockQueue(void* handle)
{
    static_cast<VulkanRender*>(handle)->m_queueMutex.lock();
}

void VulkanRender::unlockQueue(void* handle)
{
    static_cast<VulkanRender*>(handle)->m_queueMutex.unlock();
}

void VulkanRender::setSignalSemaphore(void* handle, VkSemaphore semaphore)
{
    static_cast<VulkanRender*>(handle)->m_signalSemaphore = semaphore;
}

// --- Recopie de l'image ---------------------------------------------------------------------

uint32_t VulkanRender::memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(m_gpu, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return UINT32_MAX;
}

bool VulkanRender::ensureStaging(VkDeviceSize size)
{
    if (size <= m_stagingSize)
        return true;
    if (m_staging) {
        vkUnmapMemory(m_device, m_stagingMemory);
        vkDestroyBuffer(m_device, m_staging, nullptr);
        vkFreeMemory(m_device, m_stagingMemory, nullptr);
        m_staging = VK_NULL_HANDLE;
        m_stagingMemory = VK_NULL_HANDLE;
        m_stagingMap = nullptr;
        m_stagingSize = 0;
    }
    // Un tampon que le processeur peut lire directement (HOST_VISIBLE), sans
    // étape de "vidage" (HOST_COHERENT).
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &info, nullptr, &m_staging) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, m_staging, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &alloc, nullptr, &m_stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, m_staging, m_stagingMemory, 0) != VK_SUCCESS ||
        vkMapMemory(m_device, m_stagingMemory, 0, size, 0, &m_stagingMap) != VK_SUCCESS)
        return false;
    m_stagingSize = size;
    return true;
}

bool VulkanRender::readFrame(unsigned width, unsigned height, std::vector<uint32_t>& pixels)
{
    if (!ready() || !m_hasImage || width == 0 || height == 0) {
        if (!m_warnedNoImage) {
            SDL_Log("Vulkan : image annoncée (%ux%u) mais aucune image confiée par le core", width, height);
            m_warnedNoImage = true;
        }
        return false;
    }
    if (!m_loggedFirst) {
        SDL_Log("Vulkan : première image %ux%u, format %d, disposition %d", width, height,
                static_cast<int>(m_image.create_info.format), static_cast<int>(m_image.image_layout));
        m_loggedFirst = true;
    }
    const VkImage image = m_image.create_info.image;
    const VkDeviceSize size = VkDeviceSize(width) * height * 4;
    if (!ensureStaging(size))
        return false;

    // Les commandes : passer l'image en "source de copie", la copier dans le
    // tampon, puis la remettre dans l'état où le core l'a laissée.
    VkImageSubresourceRange range = m_image.create_info.subresourceRange;
    range.levelCount = 1;
    range.layerCount = 1;
    VkImageMemoryBarrier toCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toCopy.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toCopy.oldLayout = m_image.image_layout;
    toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.image = image;
    toCopy.subresourceRange = range;
    VkImageMemoryBarrier back = toCopy;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = m_image.image_layout;

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = range.baseMipLevel;
    region.imageSubresource.baseArrayLayer = range.baseArrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};

    vkResetCommandBuffer(m_cmd, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_cmd, &begin);
    vkCmdPipelineBarrier(m_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toCopy);
    vkCmdCopyImageToBuffer(m_cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_staging, 1, &region);
    vkCmdPipelineBarrier(m_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &back);
    vkEndCommandBuffer(m_cmd);

    // Envoi : d'abord les commandes que le core nous a confiées, puis la
    // nôtre, après les signaux qu'il nous a demandé d'attendre.
    std::vector<VkCommandBuffer> commands = m_coreCommands;
    commands.push_back(m_cmd);
    std::vector<VkPipelineStageFlags> stages(m_waitSemaphores.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = static_cast<uint32_t>(m_waitSemaphores.size());
    submit.pWaitSemaphores = m_waitSemaphores.data();
    submit.pWaitDstStageMask = stages.data();
    submit.commandBufferCount = static_cast<uint32_t>(commands.size());
    submit.pCommandBuffers = commands.data();
    if (m_signalSemaphore) {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &m_signalSemaphore;
    }
    vkResetFences(m_device, 1, &m_fence);
    VkResult result;
    {
        std::lock_guard lock(m_queueMutex);
        result = vkQueueSubmit(m_queue, 1, &submit, m_fence);
    }
    // Chaque image n'est attendue qu'une fois.
    m_waitSemaphores.clear();
    m_coreCommands.clear();
    m_signalSemaphore = VK_NULL_HANDLE;
    if (result != VK_SUCCESS)
        return false;
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);

    // Les pixels sont arrivés dans le tampon. Vulkan range souvent ses
    // couleurs en B,G,R,A (comme nous) ; sinon en R,G,B,A : on inverse.
    pixels.resize(size_t(width) * height);
    std::memcpy(pixels.data(), m_stagingMap, size_t(size));
    const VkFormat format = m_image.create_info.format;
    const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    const bool rgba = format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
                      format == VK_FORMAT_A8B8G8R8_UNORM_PACK32 || format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
    if (!bgra && !rgba && !m_warnedFormat) {
        SDL_Log("Vulkan : format d'image %d non prévu, couleurs peut-être fausses", static_cast<int>(format));
        m_warnedFormat = true;
    }
    if (rgba)
        for (uint32_t& p : pixels)
            p = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
    return true;
}
