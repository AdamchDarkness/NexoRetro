// Mini-core de TEST du rendu Vulkan (pas de jeu nécessaire).
//
// Il fait ce que font les vrais cores Vulkan (ParaLLEl-RDP, Beetle PSX HW...),
// en plus simple : il crée sa propre image Vulkan, la remplit d'une couleur à
// chaque image, puis la confie à l'émulateur avec set_image().
//
//   - fond ROUGE dont le vert monte et redescend : le rendu fonctionne, et les
//     couleurs sont dans le bon ordre (du bleu voudrait dire R et B inversés) ;
//   - fond VERT tant que A (ou B) est appuyé : les manettes arrivent au core.
//
// VKTEST_NEGOTIATE=1 : le core crée lui-même son device (interface de
// négociation v2), pour tester ce chemin-là aussi.

#define VK_NO_PROTOTYPES
#include <libretro_vulkan.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr unsigned kWidth = 320;
constexpr unsigned kHeight = 240;

retro_environment_t environ_cb;
retro_video_refresh_t video_cb;
retro_input_poll_t input_poll_cb;
retro_input_state_t input_state_cb;
retro_hw_render_callback hw_render;
const retro_hw_render_interface_vulkan* vk = nullptr;
unsigned frame = 0;

// Fonctions Vulkan, chargées depuis l'interface donnée par l'émulateur.
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
PFN_vkCreateImage vkCreateImage;
PFN_vkDestroyImage vkDestroyImage;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkAllocateMemory vkAllocateMemory;
PFN_vkFreeMemory vkFreeMemory;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkCreateImageView vkCreateImageView;
PFN_vkDestroyImageView vkDestroyImageView;
PFN_vkCreateCommandPool vkCreateCommandPool;
PFN_vkDestroyCommandPool vkDestroyCommandPool;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdClearColorImage vkCmdClearColorImage;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;

VkImage image = VK_NULL_HANDLE;
VkDeviceMemory memory = VK_NULL_HANDLE;
VkImageView view = VK_NULL_HANDLE;
VkCommandPool pool = VK_NULL_HANDLE;
VkCommandBuffer cmd = VK_NULL_HANDLE;
retro_vulkan_image retroImage{};
bool firstFrame = true;

void contextDestroy()
{
    if (!vk)
        return;
    vkDeviceWaitIdle(vk->device);
    vkDestroyImageView(vk->device, view, nullptr);
    vkDestroyImage(vk->device, image, nullptr);
    vkFreeMemory(vk->device, memory, nullptr);
    vkDestroyCommandPool(vk->device, pool, nullptr);
    vk = nullptr;
}

void contextReset()
{
    if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &vk) || !vk ||
        vk->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN)
        return;
    const VkDevice dev = vk->device;
    vkGetDeviceProcAddr = vk->get_device_proc_addr;
#define LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(dev, #name))
    LOAD(vkCreateImage);
    LOAD(vkDestroyImage);
    LOAD(vkGetImageMemoryRequirements);
    LOAD(vkAllocateMemory);
    LOAD(vkFreeMemory);
    LOAD(vkBindImageMemory);
    LOAD(vkCreateImageView);
    LOAD(vkDestroyImageView);
    LOAD(vkCreateCommandPool);
    LOAD(vkDestroyCommandPool);
    LOAD(vkAllocateCommandBuffers);
    LOAD(vkResetCommandBuffer);
    LOAD(vkBeginCommandBuffer);
    LOAD(vkEndCommandBuffer);
    LOAD(vkCmdPipelineBarrier);
    LOAD(vkCmdClearColorImage);
    LOAD(vkDeviceWaitIdle);
#undef LOAD
    vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        vk->get_instance_proc_addr(vk->instance, "vkGetPhysicalDeviceMemoryProperties"));

    // L'image, en R8G8B8A8 (l'émulateur doit inverser R et B pour l'afficher).
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {kWidth, kHeight, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(dev, &info, nullptr, &image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, image, &req);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(vk->gpu, &props);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            alloc.memoryTypeIndex = i;
            break;
        }
    vkAllocateMemory(dev, &alloc, nullptr, &memory);
    vkBindImageMemory(dev, image, memory, 0);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = info.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(dev, &viewInfo, nullptr, &view);
    retroImage.image_view = view;
    retroImage.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    retroImage.create_info = viewInfo;

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vk->queue_index;
    vkCreateCommandPool(dev, &poolInfo, nullptr, &pool);
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool = pool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev, &cmdInfo, &cmd);
    firstFrame = true;
}

