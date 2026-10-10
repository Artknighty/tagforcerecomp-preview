/* Backend Vulkan 1.3 do renderizador por GPU (ver gpu.h).
 * - vulkan-1.dll carregada em execucao (sem SDK/import lib); sombreadores GLSL -> SPIR-V por tools/spv2h.py (gen/gpu_spv.h).
 * - dynamic rendering + synchronization2 (nucleo 1.3); estado de profundidade e scissor/viewport dinamicos; blend no pipeline.
 * - profundidade + stencil D24S8 (ou D32S8; o z do PSP tem 16 bits: k/65535 convertido segue distinto e ordenado, a comparacao sai identica);
 *   o stencil de 8 bits faz o papel do alfa do framebuffer do PSP (que E o stencil la). Sem formato com stencil: D16, stencil desligado.
 * - varios quadros em voo (NF), cada um com seus command buffers, anel de vertices e anel de upload mapeados (sem copias extras);
 *   texturas sao enviadas num command buffer de upload separado (sem quebrar o render pass); VkPipelineCache gravado em disco.
 * - uma unica submissao por quadro (apresentacao), salvo leituras sincronas (comparacao com o nativo). */
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "gpu.h"
#include "gfx.h"
#include "util.h"
#include "../gen/gpu_spv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <string>

/* ------------------------------------------------------------------ funcoes do Vulkan */
#define VK_GLOBAL_FUNCS(X) X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties)
#define VK_INSTANCE_FUNCS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateWin32SurfaceKHR) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceFormatProperties)
#define VK_DEVICE_FUNCS(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueWaitIdle) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkQueueSubmit2) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) \
    X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
    X(vkCreatePipelineCache) X(vkDestroyPipelineCache) X(vkGetPipelineCacheData) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
    X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) \
    X(vkResetFences) X(vkGetFenceStatus) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCmdPipelineBarrier2) X(vkCmdBeginRendering) X(vkCmdEndRendering) \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdPushConstants) X(vkCmdSetViewport) X(vkCmdSetScissor) \
    X(vkCmdSetBlendConstants) X(vkCmdSetDepthTestEnable) X(vkCmdSetDepthWriteEnable) X(vkCmdSetDepthCompareOp) X(vkCmdSetStencilTestEnable) X(vkCmdSetStencilOp) X(vkCmdSetStencilCompareMask) X(vkCmdSetStencilWriteMask) \
    X(vkCmdSetStencilReference) X(vkCmdClearAttachments) X(vkCmdDraw) X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImageToBuffer) X(vkCmdBlitImage) X(vkFlushMappedMemoryRanges) X(vkInvalidateMappedMemoryRanges)
#define X(n) static PFN_##n n;
VK_GLOBAL_FUNCS(X) VK_INSTANCE_FUNCS(X) VK_DEVICE_FUNCS(X)
#undef X
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_;

static HWND g_hwnd; static HMODULE g_lib;
static VkInstance g_inst; static VkPhysicalDevice g_pd; static VkDevice g_dev; static VkQueue g_q; static uint32_t g_qf;
static VkSurfaceKHR g_surf; static VkSwapchainKHR g_swap; static VkFormat g_swapFmt; static VkExtent2D g_swapExt;
static std::vector<VkImage> g_swapImg; static std::vector<VkImageView> g_swapView; static std::vector<VkSemaphore> g_renderDone;
static VkPhysicalDeviceMemoryProperties g_mem; static char g_adapter[128] = "?"; static bool g_dbg, g_inited;
static VkFormat g_dsFmt = VK_FORMAT_D16_UNORM; static bool g_hasSt;   /* formato de profundidade dos quadros sombra; g_hasSt: tem stencil */
static VkImageAspectFlags ds_aspect(void) { return VK_IMAGE_ASPECT_DEPTH_BIT | (g_hasSt ? VK_IMAGE_ASPECT_STENCIL_BIT : 0); }
static const VkImageLayout DS_LAYOUT = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
static const char *vkres(VkResult r) { static char b[24]; snprintf(b, sizeof b, "VkResult %d", (int)r); return b; }
#define CHK(x) do { VkResult r_ = (x); if (r_ < 0) { fprintf(stderr, "[vulkan] %s falhou: %s (linha %d)\n", #x, vkres(r_), __LINE__); return false; } } while (0)
#define CHKV(x) do { VkResult r_ = (x); if (r_ < 0) { fprintf(stderr, "[vulkan] %s falhou: %s (linha %d)\n", #x, vkres(r_), __LINE__); } } while (0)

static int mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid = 0) {
    for (uint32_t i = 0; i < g_mem.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & want) == want && !(g_mem.memoryTypes[i].propertyFlags & avoid)) return (int)i;
    for (uint32_t i = 0; i < g_mem.memoryTypeCount; i++) if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}
static int g_allocs;
static VkDeviceMemory alloc_mem(const VkMemoryRequirements &rq, VkMemoryPropertyFlags props, VkMemoryPropertyFlags avoid = 0) {
    int t = mem_type(rq.memoryTypeBits, props, avoid); if (t < 0) { static int n; if (n++ < 10) fprintf(stderr, "[vulkan] nenhum tipo de memoria serve (%llu bytes)\n", (unsigned long long)rq.size); return VK_NULL_HANDLE; }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = rq.size; ai.memoryTypeIndex = (uint32_t)t;
    VkDeviceMemory m = VK_NULL_HANDLE; VkResult ar = vkAllocateMemory(g_dev, &ai, nullptr, &m);
    if (ar < 0) { static int n; if (n++ < 10) fprintf(stderr, "[vulkan] vkAllocateMemory falhou (%s) para %llu MB\n", vkres(ar), (unsigned long long)(rq.size >> 20)); return VK_NULL_HANDLE; }
    g_allocs++; return m;
}
static void free_mem(VkDeviceMemory m) { if (m) { vkFreeMemory(g_dev, m, nullptr); g_allocs--; } }

struct Buf { VkBuffer b; VkDeviceMemory m; uint8_t *p; size_t size; };
static bool make_buf(Buf &o, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkMemoryPropertyFlags avoid = 0) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CHK(vkCreateBuffer(g_dev, &bi, nullptr, &o.b));
    VkMemoryRequirements rq; vkGetBufferMemoryRequirements(g_dev, o.b, &rq);
    o.m = alloc_mem(rq, props, avoid); if (!o.m) return false;
    CHK(vkBindBufferMemory(g_dev, o.b, o.m, 0));
    o.size = size; o.p = nullptr;
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) CHK(vkMapMemory(g_dev, o.m, 0, VK_WHOLE_SIZE, 0, (void **)&o.p));
    return true;
}
static void free_buf(Buf &o) { if (o.b) vkDestroyBuffer(g_dev, o.b, nullptr); free_mem(o.m); o = Buf(); }

struct Img { VkImage i; VkDeviceMemory m; VkImageView v; VkImageLayout layout; int w, h; VkFormat fmt; };
static bool make_img(Img &o, int w, int h, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, int levels = 1) {
    VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent = {(uint32_t)w, (uint32_t)h, 1};
    ii.mipLevels = (uint32_t)levels; ii.arrayLayers = 1; ii.samples = samples; ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    CHK(vkCreateImage(g_dev, &ii, nullptr, &o.i));
    VkMemoryRequirements rq; vkGetImageMemoryRequirements(g_dev, o.i, &rq);
    o.m = alloc_mem(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); if (!o.m) return false;
    CHK(vkBindImageMemory(g_dev, o.i, o.m, 0));
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = o.i; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
    vi.subresourceRange = {aspect, 0, (uint32_t)levels, 0, 1};
    CHK(vkCreateImageView(g_dev, &vi, nullptr, &o.v));
    o.layout = VK_IMAGE_LAYOUT_UNDEFINED; o.w = w; o.h = h; o.fmt = fmt; return true;
}
static void free_img(Img &o) { if (o.v) vkDestroyImageView(g_dev, o.v, nullptr); if (o.i) vkDestroyImage(g_dev, o.i, nullptr); free_mem(o.m); o = Img(); }

/* ------------------------------------------------------------------ quadros em voo */
static const int NF = 3;
static const size_t VB_SIZE = 16u << 20, ST_SIZE = 48u << 20;
struct Dead { Img img; VkDescriptorSet set; uint64_t tag; };
struct Slot {
    VkCommandPool pool; VkCommandBuffer up, cb; VkFence fence; bool inflight, upRec, cbRec; uint64_t serial;
    Buf vb, st; size_t vbUsed, stUsed; VkSemaphore acquire;
};
static Slot g_slot[NF]; static int g_cur; static uint64_t g_serial = 1;
static std::vector<Dead> g_dead;
static VkDescriptorPool g_dpool; static VkDescriptorSetLayout g_lay1, g_lay2, g_laySamp; static VkDescriptorSet g_sampSet;
static VkPipelineLayout g_plA, g_plB; static VkSampler g_smp[8];
static VkPipelineCache g_pcache; static bool g_pcDirty; static std::wstring g_pcPath;   /* caminho em UTF-16: _wfopen funciona com acentos na pasta */

