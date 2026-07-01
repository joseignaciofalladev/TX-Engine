#include "../Application/EngineApplication.h"
#include <Renderer/GraphicsUBO.h>

void EngineApplication::createDescriptorSetLayout()
{
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
        {{.binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex},
         {.binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment}} };

    vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data() };
    descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
}

void EngineApplication::createGraphicsPipeline()
{
    // La canalización gráfica define el proceso de renderizado completo:
    //
    // Input Assembler
    // Vertex Shader
    // Rasterizer
    // Fragment Shader
    // Color Blending
    //
    // Vulkan requiere que casi todo el estado de la canalización se cree por
    // adelantado, lo que permite al controlador realizar optimizaciones agresivas.
    vk::raii::ShaderModule shaderModule = createShaderModule(readFile("shaders/slang.spv"));

    vk::PipelineShaderStageCreateInfo
        vertShaderStageInfo{
            .stage =
                vk::ShaderStageFlagBits::eVertex,
            .module = *shaderModule,
            .pName = "vertMain"
    };

    vk::PipelineShaderStageCreateInfo
        fragShaderStageInfo{
            .stage =
                vk::ShaderStageFlagBits::eFragment,
            .module = *shaderModule,
            .pName = "fragMain"
    };

    vk::PipelineShaderStageCreateInfo
        shaderStages[] = {
            vertShaderStageInfo,
            fragShaderStageInfo
    };

    std::vector<vk::DynamicState> dynamicStates =
    {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    auto                                     bindingDescription = Vertex::getBindingDescription();
    auto                                     attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo   vertexInputInfo{ .vertexBindingDescriptionCount = 1,
                                                             .pVertexBindingDescriptions = &bindingDescription,
                                                             .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
                                                             .pVertexAttributeDescriptions = attributeDescriptions.data() };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False
    };

    vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1
    };

    vk::PipelineRasterizationStateCreateInfo rasterizer{ .depthClampEnable = vk::False,
                                                        .rasterizerDiscardEnable = vk::False,
                                                        .polygonMode = vk::PolygonMode::eFill,
                                                        .cullMode = vk::CullModeFlagBits::eBack,
                                                        .frontFace = vk::FrontFace::eCounterClockwise,
                                                        .depthBiasEnable = vk::False,
                                                        .lineWidth = 1.0f };

    vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = msaaSamples, .sampleShadingEnable = vk::False };

    vk::PipelineDepthStencilStateCreateInfo depthStencil{
    .depthTestEnable = vk::True,
    .depthWriteEnable = vk::True,
    .depthCompareOp = vk::CompareOp::eLess,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*descriptorSetLayout,
        .pushConstantRangeCount = 0
    };

    pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

    vk::Format depthFormat = findDepthFormat();

    // Configuración principal del pipeline gráfico.
    // Todavía no se crea el pipeline real;
    // simplemente se prepara toda la información.
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {.stageCount = 2,
         .pStages = shaderStages,
         .pVertexInputState = &vertexInputInfo,
         .pInputAssemblyState = &inputAssembly,
         .pViewportState = &viewportState,
         .pRasterizationState = &rasterizer,
         .pMultisampleState = &multisampling,
         .pDepthStencilState = &depthStencil,
         .pColorBlendState = &colorBlending,
         .pDynamicState = &dynamicState,
         .layout = pipelineLayout,
         .renderPass = nullptr,
         .subpass = 0},
         {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapChainSurfaceFormat.format, .depthAttachmentFormat = depthFormat} };

    if (appInfo.dynamicRenderingSupported)
    {
        graphicsPipeline = vk::raii::Pipeline(
                device,
                nullptr,
                pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            );
    }
    else
    {
        pipelineCreateInfoChain.unlink<vk::PipelineRenderingCreateInfo>();

        auto& pipelineInfo = pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>();

        pipelineInfo.renderPass = *renderPass;
        pipelineInfo.subpass = 0;

        graphicsPipeline =
            vk::raii::Pipeline(
                device,
                nullptr,
                pipelineInfo
            );
    }

    std::cout << "Graphics Pipeline created successfully\n";
}

vk::raii::ShaderModule EngineApplication::createShaderModule(const std::vector<char>& code) const
{
    vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size(),.pCode = reinterpret_cast<const uint32_t*>(code.data()) };
    return vk::raii::ShaderModule(device, createInfo);
}

std::vector<char> EngineApplication::readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("failed to open file!");
    }
    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();
    return buffer;
}