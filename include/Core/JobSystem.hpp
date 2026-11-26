#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <thread>

#if defined(__WIIU__) || defined(__wiiu__)
#include <coreinit/event.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#else
#include <mutex>
#include <condition_variable>
#endif

namespace Neleo {

using JobFunction = void (*)(void* userData);

/**
 * @brief [OPT-106 / OPT-97] Descriptor inmutable de trabajo individual para el Job System.
 * 
 * Alineado a 32 bytes para prevenir falso compartir en las líneas de caché L1D del PowerPC 750CL.
 */
struct alignas(32) JobDeclaration {
    JobFunction task{nullptr};
    void* param{nullptr};
    std::atomic<uint32_t>* counter{nullptr};
};

/**
 * @brief [OPT-106 / OPT-97 / OPT-269] Sistema de Tareas Multihilo (Job System) para CPU Espresso Tri-Core.
 * 
 * - Despacha tareas analíticas pesadas (Frustum Culling, deformación de mallas, físicas) a Core 2.
 * - Cola circular lock-free de 128 ranuras con semántica acquire-release y aislamiento de 32 bytes.
 * - Suspensión nativa en OSEvent (0% CPU en Core 2 cuando está inactivo).
 * - Ejecución cooperativa en Core 1: si Core 1 espera un contador, ayuda a vaciar la cola de jobs.
 */
class JobSystem {
public:
    static JobSystem& Instance() noexcept;

    void Init();
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

    // Encola un trabajo individual con función y parámetro
    void Dispatch(JobFunction func, void* param = nullptr, std::atomic<uint32_t>* counter = nullptr);

    // Encola un trabajo mediante descriptor
    void Dispatch(const JobDeclaration& job);

    // Encola un lote de trabajos de forma contigua
    void DispatchBatch(const JobDeclaration* jobs, size_t count, std::atomic<uint32_t>* counter = nullptr);

    // Espera cooperativa hasta que el contador llegue a cero (Core 1 ayuda a ejecutar tareas pendientes)
    void Wait(std::atomic<uint32_t>* counter);

    // Autodiagnóstico de concurrencia y sincronización
    [[nodiscard]] static bool VerifyJobSystemIntegrity();

    // Telemetría
    [[nodiscard]] uint32_t GetCompletedJobsCount() const noexcept { return m_completedJobs.load(std::memory_order_relaxed); }
    [[nodiscard]] uint32_t GetTotalDispatchedCount() const noexcept { return m_totalDispatched.load(std::memory_order_relaxed); }

private:
    JobSystem() noexcept = default;
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    void WorkerLoop();
    bool PushJob(const JobDeclaration& job) noexcept;
    bool PopJob(JobDeclaration& outJob) noexcept;
    void SleepUntilJobAvailable();
    void WakeWorker();
    void SignalCounterDone();
    void WaitForJobDone();

    static constexpr size_t QUEUE_CAPACITY = 128;
    static constexpr size_t QUEUE_MASK = QUEUE_CAPACITY - 1;

    struct alignas(32) JobSlot {
        std::atomic<uint32_t> sequence{0};
        JobDeclaration job{};
    };

    alignas(32) JobSlot m_queue[QUEUE_CAPACITY]{};
    alignas(32) std::atomic<uint32_t> m_dequeuePos{0};
    alignas(32) std::atomic<uint32_t> m_enqueuePos{0};

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    std::thread m_workerThread;

    std::atomic<uint32_t> m_completedJobs{0};
    std::atomic<uint32_t> m_totalDispatched{0};

#if defined(__WIIU__) || defined(__wiiu__)
    alignas(32) OSEvent m_jobAvailableEvent{};
    alignas(32) OSEvent m_jobDoneEvent{};
#else
    std::mutex m_syncMutex;
    std::condition_variable m_jobAvailableCv;
    std::condition_variable m_jobDoneCv;
#endif
};

} // namespace Neleo
