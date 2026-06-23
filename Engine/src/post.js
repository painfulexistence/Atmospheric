// Emscripten post-js script running inside the IIFE closure
if (typeof PThread !== 'undefined' && PThread) {
  Module['terminateAllThreads'] = function() {
    console.log("Post-JS: Triggering internal thread pool termination...");
    
    // Call internal Emscripten thread shutdown API
    if (typeof PThread.terminateAllThreads === 'function') {
      try {
        PThread.terminateAllThreads();
      } catch (e) {
        console.error("Post-JS: terminateAllThreads error:", e);
      }
    }
    
    // Force manual worker termination in the pools (names might be mangled in optimized builds, 
    // but inside this closure we can reference PThread properties safely)
    ['runningWorkers', 'unusedWorkers', 'workers'].forEach(function(poolName) {
      try {
        var pool = PThread[poolName];
        if (pool && Array.isArray(pool)) {
          pool.forEach(function(worker) {
            if (worker && typeof worker.terminate === 'function') {
              try {
                worker.terminate();
              } catch (err) {}
            }
          });
          PThread[poolName] = [];
        }
      } catch (e) {
        console.error("Post-JS: failed to clear worker pool: " + poolName, e);
      }
    });
  };
}
