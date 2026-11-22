VulkanRenderer::VulkanRenderer() = default;

VulkanRenderer::~VulkanRenderer() {
    if (!impl_) {
        return;
    }
    if (impl_->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl_->device);
        if (impl_->fence != VK_NULL_HANDLE) {
            vkDestroyFence(impl_->device, impl_->fence, nullptr);
        }
        if (impl_->commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(impl_->device, impl_->commandPool, nullptr);
        }
        vkDestroyDevice(impl_->device, nullptr);
    }
    if (impl_->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(impl_->instance, nullptr);
    }
    delete impl_;
    impl_ = nullptr;
}

bool VulkanRenderer::initialize(const RenderOptions& options, std::string& error) {
    if (impl_) {
        error = "renderer is already initialized";
        return false;
    }
    impl_ = new Impl();
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS) {
        loaderVersion = VK_API_VERSION_1_0;
    }
    impl_->info.vulkanLoaderVersion = vulkanVersionString(loaderVersion);
    if (!impl_->createInstance(options, error) ||
        !impl_->selectPhysicalDevice(options, error) ||
        !impl_->createDevice(error)) {
        return false;
    }
    const std::filesystem::path shaderDir(RR_SHADER_DIR);
    impl_->vertexSpirv = loadSpirv(shaderDir / "triangle.vert.spv", error);
    if (impl_->vertexSpirv.empty()) {
        return false;
    }
    impl_->fragmentSpirv = loadSpirv(shaderDir / "triangle.frag.spv", error);
    return !impl_->fragmentSpirv.empty();
}

bool VulkanRenderer::initialized() const {
    return impl_ && impl_->device != VK_NULL_HANDLE;
}

const DeviceInfo& VulkanRenderer::deviceInfo() const {
    static const DeviceInfo empty;
    return impl_ ? impl_->info : empty;
}

