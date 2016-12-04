#include "VulkanBaseApplication.h"

#define _USE_MATH_DEFINES
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) {
	auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pCallback);
	}
	else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
	if (func != nullptr) {
		func(instance, callback, pAllocator);
	}
}


// mouse control related
glm::vec2 cursorPos;
bool lbuttonDown, rbuttonDown;
glm::vec2 modelRotAngles; // for model rotation

glm::vec3 cameraPos;
glm::vec3 lookAtDir;
glm::vec2 cameraRotAngles; // for camera rotation

// deubg mode int
float debugMode;


namespace std {

	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1 ^
					(hash<glm::vec3>()(vertex.normal) << 1));
		}
	};
}

bool lightMoveDirs[MAX_NUM_LIGHT];


/************************************************************/
//			Base Class for Vulkan Application
/************************************************************/
//				Function Implementation
/************************************************************/
void VulkanBaseApplication::run() {
	initWindow();
	initVulkan();
	mainLoop();
}

// clean up resources
VulkanBaseApplication::~VulkanBaseApplication() {


	// swap chain image veiws
	for (auto imageView : swapChainImageViews) {
		vkDestroyImageView(device, imageView, nullptr);
	}

	//  textures
	for (auto texture : textures) {
		vkDestroyImageView(device, texture.imageView, nullptr);
		vkDestroyImage(device, texture.image, nullptr);
		vkFreeMemory(device, texture.imageMemory, nullptr);
		vkDestroySampler(device, texture.sampler, nullptr);
	}

	// mesh buffers clean up
	meshs.cleanup(device);

	// cleanup uniform buffers
	uniformData.cleanup(device);
}

void VulkanBaseApplication::initWindow() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);

	// set camera position
	cameraPos = glm::vec3(-5.0f, 0.0f, 0.0f);

	// setup callback functions
	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetScrollCallback(window, scrollCallback);
}

void VulkanBaseApplication::mainLoop() {
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		updateUniformBuffer();
		drawFrame();
	}

	vkDeviceWaitIdle(device);
}

void VulkanBaseApplication::updateUniformBuffer() {

	//--------------------- update vertex uniform buffer----------------------------------
	static auto startTime = std::chrono::high_resolution_clock::now();

	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0f;

	UBO_vsScene & vsScene = ubos.vsScene;

	// update model rotations
	//ubo.model = glm::rotate(glm::mat4(), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	//ubo.model = glm::rotate(glm::mat4(), modelRotAngles.x , glm::vec3(0.0f, 0.0f, 1.0f)) * glm::rotate(glm::mat4(), modelRotAngles.y, glm::vec3(0.0f, 1.0f, 0.0f));
	vsScene.model = glm::translate(glm::mat4(), glm::vec3(0, 4, 0));

	// update camera rotations
	//glm::vec4 rotCameraPos = glm::vec4(cameraPos, 1.0f);
	//rotCameraPos = glm::rotate(glm::mat4(), -cameraRotAngles.x, glm::vec3(0.0f, 0.0f, 1.0f)) * rotCameraPos;
	//ubo.view = glm::lookAt(glm::vec3(rotCameraPos), glm::vec3(0,0,0), glm::vec3(0.0f, 1.0f, 0.0f));
	vsScene.view = glm::lookAt(cameraPos, glm::vec3(0, 0, 0), glm::vec3(0.0f, 1.0f, 0.0f));

	// projection matrix
	vsScene.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float)swapChainExtent.height, 0.01f, 100.0f);
	vsScene.proj[1][1] *= -1;

	// cameraPos
	vsScene.cameraPos = glm::vec4(cameraPos, 1.0f);

	// copy data to buffer memory
	void* data;
	VkDeviceSize & bufferSize = uniformData.vsSceneStaging.allocSize;
	vkMapMemory(device, uniformData.vsSceneStaging.memory, 0, bufferSize, 0, &data);
		memcpy(data, &vsScene, bufferSize);
	vkUnmapMemory(device, uniformData.vsSceneStaging.memory);

	copyBuffer(uniformData.vsSceneStaging.buffer, uniformData.vsScene.buffer, bufferSize);


	//--------------------- update frag uniform buffer (lights)----------------------------------
	UBO_fsLights & fsLights = ubos.fsLights;
	for (int i = 0; i < fsLights.numLights; ++i) {
		fsLights.lights[i].pos.y += 0.0015f * ((float)lightMoveDirs[i] * 2.0f - 1.0f);

		if (fsLights.lights[i].pos.y > 1.0f || fsLights.lights[i].pos.y < -2.0f)
		{
			//fragLightInfos.lights[i].pos.y = 2.0f;
			lightMoveDirs[i] = !lightMoveDirs[i];
		}
	}

	bufferSize = uniformData.fsLightsStaging.allocSize;
	vkMapMemory(device, uniformData.fsLightsStaging.memory, 0, bufferSize, 0, &data);
		memcpy(data, &fsLights, bufferSize);
	vkUnmapMemory(device, uniformData.fsLightsStaging.memory);

	copyBuffer(uniformData.fsLightsStaging.buffer, uniformData.fsLights.buffer, bufferSize);
}


