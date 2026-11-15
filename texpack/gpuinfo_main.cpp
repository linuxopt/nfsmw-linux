// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// texpack-gpuinfo: the Vulkan GPUs, as JSON, in the order the Vulkan loader
// lists them (the order realesrgan-ncnn-vulkan's -g numbers them):
//   [{"index": 0, "name": "...", "discrete": true, "vram_mb": 16304, "bc7": true}, ...]
// vram_mb: the largest device-local heap (on an integrated GPU, the share of
// system memory the driver offers). bc7: BC7 textures can be sampled.
#include <vulkan/vulkan.h>

#include <cstdio>
#include <string>
#include <algorithm>
#include <vector>

int main()
{
    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "texpack-gpuinfo";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &app;
    VkInstance instance;
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
    {
        printf("[]\n");
        return 0;
    }
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    printf("[");
    for (uint32_t i = 0; i < count; i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        VkPhysicalDeviceMemoryProperties mem;
        vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);
        VkDeviceSize heap = 0;
        for (uint32_t h = 0; h < mem.memoryHeapCount; h++)
            if (mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                heap = std::max(heap, mem.memoryHeaps[h].size);
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(devices[i], VK_FORMAT_BC7_UNORM_BLOCK, &fp);
        std::string name;
        for (const char* c = props.deviceName; *c; c++)
            if (*c != '"' && *c != '\\')
                name += *c;
        printf("%s{\"index\": %u, \"name\": \"%s\", \"discrete\": %s, \"vram_mb\": %llu, \"bc7\": %s}", i ? ", " : "", i, name.c_str(),
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "true" : "false", (unsigned long long)(heap >> 20),
            (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? "true" : "false");
    }
    printf("]\n");
    vkDestroyInstance(instance, nullptr);
    return 0;
}
