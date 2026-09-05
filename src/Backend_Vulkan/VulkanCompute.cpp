#include "/Dev/Proyectos/Motores/TXEngine/src/Application/EngineApplication.h"
#include <Renderer/ComputeUBO.h>
#include <Renderer/Particle.h>
#include <Core/VulkanConfig.h>
#include <Renderer/GameObject.h>

void EngineApplication::createComputeDescriptorSetLayout()
{
	const std::array layoutBindings{
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
	const auto computeModule = createShaderModule(readFile("shaders/particle.comp.spv"));

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

	const vk::ComputePipelineCreateInfo pipelineInfo{
		.stage = computeShaderStageInfo,
		.layout = *computePipelineLayout
	};

	computePipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
}

void EngineApplication::createShaderStorageBuffers()
{
	// Initialize particles
	std::mt19937 rndEngine(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

	constexpr float spawnRadius = 0.25f;
	constexpr float minVelocity = 0.25f;
	constexpr float velocityScale = 0.75f;

	// Initial particle positions on a circle
	std::vector<Particle> particles(PARTICLE_COUNT);
	for (auto& particle : particles)
	{
		const float r = spawnRadius * std::sqrt(rndDist(rndEngine));
		const float theta = rndDist(rndEngine) * glm::two_pi<float>();
		const float x = r * std::cos(theta) * HEIGHT / WIDTH;
		const float y = r * std::sin(theta);

		particle.position = { x, y };

		const float velocityMagnitude = std::max(minVelocity, r * velocityScale);

		glm::vec2 dir(x, y);

		if (glm::dot(dir, dir) > 0.0f)
		{
			dir = glm::normalize(dir);
		}
		else
		{
			dir = { 1.0f, 0.0f };
		}

		particle.velocity = dir * velocityMagnitude;
		particle.color = glm::vec4(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine), 1.0f);
	}

	const vk::DeviceSize bufferSize = sizeof(Particle) * PARTICLE_COUNT;

	vk::raii::Buffer stagingBuffer(nullptr);
	vk::raii::DeviceMemory stagingBufferMemory(nullptr);
	createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

	void* const mapped = stagingBufferMemory.mapMemory(0, bufferSize);

	std::memcpy(mapped, particles.data(), static_cast<size_t>(bufferSize));

	stagingBufferMemory.unmapMemory();

	shaderStorageBuffers.clear();
	shaderStorageBuffersMemory.clear();

	shaderStorageBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
	shaderStorageBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);

	// Copy initial particle data to all storage buffers
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		vk::raii::Buffer shaderBuffer(nullptr);
		vk::raii::DeviceMemory shaderMemory(nullptr);

		createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eStorageBuffer |
			vk::BufferUsageFlagBits::eVertexBuffer |
			vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			shaderBuffer,
			shaderMemory);

		copyBuffer(stagingBuffer, shaderBuffer, bufferSize);

		shaderStorageBuffers.emplace_back(std::move(shaderBuffer));
		shaderStorageBuffersMemory.emplace_back(std::move(shaderMemory));
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
		const size_t previousFrame = (i + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;

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

	const vk::CommandBufferAllocateInfo allocInfo{
		.commandPool = *commandPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = MAX_FRAMES_IN_FLIGHT
	};

	computeCommandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

void EngineApplication::recordComputeCommandBuffer(vk::raii::CommandBuffer& cmdBuffer, uint32_t frame, uint32_t startIndex, uint32_t count)
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

	constexpr uint32_t WorkgroupSize = 256;

	const uint32_t groupCount = (count + WorkgroupSize - 1) / WorkgroupSize;

	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *computePipeline);

	cmdBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eCompute,
		*computePipelineLayout,
		0,
		*computeDescriptorSets[frame],
		{});

	cmdBuffer.pushConstants<PushConstants>(
		*computePipelineLayout,
		vk::ShaderStageFlagBits::eCompute,
		0,
		pushConstants);

	if (groupCount > 0)
	{
		cmdBuffer.dispatch(groupCount, 1, 1);
	}

	/*
	const vk::BufferMemoryBarrier2 barrier{
		.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
		.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,

		.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput,
		.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead,

		.buffer = *shaderStorageBuffers[frame],
		.offset = 0,
		.size = VK_WHOLE_SIZE
	};

	const vk::DependencyInfo dependency{
		.bufferMemoryBarrierCount = 1,
		.pBufferMemoryBarriers = &barrier
	};

	cmdBuffer.pipelineBarrier2(dependency);
	*/

	cmdBuffer.end();
}

void EngineApplication::createComputeUniformBuffers()
{
	const vk::DeviceSize bufferSize = sizeof(ComputeUBO);

	computeUniformBuffers.clear();
	computeUniformBuffersMemory.clear();
	computeUniformBuffersMapped.clear();

	computeUniformBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
	computeUniformBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);
	computeUniformBuffersMapped.reserve(MAX_FRAMES_IN_FLIGHT);

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
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
			computeUniformBuffersMemory.back().mapMemory(
				0,
				bufferSize));
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

	const std::array shaderStages{
		vertShaderStageInfo,
		fragShaderStageInfo
	};

	const auto bindingDescription = Particle::getBindingDescription();
	const auto attributeDescriptions = Particle::getAttributeDescriptions();

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

	particlePipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

	const vk::Format depthFormat = findDepthFormat();

	vk::StructureChain<
		vk::GraphicsPipelineCreateInfo,
		vk::PipelineRenderingCreateInfo> pipelineInfo =
	{
		{
			.stageCount = static_cast<uint32_t>(shaderStages.size()),
			.pStages = shaderStages.data(),
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
