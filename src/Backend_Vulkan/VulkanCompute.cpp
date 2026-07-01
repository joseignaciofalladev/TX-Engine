#include "../Application/EngineApplication.h"
#include <Renderer/ComputeUBO.h>
#include <Renderer/Particle.h>
#include <Core/VulkanConfig.h>
#include <Renderer/GameObject.h>

void EngineApplication::createComputeDescriptorSetLayout()
{
	std::array layoutBindings{
		vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr),
		vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr),
		vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr) };

	vk::DescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
	layoutInfo.pBindings = layoutBindings.data();

	computeDescriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
}

void EngineApplication::createComputePipeline()
{
	auto computeModule = createShaderModule(readFile("shaders/particle.comp.spv"));

	vk::PushConstantRange pushConstantRange{
		.stageFlags = vk::ShaderStageFlagBits::eCompute,
		.offset = 0,
		.size = sizeof(uint32_t) * 2
	};

	vk::PipelineShaderStageCreateInfo computeShaderStageInfo{};
	computeShaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
	computeShaderStageInfo.module = *computeModule;
	computeShaderStageInfo.pName = "main";

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &*computeDescriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	computePipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

	vk::ComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.stage = computeShaderStageInfo;
	pipelineInfo.layout = *computePipelineLayout;

	computePipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
}

void EngineApplication::createShaderStorageBuffers()
{
	// Initialize particles
	std::default_random_engine     rndEngine(static_cast<unsigned>(time(nullptr)));
	std::uniform_real_distribution rndDist(0.0f, 1.0f);

	// Initial particle positions on a circle
	std::vector<Particle> particles(PARTICLE_COUNT);
	for (auto& particle : particles)
	{
		float r = 0.25f * sqrtf(rndDist(rndEngine));
		float theta = rndDist(rndEngine) * 2.0f * 3.14159265358979323846f;
		float x = r * cosf(theta) * HEIGHT / WIDTH;
		float y = r * sinf(theta);
		particle.position = glm::vec2(x, y);
		particle.velocity = normalize(glm::vec2(x, y)) * 0.25f;
		particle.color = glm::vec4(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine), 1.0f);
	}

	vk::DeviceSize bufferSize = sizeof(Particle) * PARTICLE_COUNT;

	// Create a staging buffer used to upload data to the gpu
	vk::raii::Buffer       stagingBuffer({});
	vk::raii::DeviceMemory stagingBufferMemory({});
	createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

	void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
	memcpy(dataStaging, particles.data(), (size_t)bufferSize);
	stagingBufferMemory.unmapMemory();

	shaderStorageBuffers.clear();
	shaderStorageBuffersMemory.clear();

	// Copy initial particle data to all storage buffers
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vk::raii::Buffer       shaderStorageBufferTemp({});
		vk::raii::DeviceMemory shaderStorageBufferTempMemory({});
		createBuffer(bufferSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, shaderStorageBufferTemp, shaderStorageBufferTempMemory);
		copyBuffer(stagingBuffer, shaderStorageBufferTemp, bufferSize);
		shaderStorageBuffers.emplace_back(std::move(shaderStorageBufferTemp));
		shaderStorageBuffersMemory.emplace_back(std::move(shaderStorageBufferTempMemory));
	}
}

void EngineApplication::createComputeDescriptorSets()
{
	std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, computeDescriptorSetLayout);
	vk::DescriptorSetAllocateInfo        allocInfo{};
	allocInfo.descriptorPool = *descriptorPool;
	allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
	allocInfo.pSetLayouts = layouts.data();
	computeDescriptorSets.clear();
	computeDescriptorSets = device.allocateDescriptorSets(allocInfo);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		size_t previousFrame = (i + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;

		vk::DescriptorBufferInfo bufferInfo(computeUniformBuffers[i], 0, sizeof(ComputeUBO));
		vk::DescriptorBufferInfo storageBufferInfoLastFrame(shaderStorageBuffers[previousFrame], 0, sizeof(Particle) * PARTICLE_COUNT);
		vk::DescriptorBufferInfo storageBufferInfoCurrentFrame(shaderStorageBuffers[i], 0, sizeof(Particle) * PARTICLE_COUNT);

		std::array<vk::WriteDescriptorSet, 3> descriptorWrites{};

		descriptorWrites[0].dstSet = *computeDescriptorSets[i];
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].dstSet = *computeDescriptorSets[i];
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].descriptorType = vk::DescriptorType::eStorageBuffer;
		descriptorWrites[1].pBufferInfo = &storageBufferInfoLastFrame;

		descriptorWrites[2].dstSet = *computeDescriptorSets[i];
		descriptorWrites[2].dstBinding = 2;
		descriptorWrites[2].descriptorCount = 1;
		descriptorWrites[2].descriptorType = vk::DescriptorType::eStorageBuffer;
		descriptorWrites[2].pBufferInfo = &storageBufferInfoCurrentFrame;

		device.updateDescriptorSets(descriptorWrites, {});
	}
}

void EngineApplication::createComputeCommandBuffers()
{
	computeCommandBuffers.clear();
	vk::CommandBufferAllocateInfo allocInfo{};
	allocInfo.commandPool = *commandPool;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
	computeCommandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

void EngineApplication::recordComputeCommandBuffer(vk::raii::CommandBuffer& cmdBuffer, uint32_t startIndex, uint32_t count)
{
	cmdBuffer.reset();

	vk::CommandBufferBeginInfo beginInfo{
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};

	cmdBuffer.begin(beginInfo);

	struct PushConstants
	{
		uint32_t startIndex;
		uint32_t count;
	} pushConstants{ startIndex, count };

	uint32_t groupCount = (count + 255) / 256;

	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *computePipeline);

	cmdBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eCompute,
		*computePipelineLayout,
		0,
		{ *computeDescriptorSets[frameIndex] },
		{});

	cmdBuffer.pushConstants<PushConstants>(
		*computePipelineLayout,
		vk::ShaderStageFlagBits::eCompute,
		0,
		pushConstants);

	cmdBuffer.dispatch(groupCount, 1, 1);

	cmdBuffer.end();
}

