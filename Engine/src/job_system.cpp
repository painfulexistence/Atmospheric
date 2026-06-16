#include "job_system.hpp"

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>// Include Tracy for its macros
#endif

// Define the thread-local variable for worker index
#ifdef TRACY_ENABLE
thread_local int G_WORKER_THREAD_INDEX = -1;
#else
thread_local int G_WORKER_THREAD_INDEX = -1;// Still useful for the design even without Tracy
#endif

// Tries to pop a job from the local queue or steal from another
bool JobSystem::GetJob(Job& job, uint32_t thread_index) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // First, try to pop from the front of the local queue
    {
        std::lock_guard<std::mutex> lock(*_threadMutexes[thread_index]);
        if (!_threadQueues[thread_index].empty()) {
            job = std::move(_threadQueues[thread_index].front());
            _threadQueues[thread_index].pop_front();
            return true;
        }
    }

    // Local queue is empty, try to steal from other threads
    uint32_t other_thread = (thread_index + 1) % _numThreads;
    while (other_thread != thread_index) {
        std::lock_guard<std::mutex> lock(*_threadMutexes[other_thread]);
        if (!_threadQueues[other_thread].empty()) {
            // Steal from the back of the other queue
            job = std::move(_threadQueues[other_thread].back());
            _threadQueues[other_thread].pop_back();
            return true;
        }
        other_thread = (other_thread + 1) % _numThreads;
    }

    return false;// No job found
}


JobSystem::JobSystem() {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    // Single-threaded web build (no pthreads): run jobs inline on the main thread.
    _numThreads = 1;
#else
    auto numCores = std::thread::hardware_concurrency();
    _numThreads = std::max(1u, numCores);

    _threadQueues.resize(_numThreads);
    _threadMutexes.resize(_numThreads);
    for (uint32_t i = 0; i < _numThreads; ++i) {
        _threadMutexes[i] = std::make_unique<std::mutex>();
    }

    for (uint32_t threadID = 0; threadID < _numThreads; ++threadID) {
        _threads.emplace_back([this, threadID]() {
#ifdef TRACY_ENABLE
            // Set thread-local index and name for Tracy
            G_WORKER_THREAD_INDEX = threadID;
            tracy::SetThreadName(fmt::format("Worker Thread {}", threadID).c_str());
#endif
            while (!_stopped) {
#ifdef TRACY_ENABLE
                ZoneScopedN("JobSystem Worker Loop");// Scope for overall worker activity
#endif
                Job job;
                if (GetJob(job, threadID)) {
#ifdef TRACY_ENABLE
                    ZoneScopedN("Execute Job");// Scope for individual job execution
#endif
                    job(threadID);
                    finishedLabel.fetch_add(1);

                    // If we just finished the last job, notify the waiting thread
                    if (!IsBusy()) {
                        std::unique_lock<std::mutex> lock(_waitMutex);
                        _waitCondition.notify_all();
                    }
                } else {
                    // No job found, yield to prevent busy-spinning
                    std::this_thread::yield();
                }
            }
        });
    }
#endif
}

JobSystem::~JobSystem() {
    _stopped = true;
    for (auto& thread : _threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void JobSystem::Init() {
    // Already initialized in constructor for now
}

void JobSystem::Execute(const Job& job) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    job(0);
#else
#ifdef TRACY_ENABLE
    ZoneScoped;// Profile job submission
#endif
    currentLabel.fetch_add(1);

    // Round-robin assignment
    uint32_t queueIndex = _nextQueue.fetch_add(1) % _numThreads;

    {
#ifdef TRACY_ENABLE
        ZoneScopedN("Push Job to Queue");
#endif
        std::lock_guard<std::mutex> lock(*_threadMutexes[queueIndex]);
        _threadQueues[queueIndex].push_back(job);
    }

    // Notify potentially waiting worker threads?
    // Currently workers are busy-looping/yielding, so no notification needed unless we change to condition variables
#endif
}

bool JobSystem::IsBusy() {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    return false;
#else
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    return finishedLabel.load() < currentLabel.load();
#endif
}

void JobSystem::Wait() {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    return;
#else
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    while (IsBusy()) {
        // Help out while waiting
        // TODO: Implement work stealing for the main thread or just yield
        std::this_thread::yield();
    }
#ifdef TRACY_ENABLE
    FrameMark;// Explicitly mark frame boundary if waiting for jobs concludes a logical frame
#endif
#endif
}