void VulkanBaseApplication::drawFrame() {
	uint32_t imageIndex;
	vkAcquireNextImageKHR(device, swapChain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

	VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
		throw std::runtime_error("failed to submit draw command buffer!");
	}

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = { swapChain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;

	presentInfo.pImageIndices = &imageIndex;

	vkQueuePresentKHR(presentQueue, &presentInfo);
}


void VulkanBaseApplication::initVulkan() {

	createInstance();
	setupDebugCallback();
	createSurface();
	pickPhysicalDevice();
	createLogicalDevice();
	createSwapChain();
	createImageViews();
	createRenderPass();
	createDescriptorSetLayout();
	createGraphicsPipeline();
	createCommandPool();
	createDepthResources();
	createFramebuffers();
	prepareTextures();

	// load data -> create vertex and index buffer
	loadModel(meshs.scene.vertices.verticesData, meshs.scene.indices.indicesData, MODEL_PATH, 0.4f);
	createMeshBuffer(meshs.scene);

	loadAxisInfo();
	createMeshBuffer(meshs.axis);

	loadTextureQuad();
	createMeshBuffer(meshs.quad);

	// light information
	createLightInfos();

	createUniformBuffer();
	createDescriptorPool();
	createDescriptorSet();
	createCommandBuffers();
	createSemaphores();
}


// Create Vulkan instance
void VulkanBaseApplication::createInstance() {
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Hello Vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	auto extensions = getRequiredExtensions();
	createInfo.enabledExtensionCount = uint32_t(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	if (enableValidationLayers) {
		createInfo.enabledLayerCount = uint32_t(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
	}
	else {
		createInfo.enabledLayerCount = 0;
	}

	if (vkCreateInstance(&createInfo, nullptr, instance.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create instance!");
	}

	printInstanceExtensions();
}


void VulkanBaseApplication::setupDebugCallback() {
	if (!enableValidationLayers) return;

	VkDebugReportCallbackCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	createInfo.pfnCallback = debugCallback;

	if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, callback.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to set up debug callback!");
	}
}


void VulkanBaseApplication::createSurface() {
	if (glfwCreateWindowSurface(instance, window, nullptr, surface.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface!");
	}
}


void VulkanBaseApplication::pickPhysicalDevice() {

	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

	if (deviceCount == 0) {
		throw std::runtime_error("failed to find GPUs with Vulkan support!");
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

	for (const auto& device : devices) {
		if (isDeviceSuitable(device)) {
			physicalDevice = device;
			break;
		}
	}

	if (physicalDevice == VK_NULL_HANDLE) {
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}


void VulkanBaseApplication::createLogicalDevice() {
	QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

	float queuePriority = 1.0f;
	for (int queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		queueCreateInfos.push_back(queueCreateInfo);
	}


	VkPhysicalDeviceFeatures deviceFeatures = {};
	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.queueCreateInfoCount = uint32_t(queueCreateInfos.size());

	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = uint32_t(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

	if (enableValidationLayers) {
		createInfo.enabledLayerCount = uint32_t(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
	}
	else {
		createInfo.enabledLayerCount = 0;
	}

	if (vkCreateDevice(physicalDevice, &createInfo, nullptr, device.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create logical device!");
	}

	// retrieving queue handles
	vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
	vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);

}


void VulkanBaseApplication::createSwapChain() {
	SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

	VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;

	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
	uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentFamily };

	if (indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	}
	else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		//createInfo.queueFamilyIndexCount = 0; // Optional
		//createInfo.pQueueFamilyIndices = nullptr; // Optional
	}

	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;

	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if (vkCreateSwapchainKHR(device, &createInfo, nullptr, swapChain.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create swap chain!");
	}

	vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
	swapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

	swapChainImageFormat = surfaceFormat.format;
	swapChainExtent = extent;
}


void VulkanBaseApplication::createImageViews() {
	swapChainImageViews.resize(swapChainImages.size(), VDeleter<VkImageView>{device, vkDestroyImageView});

	for (uint32_t i = 0; i < swapChainImages.size(); i++) {
		createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, swapChainImageViews[i]);
	}
}


void VulkanBaseApplication::createGraphicsPipeline()
{
#pragma region Vertex and Fragment Shader Stages
	// vertex and fragment shader stages

	shaderModules.resize(5);
	VkPipelineShaderStageCreateInfo vertShaderStageInfo = loadShader("../src/shaders/triangle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkPipelineShaderStageCreateInfo fragShaderStageInfo = loadShader("../src/shaders/triangle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkPipelineShaderStageCreateInfo vertShaderStageInfo_axis = loadShader("../src/shaders/axis.vert.spv", VK_SHADER_STAGE_VERTEX_BIT, 2);
	VkPipelineShaderStageCreateInfo fragShaderStageInfo_axis = loadShader("../src/shaders/axis.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT, 3);
	VkPipelineShaderStageCreateInfo fragShaderStageInfo_quad = loadShader("../src/shaders/quad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT, 4);


	VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };
#pragma endregion


#pragma region Vertex Input State
	// vertex input state
	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attributeDescriptions.size();
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
#pragma endregion


#pragma region Input Assembly State
	// input assembly state
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	//inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	inputAssembly.primitiveRestartEnable = VK_FALSE;
#pragma endregion


#pragma region Viewport State
	// viewport state

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)swapChainExtent.width;
	viewport.height = (float)swapChainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = swapChainExtent;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;
#pragma endregion


#pragma region Rasterizer State
	// rasterizer state

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; //VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f; // Optional
	rasterizer.depthBiasClamp = 0.0f; // Optional
	rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
#pragma endregion


#pragma region Multisampling State
												// multisampling state
	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling.minSampleShading = 1.0f; // Optional
	multisampling.pSampleMask = nullptr; /// Optional
	multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
	multisampling.alphaToOneEnable = VK_FALSE; // Optional
#pragma endregion


#pragma region Depth and Stencil Testing
												   // depth and stencil testing
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f; // Optional
	colorBlending.blendConstants[1] = 0.0f; // Optional
	colorBlending.blendConstants[2] = 0.0f; // Optional
	colorBlending.blendConstants[3] = 0.0f; // Optional

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f; // Optional
	depthStencil.maxDepthBounds = 1.0f; // Optional
	depthStencil.stencilTestEnable = VK_FALSE;
	depthStencil.front = {}; // Optional
	depthStencil.back = {}; // Optional
#pragma endregion


#pragma region Dynamic state
								// dynamic state
	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_LINE_WIDTH
	};

	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;
#pragma endregion


#pragma region Pipeline Layout
	// pipeline layout object
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	VkDescriptorSetLayout setLayouts[] = { descriptorSetLayout };
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	//pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
	//pipelineLayoutInfo.pPushConstantRanges = 0; // Optional

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
		pipelineLayout.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create pipeline layout!");
	}
#pragma endregion


#pragma region Graphics Pipeline Creation
	// combine all information, generate graphics pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil; // Optional
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = nullptr; // Optional
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1; // Optional
#pragma endregion


											 // create graphics pipeline finally!
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelines.graphics) != VK_SUCCESS) {
		throw std::runtime_error("failed to create graphics pipeline!");
	}

	// create graphics pipeline for quad render
	// input assembly state for texture quad, without culling
	shaderStages[1] = fragShaderStageInfo_quad;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelines.quad) != VK_SUCCESS) {
		throw std::runtime_error("failed to create graphics pipeline!");
	}

	// create graphics pipeline for line list
	// input assembly state for axis (lines)
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	shaderStages[0] = vertShaderStageInfo_axis;
	shaderStages[1] = fragShaderStageInfo_axis;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelines.axis) != VK_SUCCESS) {
		throw std::runtime_error("failed to create graphics pipeline!");
	}


}

