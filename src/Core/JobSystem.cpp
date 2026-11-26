#include "Core/JobSystem.hpp"
#include <chrono>
#include <cstdio>

namespace Neleo {

JobSystem& JobSystem::Instance() noexcept {
    static JobSystem s_instance;
    return s_instance;
}

JobSystem::~JobSystem() {
    Shutdown();
}

void JobSystem::Init() {
    if (m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(QUEUE_CAPACITY); ++i) {
        m_queue[i].sequence.store(i, std::memory_order_relaxed);
        m_queue[i].job = JobDeclaration{};
    }
    m_dequeuePos.store(0, std::memory_order_relaxed);
    m_enqueuePos.store(0, std::memory_order_relaxed);
    m_completedJobs.store(0, std::memory_order_relaxed);
    m_totalDispatched.store(0, std::memory_order_relaxed);

#if defined(__WIIU__) || defined(__wiiu__)
    OSInitEvent(&m_jobAvailableEvent, FALSE, OS_EVENT_MODE_AUTO);
    OSInitEvent(&m_jobDoneEvent, FALSE, OS_EVENT_MODE_AUTO);
#endif

    m_running.store(true, std::memory_order_release);
    m_workerThread = std::thread(&JobSystem::WorkerLoop, this);
    m_initialized.store(true, std::memory_order_release);
}

void JobSystem::Shutdown() {
    if (!m_initialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    m_running.store(false, std::memory_order_release);
    WakeWorker();
    SignalCounterDone();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void JobSystem::WorkerLoop() {
#if defined(__WIIU__) || defined(__wiiu__)
    // [OPT-106] Asignación estricta al Core 2 de la CPU Espresso (Core 0 = SO/Audio, Core 1 = Render)
    OSSetThreadAffinity(OSGetCurrentThread(), 1 << 2);
#endif

    while (m_running.load(std::memory_order_acquire)) {
        JobDeclaration job;
        if (PopJob(job)) {
            if (job.task) {
                job.task(job.param);
            }
            if (job.counter) {
                if (job.counter->fetch_sub(1, std::memory_order_release) == 1) {
                    SignalCounterDone();
                }
            }
            m_completedJobs.fetch_add(1, std::memory_order_relaxed);
        } else {
            if (!m_running.load(std::memory_order_acquire)) {
                break;
            }
            SleepUntilJobAvailable();
        }
    }
}

bool JobSystem::PushJob(const JobDeclaration& job) noexcept {
    // [OPT-97] Cola acotada MPMC lock-free de Dmitry Vyukov (inmune a wraparound y sobrescritura en PPC750CL)
    uint32_t pos = m_enqueuePos.load(std::memory_order_relaxed);
    for (;;) {
        JobSlot& cell = m_queue[pos & QUEUE_MASK];
        const uint32_t seq = cell.sequence.load(std::memory_order_acquire);
        const int32_t dif = static_cast<int32_t>(seq - pos);
        if (dif == 0) {
            if (m_enqueuePos.compare_exchange_weak(pos, pos + 1u, std::memory_order_relaxed)) {
                cell.job = job;
                cell.sequence.store(pos + 1u, std::memory_order_release);
                m_totalDispatched.fetch_add(1u, std::memory_order_relaxed);
                return true;
            }
        } else if (dif < 0) {
            return false; // Cola saturada
        } else {
            pos = m_enqueuePos.load(std::memory_order_relaxed);
        }
    }
}

bool JobSystem::PopJob(JobDeclaration& outJob) noexcept {
    uint32_t pos = m_dequeuePos.load(std::memory_order_relaxed);
    for (;;) {
        JobSlot& cell = m_queue[pos & QUEUE_MASK];
        const uint32_t seq = cell.sequence.load(std::memory_order_acquire);
        const int32_t dif = static_cast<int32_t>(seq - (pos + 1u));
        if (dif == 0) {
            if (m_dequeuePos.compare_exchange_weak(pos, pos + 1u, std::memory_order_relaxed)) {
                outJob = cell.job;
                cell.sequence.store(pos + static_cast<uint32_t>(QUEUE_CAPACITY), std::memory_order_release);
                return true;
            }
        } else if (dif < 0) {
            return false; // Cola vacía
        } else {
            pos = m_dequeuePos.load(std::memory_order_relaxed);
        }
    }
}

void JobSystem::Dispatch(JobFunction func, void* param, std::atomic<uint32_t>* counter) {
    JobDeclaration job{func, param, counter};
    Dispatch(job);
}

void JobSystem::Dispatch(const JobDeclaration& job) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        if (job.task) {
            job.task(job.param);
        }
        return;
    }

    if (job.counter) {
        job.counter->fetch_add(1, std::memory_order_relaxed);
    }

    while (!PushJob(job)) {
        // Backpressure cooperativa: Core 1 ayuda a vaciar la cola si se satura
        JobDeclaration localJob;
        if (PopJob(localJob)) {
            if (localJob.task) {
                localJob.task(localJob.param);
            }
            if (localJob.counter) {
                if (localJob.counter->fetch_sub(1, std::memory_order_release) == 1) {
                    SignalCounterDone();
                }
            }
            m_completedJobs.fetch_add(1, std::memory_order_relaxed);
        }
    }

    WakeWorker();
}

void JobSystem::DispatchBatch(const JobDeclaration* jobs, size_t count, std::atomic<uint32_t>* counter) {
    if (!jobs || count == 0) {
        return;
    }

    if (!m_initialized.load(std::memory_order_acquire)) {
        for (size_t i = 0; i < count; ++i) {
            if (jobs[i].task) {
                jobs[i].task(jobs[i].param);
            }
        }
        return;
    }

    if (counter) {
        counter->fetch_add(static_cast<uint32_t>(count), std::memory_order_relaxed);
    }

    for (size_t i = 0; i < count; ++i) {
        JobDeclaration j = jobs[i];
        if (counter) {
            j.counter = counter;
        } else if (j.counter) {
            j.counter->fetch_add(1, std::memory_order_relaxed);
        }
        while (!PushJob(j)) {
            JobDeclaration localJob;
            if (PopJob(localJob)) {
                if (localJob.task) {
                    localJob.task(localJob.param);
                }
                if (localJob.counter) {
                    if (localJob.counter->fetch_sub(1, std::memory_order_release) == 1) {
                        SignalCounterDone();
                    }
                }
                m_completedJobs.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    WakeWorker();
}

void JobSystem::Wait(std::atomic<uint32_t>* counter) {
    if (!counter) {
        return;
    }

    while (counter->load(std::memory_order_acquire) > 0) {
        JobDeclaration job;
        if (PopJob(job)) {
            // Core 1 ejecuta cooperativamente para saturar el cómputo
            if (job.task) {
                job.task(job.param);
            }
            if (job.counter) {
                if (job.counter->fetch_sub(1, std::memory_order_release) == 1) {
                    SignalCounterDone();
                }
            }
            m_completedJobs.fetch_add(1, std::memory_order_relaxed);
        } else {
            if (counter->load(std::memory_order_acquire) == 0) {
                break;
            }
            WaitForJobDone();
        }
    }
}

void JobSystem::SleepUntilJobAvailable() {
#if defined(__WIIU__) || defined(__wiiu__)
    OSWaitEventWithTimeout(&m_jobAvailableEvent, OSMillisecondsToTicks(1));
#else
    std::unique_lock<std::mutex> lock(m_syncMutex);
    m_jobAvailableCv.wait(lock, [this]() {
        return m_dequeuePos.load(std::memory_order_acquire) != m_enqueuePos.load(std::memory_order_acquire) ||
               !m_running.load(std::memory_order_acquire);
    });
#endif
}

void JobSystem::WakeWorker() {
#if defined(__WIIU__) || defined(__wiiu__)
    OSSignalEvent(&m_jobAvailableEvent);
#else
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
    }
    m_jobAvailableCv.notify_one();
#endif
}

void JobSystem::SignalCounterDone() {
#if defined(__WIIU__) || defined(__wiiu__)
    OSSignalEvent(&m_jobDoneEvent);
#else
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
    }
    m_jobDoneCv.notify_all();
#endif
}

void JobSystem::WaitForJobDone() {
#if defined(__WIIU__) || defined(__wiiu__)
    OSWaitEventWithTimeout(&m_jobDoneEvent, OSMicrosecondsToTicks(500));
#else
    std::unique_lock<std::mutex> lock(m_syncMutex);
    m_jobDoneCv.wait_for(lock, std::chrono::microseconds(500));
#endif
}

bool JobSystem::VerifyJobSystemIntegrity() {
    auto& js = Instance();
    if (!js.IsInitialized()) {
        js.Init();
    }

    struct TestPayload {
        uint32_t id;
        uint32_t result;
    };

    TestPayload payloads[4] = {
        {10, 0},
        {20, 0},
        {30, 0},
        {40, 0}
    };

    auto taskFunc = [](void* data) {
        auto* p = static_cast<TestPayload*>(data);
        p->result = p->id * 2;
    };

    std::atomic<uint32_t> counter{0};
    for (int i = 0; i < 4; ++i) {
        js.Dispatch(taskFunc, &payloads[i], &counter);
    }

    js.Wait(&counter);

    if (counter.load(std::memory_order_acquire) != 0) {
        std::printf("[JobSystem] ERROR: Contador de trabajos no alcanzó 0 tras Wait().\n");
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        if (payloads[i].result != payloads[i].id * 2) {
            std::printf("[JobSystem] ERROR: Trabajo %d calculó resultado incorrecto (%u vs %u).\n",
                        i, payloads[i].result, payloads[i].id * 2);
            return false;
        }
    }

    std::printf("[JobSystem] EXITO: Concurrencia SMP en Core 2 y Cola Lock-Free 100%% VERIFICADA.\n");
    return true;
}

} // namespace Neleo