static Slot &S() { return g_slot[g_cur]; }
static VkCommandBuffer cbuf() {
    Slot &s = S();
    if (!s.cbRec) { VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(s.cb, &bi); s.cbRec = true; }
    return s.cb;
}
static VkCommandBuffer upbuf() {
    Slot &s = S();
    if (!s.upRec) { VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(s.up, &bi); s.upRec = true; }
    return s.up;
}
static void barrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags asp, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
    VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2}; b.srcStageMask = ss; b.srcAccessMask = sa; b.dstStageMask = ds; b.dstAccessMask = da;
    b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img; b.subresourceRange = {asp, 0, 1, 0, 1};
    VkDependencyInfo d = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; d.imageMemoryBarrierCount = 1; d.pImageMemoryBarriers = &b; vkCmdPipelineBarrier2(cb, &d);
}
static const VkPipelineStageFlags2 ST_FS = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, ST_COL = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    ST_TR = VK_PIPELINE_STAGE_2_TRANSFER_BIT, ST_DEP = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
static void free_dead_upto(uint64_t done) {   /* libera os recursos cujos comandos ja terminaram (tag <= done) */
    for (size_t i = 0; i < g_dead.size();) {
        if (g_dead[i].tag <= done) { if (g_dead[i].set) vkFreeDescriptorSets(g_dev, g_dpool, 1, &g_dead[i].set); free_img(g_dead[i].img); g_dead[i] = g_dead.back(); g_dead.pop_back(); }
        else i++;
    }
}
/* estado de cache do command buffer atual */
struct CbState { VkPipeline pipe; VkDescriptorSet set0; bool sampBound, vbBound; uint64_t dyn; VkPipelineLayout layout; };
static CbState g_cs;
static void cs_reset(void) { g_cs = CbState(); g_cs.dyn = ~0ull; }
static bool g_inRender; static int g_curFb = -1;   /* renderizando num quadro sombra? qual? */
static void end_render(void) { if (g_inRender) { vkCmdEndRendering(S().cb); g_inRender = false; g_curFb = -1; } }

/* envia o que foi gravado e avanca para o proximo slot (esperando ele terminar se ainda estiver em voo) */
static VkSemaphore g_waitSem, g_sigSem;   /* semaforos da proxima submissao (apresentacao) */
static void submit_slot(bool wait_done) {
    Slot &s = S();
    end_render();
    VkCommandBufferSubmitInfo ci[2]; uint32_t n = 0;
    if (s.upRec) { vkEndCommandBuffer(s.up); ci[n] = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; ci[n++].commandBuffer = s.up; }
    if (s.cbRec) { vkEndCommandBuffer(s.cb); ci[n] = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; ci[n++].commandBuffer = s.cb; }
    VkSemaphoreSubmitInfo ws = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, ss = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    VkSubmitInfo2 si = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; si.commandBufferInfoCount = n; si.pCommandBufferInfos = ci;
    if (g_waitSem) { ws.semaphore = g_waitSem; ws.stageMask = ST_COL; si.waitSemaphoreInfoCount = 1; si.pWaitSemaphoreInfos = &ws; }
    if (g_sigSem) { ss.semaphore = g_sigSem; ss.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; si.signalSemaphoreInfoCount = 1; si.pSignalSemaphoreInfos = &ss; }
    if (n || g_waitSem || g_sigSem) { CHKV(vkQueueSubmit2(g_q, 1, &si, s.fence)); s.inflight = true; s.serial = g_serial; }
    g_waitSem = g_sigSem = VK_NULL_HANDLE;
    if (wait_done && s.inflight) { vkWaitForFences(g_dev, 1, &s.fence, VK_TRUE, ~0ull); vkResetFences(g_dev, 1, &s.fence); s.inflight = false; }
    s.upRec = s.cbRec = false; g_serial++;
    g_cur = (g_cur + 1) % NF;
    Slot &nx = S();
    if (nx.inflight) { vkWaitForFences(g_dev, 1, &nx.fence, VK_TRUE, ~0ull); vkResetFences(g_dev, 1, &nx.fence); nx.inflight = false; }
    vkResetCommandPool(g_dev, nx.pool, 0); nx.vbUsed = nx.stUsed = 0; nx.upRec = nx.cbRec = false;
    uint64_t minInfl = ~0ull; for (auto &o : g_slot) if (o.inflight) minInfl = std::min(minInfl, o.serial);
    free_dead_upto(minInfl == ~0ull ? ~0ull : minInfl - 1);
    cs_reset();
}
/* espera terminar tudo o que foi enviado (leituras sincronas, destruicao) */
static void wait_all(void) { if (g_dev) vkDeviceWaitIdle(g_dev); for (auto &o : g_slot) if (o.inflight) { vkWaitForFences(g_dev, 1, &o.fence, VK_TRUE, ~0ull); vkResetFences(g_dev, 1, &o.fence); o.inflight = false; } }
static size_t st_alloc(size_t n, size_t align = 16) {   /* espaco no anel de upload do slot atual; -1 = sem espaco */
    Slot &s = S(); size_t o = (s.stUsed + align - 1) & ~(align - 1);
    if (o + n > s.st.size) return (size_t)-1;
    s.stUsed = o + n; return o;
}

/* ------------------------------------------------------------------ pipelines */
static VkShaderModule g_sm[12];
enum { SM_MAIN_VERT, SM_MAIN_FRAG, SM_FULL_VERT, SM_RESYNC, SM_DOWN, SM_PRESENT, SM_OVERLAY };
static VkShaderModule mk_sm(const uint32_t *code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = bytes; ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE; vkCreateShaderModule(g_dev, &ci, nullptr, &m); return m;
}
struct PipeDesc { VkPipelineLayout layout; VkShaderModule vs, fs; bool vertexIn; VkFormat colorFmt, depthFmt; VkPipelineColorBlendAttachmentState blend; bool dynDepth, staticDepthAlways; VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT; };
static VkPipeline make_pipe(const PipeDesc &d) {
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = d.vs; st[0].pName = "main"; st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = d.fs; st[1].pName = "main";
    VkVertexInputBindingDescription vb = {0, sizeof(GpuVtx), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[3] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 24}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (d.vertexIn) { vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb; vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = va; }
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = d.samples;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (d.staticDepthAlways) { ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_ALWAYS; }
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 1; cb.pAttachments = &d.blend;
    VkDynamicState dyn[16] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; uint32_t nd = 2;
    bool dsSt = d.depthFmt == VK_FORMAT_D24_UNORM_S8_UINT || d.depthFmt == VK_FORMAT_D32_SFLOAT_S8_UINT;
    if (d.dynDepth) { dyn[nd++] = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE; dyn[nd++] = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE; dyn[nd++] = VK_DYNAMIC_STATE_DEPTH_COMPARE_OP; dyn[nd++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS; }
    if (d.dynDepth && dsSt) { dyn[nd++] = VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE; dyn[nd++] = VK_DYNAMIC_STATE_STENCIL_OP; dyn[nd++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK; dyn[nd++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK; dyn[nd++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE; }
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = nd; dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &d.colorFmt; ri.depthAttachmentFormat = d.depthFmt;
    ri.stencilAttachmentFormat = dsSt ? d.depthFmt : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo pi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pi.pNext = &ri; pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds; pi.pColorBlendState = &cb; pi.pDynamicState = &dy; pi.layout = d.layout;
    VkPipeline p = VK_NULL_HANDLE; CHKV(vkCreateGraphicsPipelines(g_dev, g_pcache, 1, &pi, nullptr, &p)); g_pcDirty = true; return p;
}
static VkPipelineColorBlendAttachmentState blend_off(VkColorComponentFlags mask = 15) { VkPipelineColorBlendAttachmentState b = {}; b.colorWriteMask = mask; return b; }
static VkPipeline g_pResync, g_pDown, g_pPresent, g_pOverlay; static VkFormat g_pPresentFmt;
static std::unordered_map<uint64_t, VkPipeline> g_pipes;
static VkSampleCountFlagBits g_msaa = VK_SAMPLE_COUNT_1_BIT;   /* amostras dos quadros sombra atuais (os pipelines de desenho e o de refazer seguem esse valor) */
static VkSampleCountFlags g_maxSamples = VK_SAMPLE_COUNT_1_BIT;
static VkBlendFactor map_factor(uint32_t f, bool is_src) {
    switch (f) {
    case 0: return is_src ? VK_BLEND_FACTOR_DST_COLOR : VK_BLEND_FACTOR_SRC_COLOR;
    case 1: return is_src ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 2: case 6: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 3: case 7: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 4: return VK_BLEND_FACTOR_DST_ALPHA;
    case 5: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 11: return VK_BLEND_FACTOR_ONE;
    default: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    }
}
static VkPipeline main_pipe(const GpuState &s) {
    uint32_t mask = (s.clear ? (((s.clear & 0x100) ? 7u : 0u) | ((s.clear & 0x200) ? 8u : 0u)) : 15u) & s.cmask;
    bool bl = s.blend && !s.clear;
    uint64_t key = mask | ((uint64_t)bl << 4) | ((uint64_t)(bl ? s.bsf : 0) << 5) | ((uint64_t)(bl ? s.bdf : 0) << 9) | ((uint64_t)(bl ? s.beq : 0) << 13);
    auto it = g_pipes.find(key); if (it != g_pipes.end()) return it->second;
    VkPipelineColorBlendAttachmentState b = blend_off(mask);
    if (bl) {
        static const VkBlendOp ops[] = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX, VK_BLEND_OP_ADD};
        b.blendEnable = VK_TRUE; b.srcColorBlendFactor = map_factor(s.bsf, true); b.dstColorBlendFactor = map_factor(s.bdf, false); b.colorBlendOp = ops[s.beq > 5 ? 0 : s.beq];
        b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; b.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    PipeDesc d = {g_plA, g_sm[SM_MAIN_VERT], g_sm[SM_MAIN_FRAG], true, VK_FORMAT_R8G8B8A8_UNORM, g_dsFmt, b, true, false, g_msaa};
    VkPipeline p = make_pipe(d); g_pipes[key] = p; return p;
}
static void load_pcache(void) {
    std::vector<uint8_t> data;
    if (FILE *f = _wfopen(g_pcPath.c_str(), L"rb")) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); if (n > 0 && n < (64 << 20)) { data.resize(n); if (fread(data.data(), 1, n, f) != (size_t)n) data.clear(); } fclose(f); }
    VkPipelineCacheCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO}; ci.initialDataSize = data.size(); ci.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(g_dev, &ci, nullptr, &g_pcache) < 0 && !data.empty()) { ci.initialDataSize = 0; ci.pInitialData = nullptr; vkCreatePipelineCache(g_dev, &ci, nullptr, &g_pcache); }
}
static void save_pcache(void) {
    if (!g_pcache || !g_pcDirty) return;
    size_t n = 0; if (vkGetPipelineCacheData(g_dev, g_pcache, &n, nullptr) < 0 || !n) return;
    std::vector<uint8_t> d(n); if (vkGetPipelineCacheData(g_dev, g_pcache, &n, d.data()) < 0) return;
    if (FILE *f = _wfopen(g_pcPath.c_str(), L"wb")) { fwrite(d.data(), 1, n, f); fclose(f); }
    g_pcDirty = false;
}