void VulkanBaseApplication::createFramebuffers() {
	swapChainFramebuffers.resize(swapChainImageViews.size(), VDeleter<VkFramebuffer>{device, vkDestroyFramebuffer});

	for (size_t i = 0; i < swapChainImageViews.size(); i++) {
		std::array<VkImageView, 2> attachments = {
			swapChainImageViews[i],
			depth.view
		};


		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = (uint32_t)attachments.size();
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = swapChainExtent.width;
		framebufferInfo.height = swapChainExtent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, swapChainFramebuffers[i].replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create framebuffer!");
		}
	}
}


void VulkanBaseApplication::createCommandPool() {
	QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
	poolInfo.flags = 0; // Optional

	if (vkCreateCommandPool(device, &poolInfo, nullptr, commandPool.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create command pool!");
	}
}


void VulkanBaseApplication::createCommandBuffers() {
	commandBuffers.resize(swapChainFramebuffers.size());

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

	if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate command buffers!");
	}

	for (size_t i = 0; i < commandBuffers.size(); i++) {
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = swapChainFramebuffers[i];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;

		std::array<VkClearValue, 2> clearValues = {};
		clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
		clearValues[1].depthStencil = { 1.0f, 0 };

		renderPassInfo.clearValueCount = (uint32_t)clearValues.size();
		renderPassInfo.pClearValues = clearValues.data();


		// render pass begin
		vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// draw model here (triangle list)
		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.graphics);

		// binding the vertex buffer
		VkBuffer vertexBuffers[] = { meshs.scene.vertices.buffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

		vkCmdBindIndexBuffer(commandBuffers[i], meshs.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		//vkCmdDraw(commandBuffers[i], vertices.size(), 1, 0, 0);
		vkCmdDrawIndexed(commandBuffers[i], (uint32_t)meshs.scene.indices.indicesData.size(), 1, 0, 0, 0);


		// draw quad here
		//vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_quad);

		//// binding the vertex buffer
		//VkBuffer vertexBuffers_quad[] = { vertexBuffer_quad };
		//VkDeviceSize offsets_quad[] = { 0 };
		//vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers_quad, offsets_quad);

		//vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer_quad, 0, VK_INDEX_TYPE_UINT32);

		//vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		////vkCmdDraw(commandBuffers[i], vertices.size(), 1, 0, 0);
		//vkCmdDrawIndexed(commandBuffers[i], (uint32_t)indices_quad.size(), 1, 0, 0, 0);


		// draw axis here (line list)
		// bind pipeline
		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.axis);
		// binding the vertex buffer for axis
		VkBuffer vertexBuffers_axis[] = { meshs.axis.vertices.buffer };
		VkDeviceSize offsets_axis[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers_axis, offsets_axis);

		vkCmdBindIndexBuffer(commandBuffers[i], meshs.axis.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		//vkCmdDraw(commandBuffers[i], vertices.size(), 1, 0, 0);
		vkCmdDrawIndexed(commandBuffers[i], (uint32_t)meshs.axis.indices.indicesData.size(), 1, 0, 0, 0);


		vkCmdEndRenderPass(commandBuffers[i]);

		if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
		}
	}
}



