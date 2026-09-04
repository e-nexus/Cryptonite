// Copyright (c) 2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CHECKQUEUE_H
#define CHECKQUEUE_H

#include <algorithm>
#include <vector>

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>

template<typename T> class CCheckQueueControl;

/** Queue for verifications that have to be performed.
  * The verifications are represented by a type T, which must provide an
  * operator(), returning a bool.
  *
  * One thread (the master) is assumed to push batches of verifications
  * onto the queue, where they are processed by N-1 worker threads. When
  * the master is done adding work, it temporarily joins the worker pool
  * as an N'th worker, until all jobs are done.
  */
template<typename T> class CCheckQueue {
private:
    // Mutex to protect the inner state
    boost::mutex mutex;

    // Worker threads block on this when out of work
    boost::condition_variable condWorker;

    // Master thread blocks on this when out of work
    boost::condition_variable condMaster;

    // The queue of elements to be processed.
    // As the order of booleans doesn't matter, it is used as a LIFO (stack)
    std::vector<T> queue;

    // The number of workers (including the master) that are idle.
    int nIdle;

    // The total number of workers (including the master).
    int nTotal;

    // The temporary evaluation result.
    bool fAllOk;

    // Number of verifications that haven't completed yet.
    // This includes elements that are not anymore in queue, but still in
    // worker's own batches.
    unsigned int nTodo;

    // Whether we're shutting down.
    bool fQuit;

    // Sticky flag set by Loop() when it observes
    // boost::this_thread::interruption_requested() on the worker thread.
    // The owning thread function (ThreadScriptCheck) calls Thread() in a
    // loop; once a worker has self-cancelled it must not re-enter Loop()
    // on the next iteration, because the boost interrupt flag is sticky
    // and Thread() would return true again, spinning forever.
    bool fInterrupted;

    // The maximum number of elements to be processed in one batch
    unsigned int nBatchSize;

    // Internal function that does bulk of the verification work.
    bool Loop(bool fMaster = false) {
        boost::condition_variable &cond = fMaster ? condMaster : condWorker;
        std::vector<T> vChecks;
        vChecks.reserve(nBatchSize);
        unsigned int nNow = 0;
        bool fOk = true;
        do {
            {
                boost::unique_lock<boost::mutex> lock(mutex);
                // first do the clean-up of the previous loop run (allowing us to do it in the same critsect)
                if (nNow) {
                    fAllOk &= fOk;
                    nTodo -= nNow;
                    if (nTodo == 0 && !fMaster)
                        // We processed the last element; inform the master he can exit and return the result
                        condMaster.notify_one();
                } else {
                    // first iteration
                    nTotal++;
                }
                // logically, the do loop starts here
                while (queue.empty()) {
                    // Wait until work arrives, the queue is told to quit, or
                    // this worker thread is interrupted (test fixture's
                    // threadGroup.interrupt_all() relies on the latter so the
                    // fixture destructor can join_all() the workers).
                    if ((fMaster || fQuit) || boost::this_thread::interruption_requested()) {
                        if (boost::this_thread::interruption_requested()) {
                            fInterrupted = true;
                            fQuit = true;
                        }
                        if ((fMaster || fQuit) && nTodo == 0) {
                            nTotal--;
                            bool fRet = fAllOk;
                            // reset the status for new work later
                            if (fMaster)
                                fAllOk = true;
                            // return the current status
                            return fRet;
                        }
                    }
                    nIdle++;
                    // Use a timed wait so a worker that misses a notify can
                    // still observe fQuit / interruption_requested() on the
                    // next loop iteration within bounded time. boost::condition_
                    // variable::wait() is already an interruption point so
                    // boost::thread::interrupt() from the test fixture wakes
                    // parked workers immediately -- the 500ms timeout is a
                    // belt-and-braces guard against a missed notify on the
                    // shutdown path.
                    cond.timed_wait(lock, boost::posix_time::milliseconds(500));
                    nIdle--;
                }
                // Decide how many work units to process now.
                // * Do not try to do everything at once, but aim for increasingly smaller batches so
                //   all workers finish approximately simultaneously.
                // * Try to account for idle jobs which will instantly start helping.
                // * Don't do batches smaller than 1 (duh), or larger than nBatchSize.
                nNow = std::max(1U, std::min(nBatchSize, (unsigned int)queue.size() / (nTotal + nIdle + 1)));
                vChecks.resize(nNow);
                for (unsigned int i = 0; i < nNow; i++) {
                     // We want the lock on the mutex to be as short as possible, so swap jobs from the global
                     // queue to the local batch vector instead of copying.
                     vChecks[i].swap(queue.back());
                     queue.pop_back();
                }
                // Check whether we need to do work at all
                fOk = fAllOk;
            }
            // execute work
            for (T &check : vChecks)
                if (fOk)
                    fOk = check();
            vChecks.clear();
        } while(true);
    }

public:
    // Create a new check queue
    CCheckQueue(unsigned int nBatchSizeIn) :
        nIdle(0), nTotal(0), fAllOk(true), nTodo(0), fQuit(false), fInterrupted(false), nBatchSize(nBatchSizeIn) {}

    // Worker thread. Returns early if the worker has already
    // self-cancelled via interruption_requested() -- the sticky
    // fInterrupted flag prevents the owning thread function from
    // re-entering Loop() in a tight spin when boost::thread::interrupt()
    // has already been observed by an earlier iteration.
    void Thread() {
        if (fInterrupted)
            return;
        Loop();
    }

    // Wait until execution finishes, and return whether all evaluations where succesful.
    bool Wait() {
        return Loop(true);
    }

    // Add a batch of checks to the queue
    void Add(std::vector<T> &vChecks) {
        boost::unique_lock<boost::mutex> lock(mutex);
        for (T &check : vChecks) {
            queue.emplace_back(T());
            check.swap(queue.back());
        }
        nTodo += vChecks.size();
        if (vChecks.size() == 1)
            condWorker.notify_one();
        else if (vChecks.size() > 1)
            condWorker.notify_all();
    }

    ~CCheckQueue() {
    }

    friend class CCheckQueueControl<T>;
};

/** RAII-style controller object for a CCheckQueue that guarantees the passed
 *  queue is finished before continuing.
 */
template<typename T> class CCheckQueueControl {
private:
    CCheckQueue<T> *pqueue;
    bool fDone;

public:
    CCheckQueueControl(CCheckQueue<T> *pqueueIn) : pqueue(pqueueIn), fDone(false) {
        // passed queue is supposed to be unused, or nullptr
        if (pqueue != nullptr) {
            assert(pqueue->nTotal == pqueue->nIdle);
            assert(pqueue->nTodo == 0);
            assert(pqueue->fAllOk == true);
        }
    }

    bool Wait() {
        if (pqueue == nullptr)
            return true;
        bool fRet = pqueue->Wait();
        fDone = true;
        return fRet;
    }

    void Add(std::vector<T> &vChecks) {
        if (pqueue != nullptr)
            pqueue->Add(vChecks);
    }

    ~CCheckQueueControl() {
        if (!fDone)
            Wait();
    }
};

#endif