/* ------------------------------------------------------------------ texturas, quadros sombra */
struct TexRec { Img img; VkDescriptorSet set; bool used; };
static std::vector<TexRec> g_tex(1);   /* 0 = nenhuma */
static std::vector<uint32_t> g_texFree;
struct FbRec { bool used; int res, w, h, nw, nh, samples; Img col, dep, mc; VkDescriptorSet set; };   /* com MSAA: mc = cor multiamostrada, dep multiamostrada, col = resolvida (amostrada/lida) */
static const int kMaxFb = 32;   /* 4 de tela + ate 12 alvos de textura + intermediario e copias do 60 FPS */ static FbRec g_fb[kMaxFb];
static Img g_natC, g_natZ, g_downT, g_ovT, g_pixT, g_dummy; static VkDescriptorSet g_natSet, g_ovSet, g_pixSet, g_dummySet;
static bool g_ovOn; static Buf g_rb;

static VkDescriptorSet alloc_set(VkDescriptorSetLayout l) {
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = g_dpool; ai.descriptorSetCount = 1; ai.pSetLayouts = &l;
    VkDescriptorSet s = VK_NULL_HANDLE; if (vkAllocateDescriptorSets(g_dev, &ai, &s) < 0) return VK_NULL_HANDLE; return s;
}
static void write_set(VkDescriptorSet s, uint32_t binding, VkImageView v) {
    VkDescriptorImageInfo ii = {VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.dstBinding = binding; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(g_dev, 1, &w, 0, nullptr);
}
static void to_layout(VkCommandBuffer cb, Img &im, VkImageLayout to) {   /* transicao de layout de imagem de cor (amostrada <-> alvo <-> copia) */
    if (im.layout == to) return;
    VkPipelineStageFlags2 ss = ST_FS | ST_COL | ST_TR; VkAccessFlags2 sa = VK_ACCESS_2_MEMORY_WRITE_BIT;
    if (im.layout == VK_IMAGE_LAYOUT_UNDEFINED) { ss = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT; sa = 0; }
    VkPipelineStageFlags2 ds = to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? ST_FS : to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ? ST_COL : ST_TR;
    VkAccessFlags2 da = to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ? (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) : (VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    barrier(cb, im.i, VK_IMAGE_ASPECT_COLOR_BIT, im.layout, to, ss, sa, ds, da); im.layout = to;
}
/* copia w x h texels de src (com passo de linha em texels) para a imagem, no command buffer cb (fora de render pass) */
static bool upload_img(VkCommandBuffer cb, Img &im, const void *src, size_t bytesPerTexel, uint32_t srcRowTexels, int w, int h) {
    size_t n = (size_t)srcRowTexels * h * bytesPerTexel;
    size_t off = st_alloc(n);
    if (off == (size_t)-1) return false;
    memcpy(S().st.p + off, src, n);
    to_layout(cb, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy r = {}; r.bufferOffset = off; r.bufferRowLength = srcRowTexels; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cb, S().st.b, im.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    to_layout(cb, im, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}
static void barrier_rng(VkCommandBuffer cb, VkImage img, uint32_t base, uint32_t count, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
    VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2}; b.srcStageMask = ss; b.srcAccessMask = sa; b.dstStageMask = ds; b.dstAccessMask = da;
    b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, base, count, 0, 1};
    VkDependencyInfo d = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; d.imageMemoryBarrierCount = 1; d.pImageMemoryBarriers = &b; vkCmdPipelineBarrier2(cb, &d);
}
/* envia o nivel 0 e gera os demais por blits lineares (cada nivel = metade do anterior) */
static bool upload_mips(VkCommandBuffer cb, Img &im, const uint32_t *px, int w, int h, int levels) {
    size_t n = (size_t)w * h * 4, off = st_alloc(n); if (off == (size_t)-1) return false;
    memcpy(S().st.p + off, px, n);
    barrier_rng(cb, im.i, 0, (uint32_t)levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, ST_TR, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy r = {}; r.bufferOffset = off; r.bufferRowLength = (uint32_t)w; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cb, S().st.b, im.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    int mw = w, mh = h;
    for (int i = 1; i < levels; i++) {
        barrier_rng(cb, im.i, (uint32_t)(i - 1), 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ST_TR, VK_ACCESS_2_TRANSFER_WRITE_BIT, ST_TR, VK_ACCESS_2_TRANSFER_READ_BIT);
        int nw = std::max(1, mw / 2), nh = std::max(1, mh / 2);
        VkImageBlit bl = {}; bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(i - 1), 0, 1}; bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)i, 0, 1};
        bl.srcOffsets[1] = {mw, mh, 1}; bl.dstOffsets[1] = {nw, nh, 1};
        vkCmdBlitImage(cb, im.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, im.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
        mw = nw; mh = nh;
    }
    if (levels > 1) barrier_rng(cb, im.i, 0, (uint32_t)(levels - 1), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ST_TR, VK_ACCESS_2_TRANSFER_READ_BIT, ST_FS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    barrier_rng(cb, im.i, (uint32_t)(levels - 1), 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ST_TR, VK_ACCESS_2_TRANSFER_WRITE_BIT, ST_FS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    im.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; return true;
}
static uint32_t vk_tex_create(int w, int h, const uint32_t *px, bool mips) {
    if (!g_dev || g_allocs > 3000) return 0;
    int levels = 1; if (mips) { int m = std::max(w, h); while (m > 1) { m >>= 1; levels++; } }
    TexRec t = {}; if (!make_img(t.img, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (levels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0), VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, levels)) { free_img(t.img); return 0; }
    t.set = alloc_set(g_lay1); if (!t.set) { free_img(t.img); return 0; }
    VkCommandBuffer up = upbuf();
    auto send = [&](VkCommandBuffer c) { return levels > 1 ? upload_mips(c, t.img, px, w, h, levels) : upload_img(c, t.img, px, 4, (uint32_t)w, w, h); };
    if (!send(up)) {   /* anel de upload cheio: envia o quadro parcial e tenta de novo no proximo slot */
        submit_slot(false); up = upbuf();
        if (!send(up)) { vkFreeDescriptorSets(g_dev, g_dpool, 1, &t.set); free_img(t.img); return 0; }
    }
    write_set(t.set, 0, t.img.v);
    t.used = true; uint32_t id;
    if (!g_texFree.empty()) { id = g_texFree.back(); g_texFree.pop_back(); g_tex[id] = t; } else { id = (uint32_t)g_tex.size(); g_tex.push_back(t); }
    return id;
}
static void vk_tex_release(uint32_t h) {
    if (!h || h >= g_tex.size() || !g_tex[h].used) return;
    g_dead.push_back({g_tex[h].img, g_tex[h].set, g_serial});   /* os comandos ainda nao enviados (tag = serial atual) podem usar a textura */
    g_tex[h] = TexRec(); g_texFree.push_back(h);
}
static int vk_fb_create(int nw, int nh, int res) {
    if (!g_dev) return -1;
    int id = -1; for (int i = 0; i < kMaxFb; i++) if (!g_fb[i].used) { id = i; break; }
    if (id < 0) return -1;
    FbRec &f = g_fb[id]; f = FbRec(); f.res = res; f.nw = nw; f.nh = nh; f.w = nw * res; f.h = nh * res;
    /* MSAA pedido (ini/menu), limitado ao que a GPU suporta e a ~34 milhoes de amostras por quadro (memoria) */
    int want = std::max(1, g_gfx.msaa); while (want > 1 && ((g_maxSamples & want) == 0 || (long long)f.w * f.h * want > 34000000ll)) want >>= 1;
    if ((int)g_msaa != want) { bool any = false; for (auto &o : g_fb) any |= o.used; if (any) want = (int)g_msaa; else {
        wait_all(); for (auto &kv : g_pipes) vkDestroyPipeline(g_dev, kv.second, nullptr); g_pipes.clear(); if (g_pResync) { vkDestroyPipeline(g_dev, g_pResync, nullptr); g_pResync = VK_NULL_HANDLE; }
        g_msaa = (VkSampleCountFlagBits)want;
        PipeDesc rs = {g_plB, g_sm[SM_FULL_VERT], g_sm[SM_RESYNC], false, VK_FORMAT_R8G8B8A8_UNORM, g_dsFmt, blend_off(), false, true, g_msaa}; g_pResync = make_pipe(rs); } }
    f.samples = (int)g_msaa;
    bool ok = make_img(f.col, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
           && make_img(f.dep, f.w, f.h, g_dsFmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, ds_aspect(), g_msaa)
           && (f.samples == 1 || make_img(f.mc, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, g_msaa));
    if (ok) { f.set = alloc_set(g_lay1); ok = f.set != VK_NULL_HANDLE; }
    if (!ok) { free_img(f.col); free_img(f.dep); free_img(f.mc); return -1; }
    write_set(f.set, 0, f.col.v); f.used = true; return id;
}
static void vk_flush(void);
static void vk_fb_destroy(int id) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return;
    vk_flush(); wait_all();   /* raro (troca de resolucao/renderizador): espera a GPU */
    FbRec &f = g_fb[id]; if (f.set) vkFreeDescriptorSets(g_dev, g_dpool, 1, &f.set); free_img(f.col); free_img(f.dep); free_img(f.mc); f = FbRec();
}

/* ------------------------------------------------------------------ desenho em lotes */
struct Pend { GpuState st; uint32_t first, count; bool valid; };
static Pend g_pend;
struct PC { float tenv[4]; uint32_t flags, tfn, afunc, aref, amask, aover, sidx, aconst; float tsz[2], pad2[2]; float sfix[4]; float fbsz[2], pad3[2]; };   /* 0..80 fragmento, 80..96 vertice */
static void ds_to_attach(VkCommandBuffer cb, FbRec &f) {   /* profundidade/stencil do quadro sombra -> anexo de profundidade */
    if (f.dep.layout == DS_LAYOUT) return;
    bool und = f.dep.layout == VK_IMAGE_LAYOUT_UNDEFINED;
    barrier(cb, f.dep.i, ds_aspect(), f.dep.layout, DS_LAYOUT, und ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : ST_DEP, und ? 0 : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ST_DEP, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    f.dep.layout = DS_LAYOUT;
}
static void begin_fb_render(int id) {
    if (g_inRender && g_curFb == id) return;
    end_render();
    VkCommandBuffer cb = cbuf(); FbRec &f = g_fb[id];
    to_layout(cb, f.col, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (f.samples > 1) to_layout(cb, f.mc, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ds_to_attach(cb, f);
    VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, da = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = f.samples > 1 ? f.mc.v : f.col.v; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (f.samples > 1) { ca.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT; ca.resolveImageView = f.col.v; ca.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }   /* a imagem resolvida e a que as outras etapas leem */
    da.imageView = f.dep.v; da.imageLayout = DS_LAYOUT; da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO}; ri.renderArea = {{0, 0}, {(uint32_t)f.w, (uint32_t)f.h}}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca; ri.pDepthAttachment = &da;
    if (g_hasSt) ri.pStencilAttachment = &da;   /* mesma imagem combinada */
    vkCmdBeginRendering(cb, &ri);
    VkViewport vp = {0, 0, (float)f.w, (float)f.h, 0, 1}; vkCmdSetViewport(cb, 0, 1, &vp);
    g_inRender = true; g_curFb = id; g_cs.dyn = ~0ull;
}
static void bind_common(VkCommandBuffer cb, VkPipelineLayout layout, VkDescriptorSet set0) {
    if (g_cs.set0 != set0 || g_cs.layout != layout) { vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set0, 0, nullptr); g_cs.set0 = set0; }
    if (!g_cs.sampBound || g_cs.layout != layout) { vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &g_sampSet, 0, nullptr); g_cs.sampBound = true; }
    g_cs.layout = layout;
}
static void record_pend(void) {
    if (!g_pend.valid) return;
    g_pend.valid = false;
    const GpuState &s = g_pend.st;
    if (s.fb < 0 || s.fb >= kMaxFb || !g_fb[s.fb].used) return;
    FbRec &f = g_fb[s.fb];
    if (!s.clear && (s.tex & GPU_FBTEX)) {   /* a textura e a imagem (resolvida) de outro quadro sombra: termina o passe atual e a deixa legivel */
        int sid = (int)(s.tex & 0xff);
        if (sid != s.fb && sid < kMaxFb && g_fb[sid].used) { end_render(); to_layout(cbuf(), g_fb[sid].col, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); }
    }
    begin_fb_render(s.fb); VkCommandBuffer cb = cbuf();
    int sx1 = std::max(0, s.sx1 * f.res), sy1 = std::max(0, s.sy1 * f.res), sx2 = std::min(f.w, (s.sx2 + 1) * f.res), sy2 = std::min(f.h, (s.sy2 + 1) * f.res);
    if (sx2 <= sx1 || sy2 <= sy1) return;
    VkPipeline p = main_pipe(s);
    if (g_cs.pipe != p) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); g_cs.pipe = p; }
    VkRect2D sc = {{sx1, sy1}, {(uint32_t)(sx2 - sx1), (uint32_t)(sy2 - sy1)}}; vkCmdSetScissor(cb, 0, 1, &sc);
    bool en = s.clear ? true : s.ztest != 0, wr = s.clear ? (s.clear & 0x400) != 0 : s.zwrite != 0; uint32_t fn = s.clear ? 1 : s.zfunc;
    static const VkCompareOp cf[] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_GREATER_OR_EQUAL};
    vkCmdSetDepthTestEnable(cb, en); vkCmdSetDepthWriteEnable(cb, wr); vkCmdSetDepthCompareOp(cb, cf[fn & 7]);
    if (g_hasSt) {   /* stencil: mesmas funcoes e operacoes do PSP (teste (ref & mask) FUNC (stencil & mask)) */
        static const VkStencilOp so[] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INVERT, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP};
        vkCmdSetStencilTestEnable(cb, s.st != 0);
        vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_AND_BACK, so[s.sfail & 7], so[s.szpass & 7], so[s.szfail & 7], cf[s.sfunc & 7]);
        vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, s.smask & 255); vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, s.swmask & 255);
        vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, s.sref & 255);
    }
    float bf[4] = {(s.fix & 255) / 255.f, ((s.fix >> 8) & 255) / 255.f, ((s.fix >> 16) & 255) / 255.f, 1.f}; vkCmdSetBlendConstants(cb, bf);
    VkDescriptorSet tset = g_dummySet; bool tex = false;
    if (!s.clear && s.tex) {
        if (s.tex & GPU_FBTEX) {   /* textura = imagem da GPU de outro quadro sombra (render-to-texture) */
            int sid = (int)(s.tex & 0xff);
            if (sid != s.fb && sid < kMaxFb && g_fb[sid].used) { tset = g_fb[sid].set; tex = true; }
        } else if (s.tex < g_tex.size() && g_tex[s.tex].used) { tset = g_tex[s.tex].set; tex = true; }
    }
    bind_common(cb, g_plA, tset);
    PC pc = {};
    pc.tenv[0] = (s.tenv & 255) / 255.f; pc.tenv[1] = ((s.tenv >> 8) & 255) / 255.f; pc.tenv[2] = ((s.tenv >> 16) & 255) / 255.f;
    pc.flags = (tex ? 1u : 0u) | (s.talpha ? 2u : 0u) | (s.tdbl ? 4u : 0u) | ((s.atest && !s.clear) ? 8u : 0u) | (s.clear ? 16u : 0u) | ((s.aconst && !s.clear) ? 32u : 0u);
    pc.aconst = s.aconst & 255;
    pc.tfn = s.tfn; pc.afunc = s.afunc; pc.aref = s.aref; pc.amask = s.amask;
    pc.aover = (s.blend && s.bsf == 6) ? 1 : (s.blend && s.bsf == 7) ? 2 : (s.blend && s.bsf == 11) ? 3 : 0;
    pc.sidx = (s.linear ? 4u : 0u) | (s.clampu ? 2u : 0u) | (s.clampv ? 1u : 0u);
    pc.tsz[0] = (float)s.tw; pc.tsz[1] = (float)s.th;
    pc.sfix[0] = (s.sfix & 255) / 255.f; pc.sfix[1] = ((s.sfix >> 8) & 255) / 255.f; pc.sfix[2] = ((s.sfix >> 16) & 255) / 255.f;
    pc.fbsz[0] = (float)f.nw; pc.fbsz[1] = (float)f.nh;
    vkCmdPushConstants(cb, g_plA, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, &pc);
    vkCmdPushConstants(cb, g_plA, VK_SHADER_STAGE_VERTEX_BIT, 80, 16, pc.fbsz);
    if (!g_cs.vbBound) { VkDeviceSize o = 0; vkCmdBindVertexBuffers(cb, 0, 1, &S().vb.b, &o); g_cs.vbBound = true; }
    vkCmdDraw(cb, g_pend.count, 1, g_pend.first, 0);
}
static void vk_draw(const GpuState &s, const GpuVtx *v, int n) {
    if (n <= 0 || !g_dev) return;
    size_t bytes = (size_t)n * sizeof(GpuVtx);
    if (S().vbUsed + bytes > S().vb.size) {   /* anel de vertices cheio: grava o lote e envia este slot */
        record_pend(); submit_slot(false);
        if (bytes > S().vb.size) return;
    }
    Slot &sl = S(); uint32_t first = (uint32_t)(sl.vbUsed / sizeof(GpuVtx));
    memcpy(sl.vb.p + sl.vbUsed, v, bytes); sl.vbUsed += bytes;
    if (g_pend.valid && g_pend.first + g_pend.count == first && !memcmp(&g_pend.st, &s, sizeof s)) g_pend.count += n;
    else { record_pend(); g_pend.st = s; g_pend.first = first; g_pend.count = n; g_pend.valid = true; }
}
static void vk_flush(void) { double t = gpu_now_us(); record_pend(); g_gpu_us[0] += gpu_now_us() - t; }