void VulkanBaseApplication::createRenderPass() {

	// color attachment
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = swapChainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// depth attachment
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = findDepthFormat();
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// subpass
	VkSubpassDescription subPass = {};
	subPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subPass.colorAttachmentCount = 1;
	subPass.pColorAttachments = &colorAttachmentRef;
	subPass.pDepthStencilAttachment = &depthAttachmentRef;

	// dependency
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	// render pass info
	std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = (uint32_t)attachments.size();
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subPass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(device, &renderPassInfo, nullptr, renderPass.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create render pass!");
	}
}


void VulkanBaseApplication::createVertexBuffer(std::vector<Vertex> & verticesData, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
	VkDeviceSize bufferSize = sizeof(verticesData[0]) * verticesData.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, verticesData.data(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		buffer, bufferMemory);

	copyBuffer(stagingBuffer, buffer, bufferSize);
}


// index buffer
void VulkanBaseApplication::createIndexBuffer(std::vector<uint32_t> &indicesData, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {

	VkDeviceSize bufferSize = sizeof(indicesData[0]) * indicesData.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, indicesData.data(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		buffer, bufferMemory);

	copyBuffer(stagingBuffer, buffer, bufferSize);
}


void VulkanBaseApplication::createSemaphores() {
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, imageAvailableSemaphore.replace()) != VK_SUCCESS ||
		vkCreateSemaphore(device, &semaphoreInfo, nullptr, renderFinishedSemaphore.replace()) != VK_SUCCESS) {

		throw std::runtime_error("failed to create semaphores!");
	}
}

// check device suitabiliy
bool VulkanBaseApplication::isDeviceSuitable(VkPhysicalDevice device) {

	QueueFamilyIndices indices = findQueueFamilies(device);

	bool extensionSupported = checkDeviceExtensionSupport(device);

	bool swapChainAdequate = false;
	if (extensionSupported) {
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
		swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
	}

	return indices.isComplete() && extensionSupported && swapChainAdequate;
}


// find queue families
QueueFamilyIndices VulkanBaseApplication::findQueueFamilies(VkPhysicalDevice device) {
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

		if (queueFamily.queueCount > 0 && presentSupport) {
			indices.presentFamily = i;
		}

		if (indices.isComplete()) {
			break;
		}
		i++;
	}

	return indices;
}


// get extension that are required by instance
std::vector<const char*> VulkanBaseApplication::getRequiredExtensions() {
	std::vector<const char*> extensions;

	unsigned int glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	for (unsigned int i = 0; i < glfwExtensionCount; i++) {
		extensions.push_back(glfwExtensions[i]);
	}

	if (enableValidationLayers) {
		extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	}

	return extensions;
}


// check validation layer support
bool VulkanBaseApplication::checkValidationLayerSupport() {
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : validationLayers) {
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers) {
			if (strcmp(layerName, layerProperties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}

		if (!layerFound) {
			return false;
		}
	}

}



