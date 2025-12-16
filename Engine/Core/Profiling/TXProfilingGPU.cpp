// ============================================================
// TX Engine — Technologic Experience Engine
// GPU Profiler (Backend-agnostic core)
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

// ------------------------------------------------------------
// Filosofía
// ------------------------------------------------------------
// - No depende de API gráfica concreta (Vulkan, GNM, OpenGL)
// - El backend inyecta timestamps
// - Sin asignaciones dinámicas
// - Coste cero si está desactivado
// - Diseñado para hardware limitado (PS3 / PS4 / low-end PC)
// ------------------------------------------------------------

namespace TX
{

// Configuración
constexpr uint32_t TX_GPU_MAX_ZONES  = 128;
constexpr uint32_t TX_GPU_LATENCY    = 3;   // Frames de retraso GPU

// Timestamp abstracto
struct GPUTimestamp
{
    uint64_t Value;
};

// Zona GPU
struct GPUProfilerZone
{
    const char* Name;
    GPUTimestamp Start;
    GPUTimestamp End;
    uint64_t Duration;
};

// Frame GPU
struct GPUProfilerFrame
{
    GPUProfilerZone Zones[TX_GPU_MAX_ZONES];
    uint32_t ZoneCount;
    bool Ready;
};

// Interfaz backend
class IGPUProfilerBackend
{
public:
    virtual ~IGPUProfilerBackend() = default;

    virtual void WriteTimestamp(GPUTimestamp& out) = 0;
    virtual bool ResolveTimestamp(const GPUTimestamp& start,
                                  const GPUTimestamp& end,
                                  uint64_t& outNanoseconds) = 0;
};

// Profiler GPU
class GPUProfiler
{
public:
    static GPUProfiler& Instance()
    {
        static GPUProfiler P;
        return P;
    }

    void Initialize(IGPUProfilerBackend* backend)
    {
        Backend = backend;
    }

    void BeginFrame()
    {
        WriteIndex = (WriteIndex + 1) % TX_GPU_LATENCY;
        CurrentFrame = &Frames[WriteIndex];
        CurrentFrame->ZoneCount = 0;
        CurrentFrame->Ready = false;
    }

    void EndFrame()
    {
        CurrentFrame->Ready = true;
    }

    void BeginZone(const char* name)
    {
        if (!Backend || CurrentFrame->ZoneCount >= TX_GPU_MAX_ZONES)
            return;

        GPUProfilerZone& Z = CurrentFrame->Zones[CurrentFrame->ZoneCount++];
        Z.Name = name;
        Backend->WriteTimestamp(Z.Start);
    }

    void EndZone()
    {
        if (!Backend || CurrentFrame->ZoneCount == 0)
            return;

        GPUProfilerZone& Z = CurrentFrame->Zones[CurrentFrame->ZoneCount - 1];
        Backend->WriteTimestamp(Z.End);
    }

    void ResolveAndDump()
    {
        uint32_t readIndex = (WriteIndex + TX_GPU_LATENCY - 1) % TX_GPU_LATENCY;
        GPUProfilerFrame& F = Frames[readIndex];

        if (!F.Ready || !Backend)
            return;

        printf("\n[TXProfilerGPU] GPU timings:\n");

        for (uint32_t i = 0; i < F.ZoneCount; ++i)
        {
            GPUProfilerZone& Z = F.Zones[i];

            if (Backend->ResolveTimestamp(Z.Start, Z.End, Z.Duration))
            {
                printf("  %-24s %7.3f ms\n",
                       Z.Name,
                       Z.Duration / 1e6);
            }
        }

        F.Ready = false;
    }

private:
    GPUProfiler()
        : Backend(nullptr)
        , WriteIndex(0)
        , CurrentFrame(nullptr)
    {
        std::memset(Frames, 0, sizeof(Frames));
    }

    IGPUProfilerBackend* Backend;
    GPUProfilerFrame Frames[TX_GPU_LATENCY];
    uint32_t WriteIndex;
    GPUProfilerFrame* CurrentFrame;
};

// RAII helper
class GPUProfileScope
{
public:
    explicit GPUProfileScope(const char* name)
    {
        GPUProfiler::Instance().BeginZone(name);
    }

    ~GPUProfileScope()
    {
        GPUProfiler::Instance().EndZone();
    }
};

// Macros
#define TX_GPU_PROFILE_FRAME_BEGIN() TX::GPUProfiler::Instance().BeginFrame()
#define TX_GPU_PROFILE_FRAME_END()   TX::GPUProfiler::Instance().EndFrame()
#define TX_GPU_PROFILE_SCOPE(name)   TX::GPUProfileScope TX_CONCAT(__txgpuprof__, __LINE__)(name)
}