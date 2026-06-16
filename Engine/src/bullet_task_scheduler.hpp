#pragma once

#include "LinearMath/btThreads.h"
#include "job_system.hpp"

class BulletTaskScheduler : public btITaskScheduler {
    JobSystem& m_jobSystem;

public:
    BulletTaskScheduler(JobSystem& jobSystem, const char* name = "BulletTaskScheduler")
      : btITaskScheduler(name), m_jobSystem(jobSystem) {
    }

    virtual ~BulletTaskScheduler();

    int getMaxNumThreads() const override {
        // +1 for the main thread, as Bullet assigns a unique index to each thread accessing it
        return m_jobSystem.GetThreadCount() + 1;
    }

    int getNumThreads() const override {
        // +1 for the main thread
        return m_jobSystem.GetThreadCount() + 1;
    }

    void setNumThreads(int numThreads) override {
        // Not supported, our JobSystem has a fixed number of threads
    }

    void parallelFor(int iBegin, int iEnd, int grainSize, const btIParallelForBody& body) override;

    btScalar parallelSum(int iBegin, int iEnd, int grainSize, const btIParallelSumBody& body) override {
        // Not implemented for now, can be added later if needed
        // return body.sum(iBegin, iEnd);
        return 0;
    }
};