RenderPassResult VulkanRenderer::renderPass(const Scenario& scenario,
                                            const Pass& pass,
                                            const RenderOptions& options) {
    RenderPassResult result;
    result.passId = pass.id;
    result.ruleId = pass.rule;
    const auto start = std::chrono::steady_clock::now();
    const Rule* rule = findRule(scenario, pass.rule);
    if (!impl_ || !impl_->device) {
        result.error = "renderer is not initialized";
        return result;
    }
    if (!rule) {
        result.error = "pass rule does not exist";
        return result;
    }
    if (scenario.width > impl_->info.maxFramebufferWidth ||
        scenario.height > impl_->info.maxFramebufferHeight) {
        result.error = "framebuffer extent exceeds device limits";
        return result;
    }

    ColorTarget target;
    Buffer vertexBuffer;
    Buffer indexBuffer;
    Buffer readback;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    PipelineResult pipeline;
    bool destroyed = false;
    auto cleanup = [&]() {
        if (destroyed) {
            return;
        }
        destroyed = true;
        if (pipeline.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(impl_->device, pipeline.pipeline, nullptr);
        }
        if (pipeline.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(impl_->device, pipeline.layout, nullptr);
        }
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(impl_->device, renderPass, nullptr);
        }
        destroyBuffer(impl_->device, readback);
        destroyBuffer(impl_->device, indexBuffer);
        destroyBuffer(impl_->device, vertexBuffer);
        destroyTarget(impl_->device, target);
    };

    if (!createColorTarget(*impl_, scenario.width, scenario.height, target,
                           result.error)) {
        cleanup();
        return result;
    }
    if (!scenario.vertices.empty()) {
        if (!createBuffer(*impl_, scenario.vertices.size() * sizeof(Vertex),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer,
                          result.error) ||
            !uploadBuffer(*impl_, vertexBuffer, scenario.vertices.data(),
                          scenario.vertices.size() * sizeof(Vertex), result.error)) {
            cleanup();
            return result;
        }
    }
    if (!scenario.indices.empty()) {
        if (!createBuffer(*impl_, scenario.indices.size() * sizeof(std::uint32_t),
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer,
                          result.error) ||
            !uploadBuffer(*impl_, indexBuffer, scenario.indices.data(),
                          scenario.indices.size() * sizeof(std::uint32_t),
                          result.error)) {
            cleanup();
            return result;
        }
    }

    if (!clearTarget(*impl_, target, scenario.clear_color, result.error) ||
        !copyTargetToBuffer(*impl_, target, readback, result.initialImage,
                            result.error)) {
        cleanup();
        return result;
    }
    result.initialSha256 = sha256Hex(result.initialImage.rgba);
    Image previous = result.initialImage;

    VkAttachmentDescription attachment{};
    attachment.format = kColorFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkRenderPassCreateInfo renderPassCreate{};
    renderPassCreate.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreate.attachmentCount = 1;
    renderPassCreate.pAttachments = &attachment;
    renderPassCreate.subpassCount = 1;
    renderPassCreate.pSubpasses = &subpass;
    if (!vkOk(vkCreateRenderPass(impl_->device, &renderPassCreate, nullptr, &renderPass),
              result.error, "vkCreateRenderPass")) {
        cleanup();
        return result;
    }
    pipeline = createPipeline(*impl_, *rule, scenario.width, scenario.height,
                              renderPass, result.error);
    result.warnings = pipeline.warnings;
    result.effectiveRule = pipeline.effectiveRule;
    if (pipeline.pipeline == VK_NULL_HANDLE) {
        cleanup();
        return result;
    }

    result.draws.reserve(pass.draws.size());
    for (std::size_t i = 0; i < pass.draws.size(); ++i) {
        const Draw* draw = findDraw(scenario, pass.draws[i]);
        if (!draw) {
            result.error = "pass references a missing draw";
            cleanup();
            return result;
        }
        if (static_cast<std::uint64_t>(draw->first_index) + draw->index_count >
            scenario.indices.size()) {
            result.error = "draw range exceeds the index buffer";
            cleanup();
            return result;
        }
        if (!beginCommands(*impl_, result.error)) {
            cleanup();
            return result;
        }
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkFramebufferCreateInfo framebufferCreate{};
        framebufferCreate.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreate.renderPass = renderPass;
        framebufferCreate.attachmentCount = 1;
        framebufferCreate.pAttachments = &target.view;
        framebufferCreate.width = target.extent.width;
        framebufferCreate.height = target.extent.height;
        framebufferCreate.layers = 1;
        if (!vkOk(vkCreateFramebuffer(impl_->device, &framebufferCreate, nullptr,
                                     &framebuffer),
                  result.error, "vkCreateFramebuffer")) {
            vkEndCommandBuffer(impl_->commandBuffer);
            cleanup();
            return result;
        }
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = renderPass;
        begin.framebuffer = framebuffer;
        begin.renderArea.offset = {0, 0};
        begin.renderArea.extent = target.extent;
        vkCmdBeginRenderPass(impl_->commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(impl_->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline.pipeline);
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(impl_->commandBuffer, 0, 1, &vertexBuffer.buffer,
                               &vertexOffset);
        vkCmdBindIndexBuffer(impl_->commandBuffer, indexBuffer.buffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(impl_->commandBuffer, draw->index_count, draw->instance_count,
                         draw->first_index, 0, 0);
        vkCmdEndRenderPass(impl_->commandBuffer);
        const bool submitted = endSubmitWait(*impl_, result.error);
        vkDestroyFramebuffer(impl_->device, framebuffer, nullptr);
        if (!submitted) {
            cleanup();
            return result;
        }
        Image after;
        if (!copyTargetToBuffer(*impl_, target, readback, after, result.error)) {
            cleanup();
            return result;
        }
        DrawRender drawResult;
        drawResult.sequence = i;
        drawResult.id = draw->id;
        drawResult.firstIndex = draw->first_index;
        drawResult.indexCount = draw->index_count;
        drawResult.instanceCount = draw->instance_count;
        drawResult.beforeSha256 = sha256Hex(previous.rgba);
        drawResult.afterSha256 = sha256Hex(after.rgba);
        drawResult.change = compareImages(previous, after);
        if (options.savedDrawIndices.count(i) != 0U) {
            drawResult.hasSavedImage = true;
            drawResult.savedImage = after;
        }
        result.draws.push_back(std::move(drawResult));
        previous = std::move(after);
    }

    result.finalImage = std::move(previous);
    result.finalSha256 = sha256Hex(result.finalImage.rgba);
    result.ok = true;
    const auto end = std::chrono::steady_clock::now();
    result.elapsedMicroseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    cleanup();
    return result;
}

std::string hexColor(const std::array<std::uint8_t, 4>& rgba) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x",
                  static_cast<unsigned>(rgba[0]), static_cast<unsigned>(rgba[1]),
                  static_cast<unsigned>(rgba[2]), static_cast<unsigned>(rgba[3]));
    return std::string(buffer);
}

std::string vulkanVersionString(std::uint32_t version) {
    return versionString(version);
}
