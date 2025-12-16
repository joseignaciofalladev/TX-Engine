// ============================================================
// TX Engine — Technologic Experience Engine
// Ultra-Lightweight CPU/GPU Profiler
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>

// ------------------------------------------------------------
// Filosofía
// ------------------------------------------------------------
// - Sin dependencias externas
// - Sin asignaciones dinámicas
// - Coste casi cero cuando está desactivado
// - Diseñado para PS3 / hardware débil
// - Pensado para ingeniería, no marketing
// ------------------------------------------------------------

namespace TX
{

// Configuración
constexpr uint32_t TX_MAX_PROFILER_ZONES = 256;
constexpr uint32_t TX_MAX_FRAMES        = 120;

// Utilidades de tiempo
static inline uint64_t GetTimeNS()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// Zona de profiling
struct ProfilerZone
{
    const char* Name;
    uint64_t StartTime;
    uint64_t Accumulated;
    uint32_t CallCount;
};

// Frame profiling

struct ProfilerFrame
{
    ProfilerZone Zones[TX_MAX_PROFILER_ZONES];
    uint32_t ZoneCount;
    uint64_t FrameStart;
    uint64_t FrameEnd;
};

// Profiler principal
class Profiler
{
public:
    static Profiler& Instance()
    {
        static Profiler P;
        return P;
    }

    void BeginFrame()
    {
        CurrentFrame = (CurrentFrame + 1) % TX_MAX_FRAMES;
        Frame = &Frames[CurrentFrame];
        Frame->ZoneCount = 0;
        Frame->FrameStart = GetTimeNS();
    }

    void EndFrame()
    {
        Frame->FrameEnd = GetTimeNS();
    }

    void BeginZone(const char* name)
    {
        if (Frame->ZoneCount >= TX_MAX_PROFILER_ZONES)
            return;

        ProfilerZone& Z = Frame->Zones[Frame->ZoneCount++];
        Z.Name = name;
        Z.StartTime = GetTimeNS();
        Z.Accumulated = 0;
        Z.CallCount = 1;
    }

    void EndZone()
    {
        if (Frame->ZoneCount == 0)
            return;

        ProfilerZone& Z = Frame->Zones[Frame->ZoneCount - 1];
        Z.Accumulated += GetTimeNS() - Z.StartTime;
    }

    void DumpLastFrame()
    {
        const ProfilerFrame& F = *Frame;
        uint64_t frameTime = F.FrameEnd - F.FrameStart;

        printf("\n[TXProfiler] Frame time: %.3f ms\n", frameTime / 1e6);

        for (uint32_t i = 0; i < F.ZoneCount; ++i)
        {
            const ProfilerZone& Z = F.Zones[i];
            double ms = Z.Accumulated / 1e6;
            double pct = (double)Z.Accumulated * 100.0 / (double)frameTime;

            printf("  %-24s %7.3f ms  (%5.1f%%)\n", Z.Name, ms, pct);
        }
    }

private:
    Profiler() : CurrentFrame(0), Frame(nullptr)
    {
        std::memset(Frames, 0, sizeof(Frames));
    }

    ProfilerFrame Frames[TX_MAX_FRAMES];
    uint32_t CurrentFrame;
    ProfilerFrame* Frame;
};

// RAII helper (scoped profiling)
class ProfileScope
{
public:
    explicit ProfileScope(const char* name)
    {
        Profiler::Instance().BeginZone(name);
    }

    ~ProfileScope()
    {
        Profiler::Instance().EndZone();
    }
};

// Macros de uso (desactivables)
#define TX_PROFILE_FRAME_BEGIN() TX::Profiler::Instance().BeginFrame()
#define TX_PROFILE_FRAME_END()   TX::Profiler::Instance().EndFrame()
#define TX_PROFILE_SCOPE(name)   TX::ProfileScope TX_CONCAT(__txprof__, __LINE__)(name)

// Ejemplo de uso
void Render()
{
    TX_PROFILE_SCOPE("Render");
    // Renderizado...
}

void UpdateAI()
{
    TX_PROFILE_SCOPE("AI");
    // Lógica IA...
}

void GameLoop()
{
    TX_PROFILE_FRAME_BEGIN();

    UpdateAI();
    Render();

    TX_PROFILE_FRAME_END();
    TX::Profiler::Instance().DumpLastFrame();
}
}