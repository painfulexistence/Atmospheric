#pragma once
#include "globals.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>// Required for std::unique_ptr
#include <mutex>
#include <thread>
#include <vector>

// Declared here, defined in job_system.cpp
extern thread_local int G_WORKER_THREAD_INDEX;

using Job = std::function<void(int)>;

class JobSystem {
public:
    static JobSystem* Get() {
        static JobSystem instance;
        return &instance;
    }

    JobSystem();
    ~JobSystem();

    void Init();
    void Execute(const Job& job);
    bool IsBusy();
    void Wait();

    uint32_t GetThreadCount() const {
        return _numThreads;
    }

private:
    bool GetJob(Job& job, uint32_t thread_index);

    uint32_t _numThreads = 0;
    std::vector<std::thread> _threads;
    std::vector<std::deque<Job>> _threadQueues;
    std::vector<std::unique_ptr<std::mutex>> _threadMutexes;// Changed to unique_ptr

    std::condition_variable _waitCondition;
    std::mutex _waitMutex;

    std::atomic<bool> _stopped{ false };
    std::atomic<uint64_t> currentLabel{ 0 };
    std::atomic<uint64_t> finishedLabel{ 0 };

    // For Execute round-robin
    std::atomic<uint32_t> _nextQueue{ 0 };
};