void EngineApplication::createComputeUniformBuffers()
{
	vk::DeviceSize bufferSize = sizeof(ComputeUBO);

	computeUniformBuffers.clear();
	computeUniformBuffersMemory.clear();
	computeUniformBuffersMapped.clear();

	computeUniformBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
	computeUniformBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);
	computeUniformBuffersMapped.reserve(MAX_FRAMES_IN_FLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		auto [buffer, memory] =
			createBuffer(
				bufferSize,
				vk::BufferUsageFlagBits::eUniformBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible |
				vk::MemoryPropertyFlagBits::eHostCoherent);

		computeUniformBuffers.emplace_back(std::move(buffer));
		computeUniformBuffersMemory.emplace_back(std::move(memory));

		computeUniformBuffersMapped.emplace_back(
			computeUniformBuffersMemory.back().mapMemory(0, bufferSize));
	}
}

void EngineApplication::createParticlePipeline()
{
	auto vertModule = createShaderModule(readFile("shaders/particle.vert.spv"));
	auto fragModule = createShaderModule(readFile("shaders/particle.frag.spv"));

	vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
		.stage = vk::ShaderStageFlagBits::eVertex,
		.module = *vertModule,
		.pName = "main"
	};

	vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
		.stage = vk::ShaderStageFlagBits::eFragment,
		.module = *fragModule,
		.pName = "main"
	};

	vk::PipelineShaderStageCreateInfo shaderStages[] =
	{
		vertShaderStageInfo,
		fragShaderStageInfo
	};

	auto bindingDescription = Particle::getBindingDescription();
	auto attributeDescriptions = Particle::getAttributeDescriptions();

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &bindingDescription,
		.vertexAttributeDescriptionCount =
			static_cast<uint32_t>(attributeDescriptions.size()),
		.pVertexAttributeDescriptions =
			attributeDescriptions.data()
	};

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
		.topology = vk::PrimitiveTopology::ePointList,
		.primitiveRestartEnable = VK_FALSE
	};

	vk::PipelineViewportStateCreateInfo viewportState{
		.viewportCount = 1,
		.scissorCount = 1
	};

	vk::PipelineRasterizationStateCreateInfo rasterizer{
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = vk::PolygonMode::eFill,
		.cullMode = vk::CullModeFlagBits::eNone,
		.frontFace = vk::FrontFace::eCounterClockwise,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f
	};

	vk::PipelineMultisampleStateCreateInfo multisampling{
		.rasterizationSamples = msaaSamples,
		.sampleShadingEnable = VK_FALSE
	};

	vk::PipelineDepthStencilStateCreateInfo depthStencil{
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = vk::CompareOp::eLess,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE
	};

	vk::PipelineColorBlendAttachmentState colorBlendAttachment{
		.blendEnable = VK_TRUE,

		.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
		.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
		.colorBlendOp = vk::BlendOp::eAdd,

		.srcAlphaBlendFactor = vk::BlendFactor::eOne,
		.dstAlphaBlendFactor = vk::BlendFactor::eZero,
		.alphaBlendOp = vk::BlendOp::eAdd,

		.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA
	};

	vk::PipelineColorBlendStateCreateInfo colorBlending{
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &colorBlendAttachment
	};

	constexpr std::array dynamicStates
	{
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor
	};

	vk::PipelineDynamicStateCreateInfo dynamicState{
		.dynamicStateCount =
			static_cast<uint32_t>(dynamicStates.size()),
		.pDynamicStates = dynamicStates.data()
	};

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};

	particlePipelineLayout =
		vk::raii::PipelineLayout(device, pipelineLayoutInfo);

	vk::Format depthFormat = findDepthFormat();

	vk::StructureChain<
		vk::GraphicsPipelineCreateInfo,
		vk::PipelineRenderingCreateInfo> pipelineInfo =
	{
		{
			.stageCount = 2,
			.pStages = shaderStages,
			.pVertexInputState = &vertexInputInfo,
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pDepthStencilState = &depthStencil,
			.pColorBlendState = &colorBlending,
			.pDynamicState = &dynamicState,
			.layout = *particlePipelineLayout,
			.renderPass = nullptr
		},
		{
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &swapChainSurfaceFormat.format,
			.depthAttachmentFormat = depthFormat
		}
	};

	try
	{
		if (appInfo.dynamicRenderingSupported)
		{
			particlePipeline =
				vk::raii::Pipeline(
					device,
					nullptr,
					pipelineInfo.get<vk::GraphicsPipelineCreateInfo>());
		}
		else
		{
			pipelineInfo.unlink<vk::PipelineRenderingCreateInfo>();

			auto& graphicsInfo =
				pipelineInfo.get<vk::GraphicsPipelineCreateInfo>();

			graphicsInfo.renderPass = *renderPass;

			particlePipeline =
				vk::raii::Pipeline(
					device,
					nullptr,
					graphicsInfo);
		}

		std::cout << "Particle Pipeline created successfully\n";
	}
	catch (const std::exception& e)
	{
		std::cout << "Particle pipeline ERROR:\n";
		std::cout << e.what() << std::endl;
		throw;
	}
}