/* ------------------------------------------------------------------ passes de tela cheia */
static void full_pass_begin(VkCommandBuffer cb, VkImageView target, int w, int h, VkImageView depth, bool clearBlack, VkImageView resolve = VK_NULL_HANDLE) {
    VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, da = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = target; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; ca.loadOp = clearBlack ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color = {{0, 0, 0, 1}};
    if (resolve) { ca.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT; ca.resolveImageView = resolve; ca.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO}; ri.renderArea = {{0, 0}, {(uint32_t)w, (uint32_t)h}}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
    VkRenderingAttachmentInfo sa = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depth) { da.imageView = depth; da.imageLayout = DS_LAYOUT; da.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; da.storeOp = VK_ATTACHMENT_STORE_OP_STORE; ri.pDepthAttachment = &da; }
    if (depth && g_hasSt) { sa = da; sa.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; sa.clearValue.depthStencil = {0.f, 0}; ri.pStencilAttachment = &sa; }
    vkCmdBeginRendering(cb, &ri);
    g_inRender = true; g_curFb = -2; cs_reset();
}
static void full_draw(VkCommandBuffer cb, VkPipeline pipe, VkPipelineLayout layout, VkDescriptorSet set0, const float p0[4], int vx, int vy, int vw, int vh, int cw, int ch) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkViewport vp = {(float)vx, (float)vy, (float)vw, (float)vh, 0, 1}; vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = {{0, 0}, {(uint32_t)cw, (uint32_t)ch}}; vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set0, 0, nullptr);
    if (layout == g_plA) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &g_sampSet, 0, nullptr);
    else vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &g_sampSet, 0, nullptr);
    float pc[4] = {p0[0], p0[1], p0[2], p0[3]}; vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
}
static void ensure_nat(void) {
    if (g_natC.i) return;
    make_img(g_natC, 512, 512, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    make_img(g_natZ, 512, 512, VK_FORMAT_R16_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    make_img(g_downT, 480, 272, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    g_natSet = alloc_set(g_lay2); write_set(g_natSet, 0, g_natC.v); write_set(g_natSet, 1, g_natZ.v);
}
static void vk_fb_upload(int id, const uint32_t *col, uint32_t cstride, const uint16_t *z, uint32_t zstride) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return;
    record_pend(); ensure_nat(); end_render();
    VkCommandBuffer cb = cbuf(); FbRec &f = g_fb[id];
    if (!upload_img(cb, g_natC, col, 4, cstride, f.nw, f.nh) || !upload_img(cb, g_natZ, z, 2, zstride, f.nw, f.nh)) {
        submit_slot(false); cb = cbuf();
        if (!upload_img(cb, g_natC, col, 4, cstride, f.nw, f.nh) || !upload_img(cb, g_natZ, z, 2, zstride, f.nw, f.nh)) return;
    }
    to_layout(cb, f.col, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (f.samples > 1) to_layout(cb, f.mc, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ds_to_attach(cb, f);
    full_pass_begin(cb, f.samples > 1 ? f.mc.v : f.col.v, f.w, f.h, f.dep.v, false, f.samples > 1 ? f.col.v : VK_NULL_HANDLE);   /* o stencil volta a 0 (o hle_ge marca como desconhecido) */
    float p0[4] = {(float)f.res, 0, 0, 0};
    full_draw(cb, g_pResync, g_plB, g_natSet, p0, 0, 0, f.w, f.h, f.w, f.h);
    end_render();
}
/* comparacao assincrona com o nativo: a reducao e a copia para um buffer do host entram no command buffer do quadro (sem esperar);
 * o resultado e lido 1-2 quadros depois, quando a fence do slot que a continha ja passou */
struct Ck { Buf rb; int slot; uint64_t serial; bool used; };
static Ck g_ck[4]; static int g_ckNext;
static int vk_check_start(int id) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return -1;
    record_pend(); ensure_nat(); end_render();
    int e = g_ckNext; g_ckNext = (g_ckNext + 1) & 3; Ck &c = g_ck[e];
    if (!c.rb.b && !make_buf(c.rb, 480 * 272 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { free_buf(c.rb); return -1; }
    VkCommandBuffer cb = cbuf(); FbRec &f = g_fb[id];
    to_layout(cb, f.col, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    to_layout(cb, g_downT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    full_pass_begin(cb, g_downT.v, 480, 272, VK_NULL_HANDLE, false);
    float p0[4] = {(float)f.res, 0, 0, 0};
    full_draw(cb, g_pDown, g_plA, f.set, p0, 0, 0, 480, 272, 480, 272);
    end_render();
    to_layout(cb, g_downT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy r = {}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {480, 272, 1};
    vkCmdCopyImageToBuffer(cb, g_downT.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c.rb.b, 1, &r);
    VkMemoryBarrier2 mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}; mb.srcStageMask = ST_TR; mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT; mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; di.memoryBarrierCount = 1; di.pMemoryBarriers = &mb; vkCmdPipelineBarrier2(cb, &di);
    c.slot = g_cur; c.serial = g_serial; c.used = true;   /* este command buffer sera enviado com serial = g_serial (ver submit_slot) */
    return e;
}
static bool vk_check_get(int e, uint32_t *out) {
    if (e < 0 || e > 3 || !g_ck[e].used) return false;
    Ck &c = g_ck[e]; double t0 = gpu_now_us();
    if (c.slot == g_cur && S().cbRec && c.serial == g_serial) { record_pend(); submit_slot(true); }   /* ainda nao enviado (uso no mesmo quadro): envia e espera */
    else { Slot &s = g_slot[c.slot]; if (s.inflight && s.serial == c.serial) vkWaitForFences(g_dev, 1, &s.fence, VK_TRUE, ~0ull); }
    c.used = false;
    memcpy(out, c.rb.p, 480 * 272 * 4); g_gpu_us[1] += gpu_now_us() - t0;
    return true;
}
static bool vk_fb_readback(int id, std::vector<uint32_t> &out, int *w, int *h) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return false;
    record_pend(); end_render(); FbRec &f = g_fb[id]; VkCommandBuffer cb = cbuf();
    to_layout(cb, f.col, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Buf rb = {}; if (!make_buf(rb, (size_t)f.w * f.h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { free_buf(rb); return false; }
    VkBufferImageCopy r = {}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {(uint32_t)f.w, (uint32_t)f.h, 1};
    vkCmdCopyImageToBuffer(cb, f.col.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.b, 1, &r);
    VkMemoryBarrier2 mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}; mb.srcStageMask = ST_TR; mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT; mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; di.memoryBarrierCount = 1; di.pMemoryBarriers = &mb; vkCmdPipelineBarrier2(cb, &di);
    submit_slot(true);
    out.resize((size_t)f.w * f.h); memcpy(out.data(), rb.p, out.size() * 4); *w = f.w; *h = f.h; free_buf(rb);
    return true;
}
/* anel de quadros reduzidos (caixa-preta do F8, ver gpu.h): a reducao entra no command buffer do quadro, antes da apresentacao; nada vem para a
 * RAM ate alguem ler (vk_ring_read, uma vez por F8) */
static std::vector<Img> g_ring; static std::vector<char> g_ringOk; static int g_ringW, g_ringH; static Buf g_ringRb;
static void ring_free_all(void) { for (auto &i : g_ring) free_img(i); g_ring.clear(); g_ringOk.clear(); free_buf(g_ringRb); g_ringW = g_ringH = 0; }
static bool vk_ring_push(int id, int slot, int nslots) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used || slot < 0 || slot >= nslots) return false;
    FbRec &f = g_fb[id]; const int n = (f.res + 1) / 2, w = f.w / n, h = f.h / n;
    record_pend(); end_render();
    if ((int)g_ring.size() != nslots || w != g_ringW || h != g_ringH) {   /* resolucao trocada (raro): refaz o anel */
        if (!g_ring.empty()) { wait_all(); ring_free_all(); }
        g_ring.assign(nslots, Img()); g_ringOk.assign(nslots, 0); g_ringW = w; g_ringH = h;
    }
    Img &r = g_ring[slot];
    if (!r.i && !make_img(r, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) { free_img(r); return false; }
    VkCommandBuffer cb = cbuf();
    to_layout(cb, f.col, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    to_layout(cb, r, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    full_pass_begin(cb, r.v, w, h, VK_NULL_HANDLE, false);
    float p0[4] = {(float)n, 0, 0, 0};
    full_draw(cb, g_pDown, g_plA, f.set, p0, 0, 0, w, h, w, h);
    end_render();
    g_ringOk[slot] = 1; return true;
}
static bool vk_ring_read(int slot, std::vector<uint32_t> &out, int *w, int *h) {
    if (slot < 0 || slot >= (int)g_ring.size() || !g_ringOk[slot] || !g_ring[slot].i) return false;
    record_pend(); end_render();
    if (!g_ringRb.b && !make_buf(g_ringRb, (size_t)g_ringW * g_ringH * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { free_buf(g_ringRb); return false; }
    Img &r = g_ring[slot]; VkCommandBuffer cb = cbuf();
    to_layout(cb, r, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c = {}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {(uint32_t)g_ringW, (uint32_t)g_ringH, 1};
    vkCmdCopyImageToBuffer(cb, r.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_ringRb.b, 1, &c);
    VkMemoryBarrier2 mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}; mb.srcStageMask = ST_TR; mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT; mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; di.memoryBarrierCount = 1; di.pMemoryBarriers = &mb; vkCmdPipelineBarrier2(cb, &di);
    submit_slot(true);
    out.resize((size_t)g_ringW * g_ringH); memcpy(out.data(), g_ringRb.p, out.size() * 4); *w = g_ringW; *h = g_ringH;
    return true;
}

/* ------------------------------------------------------------------ swapchain e apresentacao */
static void destroy_swap_views(void) {   /* semaforos e vistas das imagens da swapchain atual (a GPU precisa estar parada) */
    for (auto s : g_renderDone) vkDestroySemaphore(g_dev, s, nullptr); g_renderDone.clear();
    for (auto v : g_swapView) vkDestroyImageView(g_dev, v, nullptr); g_swapView.clear(); g_swapImg.clear();
}
static void destroy_swap(void) {
    if (!g_dev) return;
    wait_all(); destroy_swap_views();
    if (g_swap) { vkDestroySwapchainKHR(g_dev, g_swap, nullptr); g_swap = VK_NULL_HANDLE; }
}
static int g_swapSync;   /* g_gfx.sync com que a swapchain atual foi criada */
static bool create_swap(int cw, int ch, VkSwapchainKHR old = VK_NULL_HANDLE) {
    VkSurfaceCapabilitiesKHR caps; CHK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_pd, g_surf, &caps));
    uint32_t nf = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(g_pd, g_surf, &nf, nullptr); std::vector<VkSurfaceFormatKHR> fm(nf); vkGetPhysicalDeviceSurfaceFormatsKHR(g_pd, g_surf, &nf, fm.data());
    VkSurfaceFormatKHR sf = fm.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : fm[0];
    for (auto &f : fm) if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) { sf = f; break; }
    uint32_t np = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(g_pd, g_surf, &np, nullptr); std::vector<VkPresentModeKHR> pm(np); vkGetPhysicalDeviceSurfacePresentModesKHR(g_pd, g_surf, &np, pm.data());
    /* modo de apresentacao (menu Graficos -> Sincronizacao): 0 = MAILBOX > IMMEDIATE > FIFO (padrao: sem espera de vsync e sem tearing; o ritmo e do
     * limitador do jogo), 1 = FIFO (vsync), 2 = IMMEDIATE > MAILBOX > FIFO (menor latencia, pode rasgar) */
    g_swapSync = g_gfx.sync;
    auto has = [&](VkPresentModeKHR w) { for (auto m : pm) if (m == w) return true; return false; };
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (g_swapSync == 2) { if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) mode = VK_PRESENT_MODE_IMMEDIATE_KHR; else if (has(VK_PRESENT_MODE_MAILBOX_KHR)) mode = VK_PRESENT_MODE_MAILBOX_KHR; }
    else if (g_swapSync == 0) { if (has(VK_PRESENT_MODE_MAILBOX_KHR)) mode = VK_PRESENT_MODE_MAILBOX_KHR; else if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) mode = VK_PRESENT_MODE_IMMEDIATE_KHR; }
    VkExtent2D ext = caps.currentExtent.width != 0xffffffffu ? caps.currentExtent : VkExtent2D{(uint32_t)cw, (uint32_t)ch};
    if (!ext.width || !ext.height) return false;
    uint32_t cnt = std::max(3u, caps.minImageCount); if (caps.maxImageCount && cnt > caps.maxImageCount) cnt = caps.maxImageCount;
    VkSwapchainCreateInfoKHR si = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; si.surface = g_surf; si.minImageCount = cnt; si.imageFormat = sf.format; si.imageColorSpace = sf.colorSpace; si.imageExtent = ext;
    si.imageArrayLayers = 1; si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; si.preTransform = caps.currentTransform;
    si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; si.presentMode = mode; si.clipped = VK_TRUE; si.oldSwapchain = old;
    CHK(vkCreateSwapchainKHR(g_dev, &si, nullptr, &g_swap));
    g_swapFmt = sf.format; g_swapExt = ext;
    if (g_dbg) fprintf(stderr, "[vulkan] swapchain %ux%u (janela %dx%d, currentExtent %ux%u)\n", ext.width, ext.height, cw, ch, caps.currentExtent.width, caps.currentExtent.height);
    uint32_t n = 0; vkGetSwapchainImagesKHR(g_dev, g_swap, &n, nullptr); g_swapImg.resize(n); vkGetSwapchainImagesKHR(g_dev, g_swap, &n, g_swapImg.data());
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = g_swapImg[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = sf.format; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v; CHK(vkCreateImageView(g_dev, &vi, nullptr, &v)); g_swapView.push_back(v);
        VkSemaphoreCreateInfo sci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkSemaphore sm; CHK(vkCreateSemaphore(g_dev, &sci, nullptr, &sm)); g_renderDone.push_back(sm);
    }
    /* pipelines de apresentacao dependem do formato da swapchain */
    if (g_pPresentFmt != sf.format) {
        if (g_pPresent) vkDestroyPipeline(g_dev, g_pPresent, nullptr); if (g_pOverlay) vkDestroyPipeline(g_dev, g_pOverlay, nullptr);
        PipeDesc d = {g_plA, g_sm[SM_FULL_VERT], g_sm[SM_PRESENT], false, sf.format, VK_FORMAT_UNDEFINED, blend_off(), false, false};
        g_pPresent = make_pipe(d);
        VkPipelineColorBlendAttachmentState b = blend_off(); b.blendEnable = VK_TRUE; b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; b.colorBlendOp = VK_BLEND_OP_ADD;
        b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; b.alphaBlendOp = VK_BLEND_OP_ADD;
        PipeDesc o = {g_plA, g_sm[SM_FULL_VERT], g_sm[SM_OVERLAY], false, sf.format, VK_FORMAT_UNDEFINED, b, false, false};
        g_pOverlay = make_pipe(o); g_pPresentFmt = sf.format;
    }
    return true;
}
static bool ensure_swap(void) {
    if (!g_dev || !g_hwnd) return false;
    RECT cr; GetClientRect(g_hwnd, &cr); int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
    if (cw <= 0 || ch <= 0) return false;   /* minimizada */
    if (!g_surf) {
        VkWin32SurfaceCreateInfoKHR ci = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR}; ci.hinstance = GetModuleHandleA(nullptr); ci.hwnd = g_hwnd;
        CHK(vkCreateWin32SurfaceKHR(g_inst, &ci, nullptr, &g_surf));
        VkBool32 ok = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(g_pd, g_qf, g_surf, &ok); if (!ok) { fprintf(stderr, "[vulkan] a fila nao apresenta nessa janela\n"); return false; }
    }
    if (!g_swap) return create_swap(cw, ch);
    if ((int)g_swapExt.width != cw || (int)g_swapExt.height != ch || g_swapSync != g_gfx.sync) {   /* janela redimensionada ou modo de sincronizacao trocado no menu: recria passando a antiga (oldSwapchain) */
        wait_all(); destroy_swap_views();
        VkSwapchainKHR old = g_swap; g_swap = VK_NULL_HANDLE;
        bool ok = create_swap(cw, ch, old);
        vkDestroySwapchainKHR(g_dev, old, nullptr);
        return ok;
    }
    return true;
}
struct Last { int kind; int fb; int w, h; };   /* kind: 0 nada, 1 quadro sombra, 2 pixels nativos */
static Last g_last;
static void present_source(VkDescriptorSet srcSet, Img *srcImg, int sw, int sh) {
    double tp = gpu_now_us();
    record_pend(); end_render();
    if (!ensure_swap()) return;
    /* 1 acquire (com o semaforo do slot); recria a swapchain se ficou desatualizada */
    uint32_t idx = 0; VkResult r;
    for (int tries = 0; tries < 2; tries++) {
        r = vkAcquireNextImageKHR(g_dev, g_swap, ~0ull, S().acquire, VK_NULL_HANDLE, &idx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) { if (r == VK_ERROR_OUT_OF_DATE_KHR) { destroy_swap(); if (!ensure_swap()) return; continue; } }
        break;
    }
    if (g_dbg && r != VK_SUCCESS) { static int na; if (na++ < 20) fprintf(stderr, "[vulkan] acquire: %s\n", vkres(r)); }
    if (r < 0) return;
    VkCommandBuffer cb = cbuf();
    if (srcImg) to_layout(cb, *srcImg, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cb, g_swapImg[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, ST_COL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    int cw = (int)g_swapExt.width, ch = (int)g_swapExt.height, w, h;   /* mesma conta de gfx_paint */
    if (g_gfx.aspect == GFX_ASPECT_STRETCH) { w = cw; h = ch; }
    else {
        int an = g_gfx.aspect == GFX_ASPECT_4_3 ? 4 : 480, ad = g_gfx.aspect == GFX_ASPECT_4_3 ? 3 : 272;
        w = cw; h = (int)((long long)cw * ad / an); if (h > ch) { h = ch; w = (int)((long long)ch * an / ad); }
        if (g_gfx.integer) { int n = std::min(cw / 480, ch / 272); if (n >= 1) { w = 480 * n; h = 272 * n; if (g_gfx.aspect == GFX_ASPECT_4_3) { w = h * 4 / 3; if (w > cw) w = cw; } } }
    }
    int x = (cw - w) / 2, y = (ch - h) / 2;
    full_pass_begin(cb, g_swapView[idx], cw, ch, VK_NULL_HANDLE, true);
    float k = std::max(1.f, (float)w / (float)sw);
    float mode = g_gfx.filter == GFX_FILTER_NEAREST ? 0.f : g_gfx.filter == GFX_FILTER_BILINEAR ? 2.f : 1.f;
    if (w == sw && h == sh) mode = 0.f;
    if (mode != 0.f && (w < sw || h < sh)) { mode = 3.f; k = std::max((float)sw / (float)w, (float)sh / (float)h); }   /* reducao: supersampling no sombreador */
    float p0[4] = {mode, k, (float)sw, (float)sh};
    full_draw(cb, g_pPresent, g_plA, srcSet, p0, x, y, w, h, cw, ch);
    if (g_ovOn) { float z[4] = {}; full_draw(cb, g_pOverlay, g_plA, g_ovSet, z, x, y, w, h, cw, ch); }
    end_render();
    barrier(cb, g_swapImg[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, ST_COL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    g_waitSem = S().acquire; g_sigSem = g_renderDone[idx];
    submit_slot(false);
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; VkSemaphore rd = g_renderDone[idx]; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &rd; pi.swapchainCount = 1; pi.pSwapchains = &g_swap; pi.pImageIndices = &idx;
    r = vkQueuePresentKHR(g_q, &pi);
    if (g_dbg && r != VK_SUCCESS) { static int np; if (np++ < 20) fprintf(stderr, "[vulkan] present: %s\n", vkres(r)); }
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { destroy_swap(); }
    g_gpu_us[2] += gpu_now_us() - tp;
    static uint64_t nfr; if (++nfr % 300 == 0) save_pcache();
}
static void vk_present_fb(int id) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return;
    g_last = {1, id, g_fb[id].w, g_fb[id].h};
    present_source(g_fb[id].set, &g_fb[id].col, g_fb[id].w, g_fb[id].h);
}
static void vk_present_pixels(const uint32_t *xrgb, int w, int h) {
    if (!g_dev) return;
    record_pend(); end_render();
    if (g_pixT.i && (g_pixT.w != w || g_pixT.h != h)) { vkDeviceWaitIdle(g_dev); free_img(g_pixT); }   /* tamanho novo (video substituto HD) */
    if (!g_pixT.i) { make_img(g_pixT, w, h, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT); if (!g_pixSet) g_pixSet = alloc_set(g_lay1); write_set(g_pixSet, 0, g_pixT.v); }
    VkCommandBuffer cb = cbuf();
    if (!upload_img(cb, g_pixT, xrgb, 4, (uint32_t)w, w, h)) { submit_slot(false); cb = cbuf(); if (!upload_img(cb, g_pixT, xrgb, 4, (uint32_t)w, w, h)) return; }
    g_last = {2, 0, w, h};
    present_source(g_pixSet, &g_pixT, w, h);
}
static void vk_present_last(void) {
    if (g_last.kind == 1 && g_last.fb >= 0 && g_fb[g_last.fb].used) present_source(g_fb[g_last.fb].set, &g_fb[g_last.fb].col, g_fb[g_last.fb].w, g_fb[g_last.fb].h);
    else if (g_last.kind == 2 && g_pixT.i) present_source(g_pixSet, &g_pixT, g_last.w, g_last.h);
}
static void vk_set_overlay(const uint32_t *rgba) {
    if (!g_dev) return;
    g_ovOn = rgba != nullptr; if (!rgba) return;
    if (!g_ovT.i) { make_img(g_ovT, GPU_OVW, GPU_OVH, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT); g_ovSet = alloc_set(g_lay1); write_set(g_ovSet, 0, g_ovT.v); }
    record_pend(); end_render();
    VkCommandBuffer cb = cbuf();
    if (!upload_img(cb, g_ovT, rgba, 4, GPU_OVW, GPU_OVW, GPU_OVH)) { submit_slot(false); cb = cbuf(); upload_img(cb, g_ovT, rgba, 4, GPU_OVW, GPU_OVW, GPU_OVH); }
}

/* ------------------------------------------------------------------ inicializacao */
static void destroy_all(void) {
    if (!g_dev) return;
    wait_all(); save_pcache();
    destroy_swap();
    for (int i = 0; i < kMaxFb; i++) if (g_fb[i].used) { FbRec &f = g_fb[i]; free_img(f.col); free_img(f.dep); free_img(f.mc); f = FbRec(); }
    g_msaa = VK_SAMPLE_COUNT_1_BIT;
    for (auto &t : g_tex) if (t.used) free_img(t.img); g_tex.assign(1, TexRec()); g_texFree.clear();
    free_dead_upto(~0ull);
    free_img(g_natC); free_img(g_natZ); free_img(g_downT); free_img(g_ovT); free_img(g_pixT); g_pixSet = VK_NULL_HANDLE; free_img(g_dummy); free_buf(g_rb);
    ring_free_all();
    for (auto &kv : g_pipes) vkDestroyPipeline(g_dev, kv.second, nullptr); g_pipes.clear();
    for (VkPipeline *p : {&g_pResync, &g_pDown, &g_pPresent, &g_pOverlay}) if (*p) { vkDestroyPipeline(g_dev, *p, nullptr); *p = VK_NULL_HANDLE; }
    g_pPresentFmt = VK_FORMAT_UNDEFINED;
    if (g_pcache) vkDestroyPipelineCache(g_dev, g_pcache, nullptr); g_pcache = VK_NULL_HANDLE;
    for (auto &c : g_ck) { free_buf(c.rb); c = Ck(); }
    for (auto &s : g_slot) { free_buf(s.vb); free_buf(s.st); if (s.fence) vkDestroyFence(g_dev, s.fence, nullptr); if (s.acquire) vkDestroySemaphore(g_dev, s.acquire, nullptr); if (s.pool) vkDestroyCommandPool(g_dev, s.pool, nullptr); s = Slot(); }
    for (auto &m : g_sm) if (m) { vkDestroyShaderModule(g_dev, m, nullptr); m = VK_NULL_HANDLE; }
    for (auto &s : g_smp) if (s) { vkDestroySampler(g_dev, s, nullptr); s = VK_NULL_HANDLE; }
    if (g_dpool) vkDestroyDescriptorPool(g_dev, g_dpool, nullptr); g_dpool = VK_NULL_HANDLE;
    if (g_plA) vkDestroyPipelineLayout(g_dev, g_plA, nullptr); if (g_plB) vkDestroyPipelineLayout(g_dev, g_plB, nullptr); g_plA = g_plB = VK_NULL_HANDLE;
    if (g_lay1) vkDestroyDescriptorSetLayout(g_dev, g_lay1, nullptr); if (g_lay2) vkDestroyDescriptorSetLayout(g_dev, g_lay2, nullptr); if (g_laySamp) vkDestroyDescriptorSetLayout(g_dev, g_laySamp, nullptr);
    g_lay1 = g_lay2 = g_laySamp = VK_NULL_HANDLE;
    if (g_surf) { vkDestroySurfaceKHR(g_inst, g_surf, nullptr); g_surf = VK_NULL_HANDLE; }
    vkDestroyDevice(g_dev, nullptr); g_dev = VK_NULL_HANDLE;
    if (g_inst) { vkDestroyInstance(g_inst, nullptr); g_inst = VK_NULL_HANDLE; }
    g_inited = false; g_allocs = 0; g_pend = Pend(); g_inRender = false; g_curFb = -1; g_cur = 0;
}
static bool load_lib(void) {
    if (g_lib) return true;
    g_lib = LoadLibraryA("vulkan-1.dll"); if (!g_lib) { fprintf(stderr, "[vulkan] vulkan-1.dll nao encontrada\n"); return false; }
    vkGetInstanceProcAddr_ = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(g_lib, "vkGetInstanceProcAddr"); if (!vkGetInstanceProcAddr_) return false;
#define X(n) n = (PFN_##n)(void *)vkGetInstanceProcAddr_(nullptr, #n);
    VK_GLOBAL_FUNCS(X)
#undef X
    return vkCreateInstance != nullptr;
}
static bool build_device(void) {
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.pApplicationName = "Tag Force Recomp"; ai.apiVersion = VK_API_VERSION_1_3;
    const char *iext[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkInstanceCreateInfo ic = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ic.pApplicationInfo = &ai; ic.enabledExtensionCount = 2; ic.ppEnabledExtensionNames = iext;
    CHK(vkCreateInstance(&ic, nullptr, &g_inst));
#define X(n) n = (PFN_##n)(void *)vkGetInstanceProcAddr_(g_inst, #n);
    VK_INSTANCE_FUNCS(X)
#undef X
    uint32_t np = 0; vkEnumeratePhysicalDevices(g_inst, &np, nullptr); std::vector<VkPhysicalDevice> pds(np); vkEnumeratePhysicalDevices(g_inst, &np, pds.data());
    int best = -1, bestScore = -1; uint32_t bestQf = 0;
    for (uint32_t i = 0; i < np; i++) {
        VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(pds[i], &pr);
        if (pr.apiVersion < VK_API_VERSION_1_3) continue;
        uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, nullptr); std::vector<VkQueueFamilyProperties> qp(nq); vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, qp.data());
        int qf = -1; for (uint32_t q = 0; q < nq; q++) if (qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qf = (int)q; break; }
        if (qf < 0) continue;
        uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pds[i], nullptr, &ne, nullptr); std::vector<VkExtensionProperties> ex(ne); vkEnumerateDeviceExtensionProperties(pds[i], nullptr, &ne, ex.data());
        bool sw = false; for (auto &e : ex) if (!strcmp(e.extensionName, "VK_KHR_swapchain")) sw = true;
        if (!sw) continue;
        int score = pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > bestScore) { bestScore = score; best = (int)i; bestQf = (uint32_t)qf; }
    }
    if (best < 0) { fprintf(stderr, "[vulkan] nenhuma GPU com Vulkan 1.3\n"); return false; }
    g_pd = pds[best]; g_qf = bestQf;
    VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(g_pd, &pr); snprintf(g_adapter, sizeof g_adapter, "%s (Vulkan %u.%u.%u)", pr.deviceName, VK_VERSION_MAJOR(pr.apiVersion), VK_VERSION_MINOR(pr.apiVersion), VK_VERSION_PATCH(pr.apiVersion));
    vkGetPhysicalDeviceMemoryProperties(g_pd, &g_mem);
    g_maxSamples = pr.limits.framebufferColorSampleCounts & pr.limits.framebufferDepthSampleCounts;
    /* profundidade + stencil (o stencil emula o alfa do framebuffer do PSP); TF_GPU_NOSTENCIL=1: so D16, desenhos com stencil voltam ao raster nativo */
    g_dsFmt = VK_FORMAT_D16_UNORM; g_hasSt = false;
    if (!getenv("TF_GPU_NOSTENCIL")) for (VkFormat cand : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(g_pd, cand, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { g_dsFmt = cand; g_hasSt = true; g_maxSamples &= pr.limits.framebufferStencilSampleCounts; break; }
    }
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(g_pd, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2) { fprintf(stderr, "[vulkan] falta dynamicRendering/synchronization2\n"); return false; }
    VkPhysicalDeviceVulkan13Features en13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; en13.dynamicRendering = VK_TRUE; en13.synchronization2 = VK_TRUE;
    float prio = 1.f; VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = g_qf; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    const char *dext[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dc = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dc.pNext = &en13; dc.queueCreateInfoCount = 1; dc.pQueueCreateInfos = &qi; dc.enabledExtensionCount = 1; dc.ppEnabledExtensionNames = dext;
    CHK(vkCreateDevice(g_pd, &dc, nullptr, &g_dev));
#define X(n) n = (PFN_##n)(void *)vkGetDeviceProcAddr(g_dev, #n);
    VK_DEVICE_FUNCS(X)
#undef X
    vkGetDeviceQueue(g_dev, g_qf, 0, &g_q);
    return true;
}
static bool build_resources(void) {
    /* samplers: i = linear*4 + clampu*2 + clampv */
    for (int i = 0; i < 8; i++) {
        VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.magFilter = si.minFilter = (i & 4) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = (i & 2) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT; si.addressModeV = (i & 1) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxLod = VK_LOD_CLAMP_NONE;
        CHK(vkCreateSampler(g_dev, &si, nullptr, &g_smp[i]));
    }
    VkDescriptorSetLayoutBinding b0 = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, b1 = {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; lc.bindingCount = 1; lc.pBindings = &b0; CHK(vkCreateDescriptorSetLayout(g_dev, &lc, nullptr, &g_lay1));
    VkDescriptorSetLayoutBinding two[2] = {b0, b1}; lc.bindingCount = 2; lc.pBindings = two; CHK(vkCreateDescriptorSetLayout(g_dev, &lc, nullptr, &g_lay2));
    VkDescriptorSetLayoutBinding bs = {0, VK_DESCRIPTOR_TYPE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, g_smp}; lc.bindingCount = 1; lc.pBindings = &bs; CHK(vkCreateDescriptorSetLayout(g_dev, &lc, nullptr, &g_laySamp));
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5000}, {VK_DESCRIPTOR_TYPE_SAMPLER, 8}};
    VkDescriptorPoolCreateInfo pc = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pc.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pc.maxSets = 4100; pc.poolSizeCount = 2; pc.pPoolSizes = ps;
    CHK(vkCreateDescriptorPool(g_dev, &pc, nullptr, &g_dpool));
    g_sampSet = alloc_set(g_laySamp);   /* samplers imutaveis: nada a escrever */
    VkPushConstantRange pr[2] = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80}, {VK_SHADER_STAGE_VERTEX_BIT, 80, 16}};
    VkDescriptorSetLayout la[2] = {g_lay1, g_laySamp}, lb[2] = {g_lay2, g_laySamp};
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount = 2; pl.pSetLayouts = la; pl.pushConstantRangeCount = 2; pl.pPushConstantRanges = pr;
    CHK(vkCreatePipelineLayout(g_dev, &pl, nullptr, &g_plA)); pl.pSetLayouts = lb; CHK(vkCreatePipelineLayout(g_dev, &pl, nullptr, &g_plB));
    g_sm[SM_MAIN_VERT] = mk_sm(spv_main_vert, sizeof spv_main_vert); g_sm[SM_MAIN_FRAG] = mk_sm(spv_main_frag, sizeof spv_main_frag); g_sm[SM_FULL_VERT] = mk_sm(spv_full_vert, sizeof spv_full_vert);
    g_sm[SM_RESYNC] = mk_sm(spv_resync_frag, sizeof spv_resync_frag); g_sm[SM_DOWN] = mk_sm(spv_down_frag, sizeof spv_down_frag); g_sm[SM_PRESENT] = mk_sm(spv_present_frag, sizeof spv_present_frag);
    g_sm[SM_OVERLAY] = mk_sm(spv_overlay_frag, sizeof spv_overlay_frag);
    g_pcPath = exe_dir() + L"tagforce_vk_cache.bin";
    load_pcache();
    PipeDesc rs = {g_plB, g_sm[SM_FULL_VERT], g_sm[SM_RESYNC], false, VK_FORMAT_R8G8B8A8_UNORM, g_dsFmt, blend_off(), false, true, g_msaa}; g_pResync = make_pipe(rs);
    PipeDesc dn = {g_plA, g_sm[SM_FULL_VERT], g_sm[SM_DOWN], false, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, blend_off(), false, false}; g_pDown = make_pipe(dn);
    for (int i = 0; i < NF; i++) {
        Slot &s = g_slot[i];
        VkCommandPoolCreateInfo cp = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.queueFamilyIndex = g_qf; CHK(vkCreateCommandPool(g_dev, &cp, nullptr, &s.pool));
        VkCommandBufferAllocateInfo ca = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = s.pool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
        CHK(vkAllocateCommandBuffers(g_dev, &ca, &s.up)); CHK(vkAllocateCommandBuffers(g_dev, &ca, &s.cb));
        VkFenceCreateInfo fc = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; CHK(vkCreateFence(g_dev, &fc, nullptr, &s.fence));
        VkSemaphoreCreateInfo sc = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; CHK(vkCreateSemaphore(g_dev, &sc, nullptr, &s.acquire));
        const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        /* vertices: memoria visivel pela CPU e local da GPU (ReBAR) se houver, senao so visivel; upload: memoria do sistema */
        if (!make_buf(s.vb, VB_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hv | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { s.vb = Buf(); if (!make_buf(s.vb, VB_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hv)) return false; }
        if (!make_buf(s.st, ST_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, hv, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    }
    /* textura 1x1 para desenhos sem textura */
    if (!make_img(g_dummy, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
    g_dummySet = alloc_set(g_lay1); write_set(g_dummySet, 0, g_dummy.v);
    g_cur = 0; cs_reset();
    uint32_t px = 0xffffffffu; if (!upload_img(upbuf(), g_dummy, &px, 4, 1, 1, 1)) return false;
    return true;
}
static bool vk_init(HWND wnd) {
    g_hwnd = wnd; g_dbg = getenv("TF_GPU_DEBUG") != nullptr;
    if (g_inited) return ensure_swap();
    if (!load_lib() || !build_device()) { destroy_all(); return false; }
    if (!build_resources()) { fprintf(stderr, "[vulkan] falha ao criar os recursos\n"); destroy_all(); return false; }
    g_inited = true;
    fprintf(stderr, "[gpu] Vulkan em: %s (profundidade %s)\n", g_adapter, g_dsFmt == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : g_dsFmt == VK_FORMAT_D32_SFLOAT_S8_UINT ? "D32S8" : "D16, sem stencil");
    if (!ensure_swap()) { destroy_all(); return false; }
    return true;
}
static bool vk_ready(void) { return g_inited && g_swap != VK_NULL_HANDLE; }
static bool vk_has_stencil(void) { return g_inited && g_hasSt; }
static void vk_release_swapchain(void) {
    if (!g_inited) return;
    record_pend(); submit_slot(true);
    destroy_all();   /* solta tambem a superficie: a janela pode passar a ser usada por outra API (D3D11/GDI) */
}
static const char *vk_adapter(void) { return g_adapter; }

const GpuBackend gpu_backend_vulkan = {"Vulkan", vk_init, vk_ready, vk_release_swapchain, vk_adapter, vk_fb_create, vk_fb_destroy, vk_fb_upload, vk_check_start, vk_check_get,
    vk_fb_readback, vk_draw, vk_flush, vk_tex_create, vk_tex_release, vk_present_fb, vk_present_pixels, vk_present_last, vk_set_overlay, vk_has_stencil,
    vk_ring_push, vk_ring_read};
