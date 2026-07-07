#include "../Application/EngineApplication.h"

void EngineApplication::initThreads()
{
	uint32_t hwThreads = std::thread::hardware_concurrency();

	if (hwThreads == 0)
		hwThreads = 4;

	threadCount = std::max(1u, hwThreads - 1);

	// Nunca más workers que partículas
	threadCount = std::min(threadCount, PARTICLE_COUNT);

	// Opcional: limitar por trabajo útil
	constexpr uint32_t MinParticlesPerThread = 1024;

	uint32_t desired = (PARTICLE_COUNT + MinParticlesPerThread - 1) / MinParticlesPerThread;

	threadCount = std::clamp(desired, 1u, threadCount);

	std::cout << "Initializing " << threadCount << " worker threads\n";

	shouldExit.store(false, std::memory_order_release);

	threadWorkReady = std::vector<std::atomic<bool>>(threadCount);
	threadWorkDone = std::vector<std::atomic<bool>>(threadCount);

	for (uint32_t i = 0; i < threadCount; i++)
	{
		threadWorkReady[i].store(false, std::memory_order_relaxed);
		threadWorkDone[i].store(true, std::memory_order_relaxed);
	}

	initThreadResources();

	particleGroups.clear();
	particleGroups.resize(threadCount);

	const uint32_t particlesPerThread = (PARTICLE_COUNT + threadCount - 1) / threadCount;

	for (uint32_t i = 0; i < threadCount; ++i)
	{
		uint32_t start = i * particlesPerThread;

		uint32_t end = std::min(start + particlesPerThread, PARTICLE_COUNT);

		if (start >= PARTICLE_COUNT)
		{
			particleGroups[i].startIndex = PARTICLE_COUNT;
			particleGroups[i].count = 0;
		}
		else
		{
			particleGroups[i].startIndex = start;
			particleGroups[i].count = end - start;

			std::cout
				<< "Thread "
				<< i
				<< " processes particles "
				<< particleGroups[i].startIndex
				<< " -> "
				<< (particleGroups[i].count
					? particleGroups[i].startIndex + particleGroups[i].count - 1
					: particleGroups[i].startIndex)
				<< '\n';
		}
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
	while (true)
	{
		std::unique_lock<std::mutex> lock(workCompleteMutex);

		workCompleteCv.wait(lock,
			[this, threadIndex]
			{
				return shouldExit.load(std::memory_order_acquire) ||
					threadWorkReady[threadIndex].load(std::memory_order_acquire);
			});

		if (shouldExit.load(std::memory_order_acquire))
			return;

		threadWorkReady[threadIndex].store(false, std::memory_order_release);

		lock.unlock();

		try
		{
			uint32_t frame = workerFrameIndex.load(std::memory_order_acquire);

			auto& cmdBuffer = resourceManager.getCommandBuffer(threadIndex, frame);

			const ParticleGroup& group = particleGroups[threadIndex];

			recordComputeCommandBuffer(
				cmdBuffer,
				frame,
				group.startIndex,
				group.count);
		}
		catch (const std::exception& e)
		{
			std::lock_guard guard(workCompleteMutex);

			if (!threadException)
				threadException = std::current_exception();
		}

		lock.lock();
		threadWorkDone[threadIndex].store(true, std::memory_order_release);

		lock.unlock();
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