// --- Négociation (VKTEST_NEGOTIATE=1) -------------------------------------------------

const VkApplicationInfo* appInfo()
{
    static const VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "vulkantest", 1, "vulkantest", 1,
                                       VK_API_VERSION_1_1};
    return &app;
}

bool createDevice2(retro_vulkan_context* context, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR,
                   PFN_vkGetInstanceProcAddr getInstanceProcAddr, retro_vulkan_create_device_wrapper_t wrapper, void* opaque)
{
    auto getFamilies = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        getInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    uint32_t count = 0;
    getFamilies(gpu, &count, nullptr);
    VkQueueFamilyProperties families[16];
    count = count > 16 ? 16 : count;
    getFamilies(gpu, &count, families);
    uint32_t family = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            family = i;
            break;
        }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    const VkDevice device = wrapper(gpu, opaque, &info);
    if (!device)
        return false;
    auto getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(getInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(getDeviceProcAddr(device, "vkGetDeviceQueue"));
    context->gpu = gpu;
    context->device = device;
    context->queue_family_index = family;
    getQueue(device, family, 0, &context->queue);
    context->presentation_queue = context->queue;
    context->presentation_queue_family_index = family;
    return true;
}

retro_hw_render_context_negotiation_interface_vulkan negotiation{
    RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN, 2, appInfo, nullptr, nullptr, nullptr, createDevice2};

} // namespace

// --- API libretro ---------------------------------------------------------------------------

extern "C" {

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
    bool noGame = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &noGame);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t) {}
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
RETRO_API void retro_init() {}
RETRO_API void retro_deinit() {}
RETRO_API unsigned retro_api_version() { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(retro_system_info* info)
{
    std::memset(info, 0, sizeof(*info));
    info->library_name = "Vulkan test";
    info->library_version = "1";
    info->valid_extensions = "";
}

RETRO_API void retro_get_system_av_info(retro_system_av_info* info)
{
    std::memset(info, 0, sizeof(*info));
    info->geometry = {kWidth, kHeight, kWidth, kHeight, 4.0f / 3.0f};
    info->timing = {60.0, 44100.0};
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}
RETRO_API void retro_reset() { frame = 0; }

RETRO_API bool retro_load_game(const retro_game_info*)
{
    hw_render = {};
    hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
    hw_render.version_major = VK_API_VERSION_1_1;
    hw_render.context_reset = contextReset;
    hw_render.context_destroy = contextDestroy;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
        return false;
    const char* negotiate = std::getenv("VKTEST_NEGOTIATE");
    if (negotiate && negotiate[0] == '1')
        environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, &negotiation);
    return true;
}

RETRO_API void retro_unload_game() {}

RETRO_API void retro_run()
{
    input_poll_cb();
    ++frame;
    if (!vk)
        return;
    const bool pressed = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) ||
                         input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    const float wave = static_cast<float>(frame % 120) / 119.0f;
    VkClearColorValue color{};
    color.float32[0] = pressed ? 0.0f : 1.0f; // rouge
    color.float32[1] = pressed ? 1.0f : wave; // vert
    color.float32[2] = 0.0f;                  // bleu
    color.float32[3] = 1.0f;

    vk->wait_sync_index(vk->handle);
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier toClear{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toClear.srcAccessMask = 0;
    toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toClear.oldLayout = firstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.srcQueueFamilyIndex = toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.image = image;
    toClear.subresourceRange = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &toClear);
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    VkImageMemoryBarrier toRead = toClear;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toRead);
    vkEndCommandBuffer(cmd);
    firstFrame = false;

    // L'émulateur envoie nos commandes, puis lit l'image.
    vk->set_command_buffers(vk->handle, 1, &cmd);
    vk->set_image(vk->handle, &retroImage, 0, nullptr, vk->queue_index);
    video_cb(RETRO_HW_FRAME_BUFFER_VALID, kWidth, kHeight, 0);
}

RETRO_API size_t retro_serialize_size() { return 0; }
RETRO_API bool retro_serialize(void*, size_t) { return false; }
RETRO_API bool retro_unserialize(const void*, size_t) { return false; }
RETRO_API void retro_cheat_reset() {}
RETRO_API void retro_cheat_set(unsigned, bool, const char*) {}
RETRO_API bool retro_load_game_special(unsigned, const retro_game_info*, size_t) { return false; }
RETRO_API unsigned retro_get_region() { return RETRO_REGION_NTSC; }
RETRO_API void* retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }

} // extern "C"