// helper func: print extension names
void VulkanBaseApplication::printInstanceExtensions()
{
	uint32_t extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> extensions(extensionCount);

	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

	std::cout << "available extensions:" << std::endl;

	for (const auto& extension : extensions) {
		std::cout << "\t" << extension.extensionName << std::endl;
	}
}


// check device extension support
bool VulkanBaseApplication::checkDeviceExtensionSupport(VkPhysicalDevice device) {

	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (const auto & extension : availableExtensions) {
		requiredExtensions.erase(extension.extensionName);
	}

	return requiredExtensions.empty();
}


// query swap chain support, assign SwapChainSupportDetails struct
SwapChainSupportDetails VulkanBaseApplication::querySwapChainSupport(VkPhysicalDevice device) {
	SwapChainSupportDetails details;

	// basic surface capabilities
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

	// query supported surface format
	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

	if (formatCount != 0) {
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
	}

	// query supported presentation modes
	uint32_t presentModesCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModesCount, nullptr);

	if (presentModesCount != 0) {
		details.presentModes.resize(presentModesCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModesCount, details.presentModes.data());
	}

	return details;
}


// swap chain choose format
VkSurfaceFormatKHR VulkanBaseApplication::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
		return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	}

	for (const auto& availableFormat : availableFormats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return availableFormat;
		}
	}

	return availableFormats[0];
}


// swap chain, choose present mode
VkPresentModeKHR VulkanBaseApplication::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {
	for (const auto& availablePresentMode : availablePresentModes) {
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return availablePresentMode;
		}
	}

}


// swap chain, choose swap extent
VkExtent2D VulkanBaseApplication::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		return capabilities.currentExtent;
	}
	else {
		VkExtent2D actualExtent = { (uint32_t)WIDTH, (uint32_t)HEIGHT };

		actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

		return actualExtent;
	}
}


// read shader file from compiled binary file
std::vector<char> VulkanBaseApplication::readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

void VulkanBaseApplication::createShaderModule(const std::vector<char> & code, VDeleter<VkShaderModule> & shaderModule) {

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = (uint32_t*)code.data();

	if (vkCreateShaderModule(device, &createInfo, nullptr, shaderModule.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module!");
	}

}


VkPipelineShaderStageCreateInfo VulkanBaseApplication::loadShader(std::string fileName, VkShaderStageFlagBits stage, int shaderModuleIndex) {
	VkPipelineShaderStageCreateInfo shaderStage = {};
	std::vector<char> codes = readFile(fileName);
	createShaderModule(codes, shaderModules[shaderModuleIndex]);

	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStage.stage = stage;
	shaderStage.module = shaderModules[shaderModuleIndex];
	shaderStage.pName = "main"; // todo : make param

	return shaderStage;
}


// find memory type
uint32_t VulkanBaseApplication::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {

	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type!");
}


// abstracting buffer creation
void VulkanBaseApplication::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create buffer!");
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate buffer memory!");
	}

	vkBindBufferMemory(device, buffer, bufferMemory, 0);
}


// copy buffer from srcBuffer to dstBuffer
void VulkanBaseApplication::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {

	VkCommandBuffer commandBuffer = beginSingleTimeCommands();

	VkBufferCopy copyRegion = {};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(commandBuffer);
}


// descriptor set layout
void VulkanBaseApplication::createDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 1;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding samplerLayoutBinding2 = {};
	samplerLayoutBinding2.binding = 2;
	samplerLayoutBinding2.descriptorCount = 1;
	samplerLayoutBinding2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding2.pImmutableSamplers = nullptr;
	samplerLayoutBinding2.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding fragLightsLayoutBinding = {};
	fragLightsLayoutBinding.binding = 3;
	fragLightsLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	fragLightsLayoutBinding.descriptorCount = 1;
	fragLightsLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragLightsLayoutBinding.pImmutableSamplers = nullptr; // Optional

	std::array<VkDescriptorSetLayoutBinding, 4> bindings = { uboLayoutBinding, samplerLayoutBinding, samplerLayoutBinding2, fragLightsLayoutBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = (uint32_t)bindings.size();
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, descriptorSetLayout.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create descriptor set layout!");
	}

}

