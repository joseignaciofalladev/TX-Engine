#include "../Application/EngineApplication.h"

void EngineApplication::initThreads()
{
	threadCount = std::thread::hardware_concurrency();

	if (threadCount == 0)
		threadCount = 4;

	threadCount = std::min(threadCount, 8u);

	std::cout << "Initializing " << threadCount << " worker threads\n";

	shouldExit = false;

	threadWorkReady = std::vector<std::atomic<bool>>(threadCount);
	threadWorkDone = std::vector<std::atomic<bool>>(threadCount);

	for (uint32_t i = 0; i < threadCount; i++)
	{
		threadWorkReady[i].store(false);
		threadWorkDone[i].store(true);
	}

	initThreadResources();

	particleGroups.resize(threadCount);

	const uint32_t particlesPerThread = PARTICLE_COUNT / threadCount;

	for (uint32_t i = 0; i < threadCount; i++)
	{
		particleGroups[i].startIndex = i * particlesPerThread;

		particleGroups[i].count =
			(i == threadCount - 1)
			? (PARTICLE_COUNT - particleGroups[i].startIndex)
			: particlesPerThread;

		std::cout
			<< "Thread "
			<< i
			<< " processes particles "
			<< particleGroups[i].startIndex
			<< " -> "
			<< particleGroups[i].startIndex + particleGroups[i].count - 1
			<< '\n';
	}

	workerThreads.clear();
	workerThreads.reserve(threadCount);

	for (uint32_t i = 0; i < threadCount; i++)
	{
		workerThreads.emplace_back(
			&EngineApplication::workerThreadFunc,
			this,
			i);

		std::cout << "Started worker thread " << i << '\n';
	}
}

void EngineApplication::workerThreadFunc(uint32_t threadIndex)
{
	while (!shouldExit)
	{
		{
			std::unique_lock<std::mutex> lock(workCompleteMutex);

			workCompleteCv.wait(lock, [this, threadIndex]()
				{
					return shouldExit.load(std::memory_order_acquire) ||
						threadWorkReady[threadIndex].load(std::memory_order_acquire);
				});

			if (shouldExit.load(std::memory_order_acquire)){break;}
		}

		const ParticleGroup& group = particleGroups[threadIndex];

		try
		{
			auto& cmdBuffer = resourceManager.getCommandBuffer(threadIndex);
			recordComputeCommandBuffer(cmdBuffer, group.startIndex, group.count);
		}
		catch (const std::exception& e)
		{
			std::cerr
				<< "Worker thread "
				<< threadIndex
				<< " failed: "
				<< e.what()
				<< '\n';
			std::terminate();
		}

		threadWorkReady[threadIndex].store(false, std::memory_order_release);
		threadWorkDone[threadIndex].store(true, std::memory_order_release);

		workCompleteCv.notify_all();
	}
}

void EngineApplication::stopThreads()
{
	shouldExit.store(true, std::memory_order_release);

	for (uint32_t i = 0; i < threadCount; i++)
	{
		threadWorkDone[i].store(true, std::memory_order_release);
		threadWorkReady[i].store(false, std::memory_order_release);
	}

	// Notify all threads in case they're waiting on the condition variable
	{
		std::lock_guard<std::mutex> lock(workCompleteMutex);
		workCompleteCv.notify_all();
	}

	for (auto& thread : workerThreads)
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}

	workerThreads.clear();
}

void EngineApplication::initThreadResources()
{
	resourceManager.createThreadCommandPools(device, graphicsQueueFamilyIndex, threadCount);
	resourceManager.allocateCommandBuffers(device, threadCount, 1);
}

void EngineApplication::signalThreadsToWork()
{
    // Reset thread state
    for (uint32_t i = 0; i < threadCount; i++)
    {
        threadWorkDone[i].store(false, std::memory_order_release);
        threadWorkReady[i].store(true, std::memory_order_release);
    }

    // Wake all worker threads
	workCompleteCv.notify_all();
}

void EngineApplication::waitForThreadsToComplete()
{
	std::unique_lock<std::mutex> lock(workCompleteMutex);

	workCompleteCv.wait(lock, [this]()
		{
			for (uint32_t i = 0; i < threadCount; i++)
			{
				if (!threadWorkDone[i].load(std::memory_order_acquire))
					return false;
			}

			return true;
		});
}