void VulkanBaseApplication::createLightInfos() {

	std::default_random_engine gen;
	std::uniform_real_distribution<float> uniformDistribution(0.0f, 1.0f);

	ubos.fsLights.numLights = 8;
	float scale = 3.0f;
	for (int i = 0; i < ubos.fsLights.numLights; ++i) {
		float posX = uniformDistribution(gen) * 4.0f * scale - 2.0f * scale;
		float posY = uniformDistribution(gen) * 2.0f - 1.0f;
		float posZ = uniformDistribution(gen) * 2.0f * scale - scale;
		float intensity = uniformDistribution(gen) * 0.8f;
		ubos.fsLights.lights[i].pos = glm::vec4(posX, posY, posZ, intensity);

		ubos.fsLights.lights[i].color = glm::vec4(
			uniformDistribution(gen),
			uniformDistribution(gen),
			uniformDistribution(gen),
			uniformDistribution(gen) * 2.0f * scale);

		lightMoveDirs[i] = false;
	}
}


void VulkanBaseApplication::createUniformBuffer() {
	//VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	//createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformStagingBuffer.buffer, uniformStagingBuffer.memory);
	//createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, uniformBuffer.buffer, uniformBuffer.memory);

	//bufferSize = sizeof(FragLightInfos);

	//createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fragLightsStagingBuffer.buffer, fragLightsStagingBuffer.memory);
	//createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, fragLightsBuffer.buffer, fragLightsBuffer.memory);

	// vs scene
	VkDeviceSize bufferSize = sizeof(UBO_vsScene);
	uniformData.vsSceneStaging.allocSize = bufferSize;
	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		uniformData.vsSceneStaging.buffer, uniformData.vsSceneStaging.memory);

	uniformData.vsScene.allocSize = bufferSize;
	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		uniformData.vsScene.buffer, uniformData.vsScene.memory);

	// fs lights
	bufferSize = sizeof(UBO_fsLights);
	uniformData.fsLightsStaging.allocSize = bufferSize;
	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		uniformData.fsLightsStaging.buffer, uniformData.fsLightsStaging.memory);

	uniformData.fsLights.allocSize = bufferSize;
	createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		uniformData.fsLights.buffer, uniformData.fsLights.memory);

}


void VulkanBaseApplication::createDescriptorPool() {

	std::array<VkDescriptorPoolSize, 4> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = 1;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = 1;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[2].descriptorCount = 1;
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[3].descriptorCount = 1;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, descriptorPool.replace()) != VK_SUCCESS) {
		throw std::runtime_error("failed to create descriptor pool!");
	}
}


void VulkanBaseApplication::createDescriptorSet() {
	VkDescriptorSetLayout layouts[] = { descriptorSetLayout };
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = layouts;

	if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate descriptor set!");
	}


	VkDescriptorBufferInfo vsSceneDescriptorInfo = {};
	vsSceneDescriptorInfo.buffer = uniformData.vsScene.buffer;
	vsSceneDescriptorInfo.offset = 0;
	vsSceneDescriptorInfo.range = uniformData.vsScene.allocSize;

	VkDescriptorBufferInfo fsLightsDescriptorInfo = {};
	fsLightsDescriptorInfo.buffer = uniformData.fsLights.buffer;
	fsLightsDescriptorInfo.offset = 0;
	fsLightsDescriptorInfo.range = uniformData.fsLights.allocSize;

	std::array<VkDescriptorImageInfo, 2> imageInfo = {};
	imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo[0].imageView = textures[0].imageView; //textureImageViews[0];
	imageInfo[0].sampler = textures[0].sampler; // textureSamplers[0];

	imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo[1].imageView = textures[1].imageView; // textureImageViews[1];
	imageInfo[1].sampler = textures[1].sampler; // textureSamplers[1];

	std::array<VkWriteDescriptorSet, 4> descriptorWrites = {};

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pBufferInfo = &vsSceneDescriptorInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &imageInfo[0];

	descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[2].dstSet = descriptorSet;
	descriptorWrites[2].dstBinding = 2;
	descriptorWrites[2].dstArrayElement = 0;
	descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[2].descriptorCount = 1;
	descriptorWrites[2].pImageInfo = &imageInfo[1];

	descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[3].dstSet = descriptorSet;
	descriptorWrites[3].dstBinding = 3;
	descriptorWrites[3].dstArrayElement = 0;
	descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[3].descriptorCount = 1;
	descriptorWrites[3].pBufferInfo = &fsLightsDescriptorInfo;

	vkUpdateDescriptorSets(device, (uint32_t)descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}


void VulkanBaseApplication::createTextureImage(const std::string& texFilename, VkImage & texImage, VkDeviceMemory & texImageMemory) {

	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(texFilename.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	VkDeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		throw std::runtime_error("failed to load texture image!");
	}

	VkImage stagingImage;
	VkDeviceMemory stagingImageMemory;
	createImage(
		texWidth, texHeight,
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingImage, stagingImageMemory);

	void* data;
	vkMapMemory(device, stagingImageMemory, 0, imageSize, 0, &data);
	memcpy(data, pixels, (size_t)imageSize);
	vkUnmapMemory(device, stagingImageMemory);

	stbi_image_free(pixels);

	createImage(
		texWidth, texHeight,
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		texImage, texImageMemory);

	transitionImageLayout(stagingImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	transitionImageLayout(texImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copyImage(stagingImage, texImage, texWidth, texHeight);

	transitionImageLayout(texImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}


void VulkanBaseApplication::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage & image, VkDeviceMemory & imageMemory) {

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	imageInfo.usage = usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
		throw std::runtime_error("failed to create image!");
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(device, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate image memory!");
	}

	vkBindImageMemory(device, image, imageMemory, 0);
}


void VulkanBaseApplication::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	// use the right subresource aspect
	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (hasStencilComponent(format)) {
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}


	if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}
	else {
		throw std::invalid_argument("unsupported layout transition!");
	}

	vkCmdPipelineBarrier(
		commandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
		);

	endSingleTimeCommands(commandBuffer);
}


void VulkanBaseApplication::copyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands();

	VkImageSubresourceLayers subResource = {};
	subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subResource.baseArrayLayer = 0;
	subResource.mipLevel = 0;
	subResource.layerCount = 1;

	VkImageCopy region = {};
	region.srcSubresource = subResource;
	region.dstSubresource = subResource;
	region.srcOffset = { 0, 0, 0 };
	region.dstOffset = { 0, 0, 0 };
	region.extent.width = width;
	region.extent.height = height;
	region.extent.depth = 1;

	vkCmdCopyImage(
		commandBuffer,
		srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region
		);

	endSingleTimeCommands(commandBuffer);
}


void VulkanBaseApplication::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView & imageView) {

	VkImageViewCreateInfo viewInfo = {};

	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
		throw std::runtime_error("failed to create texture image view!");
	}
}


void VulkanBaseApplication::createTextureImageView(VkImage & textureImage, VkImageView & textureImageView) {
	createImageView(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, textureImageView);
}


void VulkanBaseApplication::createTextureSampler(VkSampler & textureSampler) {
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;

	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;

	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;

	if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
		throw std::runtime_error("failed to create texture sampler!");
	}
}


VkFormat VulkanBaseApplication::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("failed to find supported format!");
}


VkFormat VulkanBaseApplication::findDepthFormat() {
	return findSupportedFormat(
	{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
}


bool VulkanBaseApplication::hasStencilComponent(VkFormat format) {
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


void VulkanBaseApplication::createDepthResources() {
	VkFormat depthFormat = findDepthFormat();

	createImage(swapChainExtent.width, swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depth.image, depth.mem);
	createImageView(depth.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, depth.view);

	transitionImageLayout(depth.image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}


void VulkanBaseApplication::loadModel(std::vector<Vertex> & vertices, std::vector<uint32_t> & indices, const std::string & modelFilename, float scale) {

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, modelFilename.c_str())) {
		throw std::runtime_error(err);
	}

	std::unordered_map<Vertex, int> uniqueVertices = {};

	for (const auto& shape : shapes) {
		//for (const auto& index : shape.mesh.indices) {
		for (int i = 0; i < shape.mesh.indices.size(); i++) {
			auto& index = shape.mesh.indices[i];
			Vertex vertex = {};

			vertex.pos = {
				scale * attrib.vertices[3 * index.vertex_index + 0],
				scale * attrib.vertices[3 * index.vertex_index + 1],
				scale * attrib.vertices[3 * index.vertex_index + 2]
			};

			vertex.texCoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
			};

			vertex.color = { 1.0f, 1.0f, 1.0f };

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = (uint32_t)vertices.size();
				vertices.push_back(vertex);
			}

			indices.push_back(uniqueVertices[vertex]);
		}
	}


	// compute triangle normal here
	for (int i = 0; i < indices.size(); i += 3) {
		glm::vec3 P1 = vertices[indices[i + 0]].pos;
		glm::vec3 P2 = vertices[indices[i + 1]].pos;
		glm::vec3 P3 = vertices[indices[i + 2]].pos;

		glm::vec3 V = P2 - P1;
		glm::vec3 W = P3 - P1;

		glm::vec3 N = glm::cross(V, W);

		N = glm::normalize(N);
		vertices[indices[i + 0]].normal = N;
		vertices[indices[i + 1]].normal = N;
		vertices[indices[i + 2]].normal = N;
	}
}


	// load axis info
void VulkanBaseApplication::loadAxisInfo() {

	const float axisLen = 1.0f;
	const float axisDelta = 0.1f;
	meshs.axis.vertices.verticesData = {

		{ { 0.0f, 0.0f, 0.0f },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { axisLen, 0.0f, 0.0f },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { axisLen - axisDelta, -axisDelta / 2.0f, 0.0f },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { axisLen - axisDelta, axisDelta / 2.0f, 0.0f },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f } },

		{ { 0.0f, 0.0f, 0.0f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { 0.0f, axisLen, 0.0f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { -axisDelta / 2.0f, axisLen - axisDelta, 0.0f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { axisDelta / 2.0f, axisLen - axisDelta, 0.0f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },

		{ { 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 0.0f } },
		{ { 0.0f, 0.0f, axisLen },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 0.0f } },
		{ { 0.0f, -axisDelta / 2.0f, axisLen - axisDelta },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 0.0f } },
		{ { 0.0f, axisDelta / 2.0f, axisLen - axisDelta },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 0.0f } }
	};

	meshs.axis.indices.indicesData = {
		0, 1,
		1, 2,
		1, 3,

		4, 5,
		5, 6,
		5, 7,

		8, 9,
		9, 10,
		9, 11
	};

}

void VulkanBaseApplication::createMeshBuffer(Mesh & mesh)
{
	createVertexBuffer(mesh.vertices.verticesData, mesh.vertices.buffer, mesh.vertices.mem);
	createIndexBuffer(mesh.indices.indicesData, mesh.indices.buffer, mesh.indices.mem);
}


// load texture quad info
void VulkanBaseApplication::loadTextureQuad() {

	const float x = 1.5f;
	const float axisDelta = 0.1f;
	meshs.quad.vertices.verticesData = {

		{ { 0, 0.25f, 1.5f },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { 0, -0.25f, 1.50f },{ 0.0f, 1.0f, 0.0f },{ 1.0f, 0.0f } },
		{ { 0, -0.25f, 1.0f },{ 0.0f, 0.0f, 1.0f },{ 1.0f, 1.0f } },
		{ { 0, 0.25f, 1.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 1.0f } },

	};

	meshs.quad.indices.indicesData = {
		0, 1, 2, 2, 3, 0,
	};

}


void VulkanBaseApplication::prepareTextures() {
	/*createTextureImage();
	createTextureImageView();
	createTextureSampler();*/

	for (int i = 0; i < textures.size(); ++i) {
		createTextureImage(TEXTURES_PATH[i], textures[i].image, textures[i].imageMemory);
		createTextureImageView(textures[i].image, textures[i].imageView);
		createTextureSampler(textures[i].sampler);
	}

}

VkCommandBuffer VulkanBaseApplication::beginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}


void VulkanBaseApplication::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}


/************************************************************/
//		Debug Callback func and Mouse/keyboard Callback
/************************************************************/
//			Function Implementation
/************************************************************/

// debug call back func
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData) {
	std::cerr << "validation layer: " << msg << std::endl;

	return VK_FALSE;
}


// mouse control related
void cursorPosCallback(GLFWwindow* window, double xPos, double yPos) {
	//std::cout << xPos << "," << yPos << std::endl;
	if (!lbuttonDown && !rbuttonDown) {
		return;
	}
	else if (lbuttonDown) {
		glm::vec2 tmp(xPos, yPos);
		modelRotAngles += (tmp - cursorPos) * 0.01f;

		cursorPos = tmp;
	}
	else if (rbuttonDown) {
		glm::vec2 tmp(xPos, yPos);
		cameraRotAngles += (tmp - cursorPos) * 0.01f;

		cursorPos = tmp;
	}

}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {

	if (action == GLFW_PRESS) {
		double x, y;
		glfwGetCursorPos(window, &x, &y);
		cursorPos = glm::vec2(x, y);

		if (button == GLFW_MOUSE_BUTTON_LEFT) {
			lbuttonDown = true;
		}
		else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
			rbuttonDown = true;
		}
	}
	else if (action == GLFW_RELEASE) {
		if (button == GLFW_MOUSE_BUTTON_LEFT) {
			lbuttonDown = false;
		}
		else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
			rbuttonDown = false;
		}

	}

}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {

	if (action == GLFW_PRESS)
	{
		switch (key)
		{
		case GLFW_KEY_ESCAPE:
			glfwSetWindowShouldClose(window, GLFW_TRUE);
			break;
		case GLFW_KEY_0:
			debugMode = 0;
			break;
		case GLFW_KEY_1:
			debugMode = 1;
			break;
		case GLFW_KEY_2:
			debugMode = 2;
			break;
		case GLFW_KEY_3:
			debugMode = 3;
			break;
		case GLFW_KEY_4:
			debugMode = 4;
			break;
		case GLFW_KEY_5:
			debugMode = 5;
			break;
		default:
			break;
		}
	}

}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
	//std::cout << xoffset << "," << yoffset << std::endl;
	if (cameraPos.length() > FLT_EPSILON) {
		cameraPos -= (float)yoffset * glm::normalize(cameraPos) * 0.05f;
	}
}