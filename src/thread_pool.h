#pragma once

// =======================================================================
// This code (thread_pool.h and thread_pool.cpp) is Apache-2.0 licensed
// https://github.com/yalcinerbora/mray/blob/main/LICENSE
//
// This is a modified version of the code from the renderer
// https://github.com/yalcinerbora/mray
// =======================================================================
//
// Fix Sized Atomic Multi-producer Multi-consumer ring buffer queue.
// Somewhat advanced implementation. Given producer/consumer count
// vs. queue size ratio, atomic wait amount changes.
//
// Theoretically with infinite queue size this queue will be
// wait free (given producers/consumers keepup with each other).
// One problem is that this queue is wait only. You cannot give up
// producing/consuming after you atomically increment the queue.
//
// Similar queues exists in github. Similar to those designs,
//  this queue uses per-element atomic variable plus
//  atomic head and tail.
//
//  One different of this queue that we always increment the
//  head/tail (producer/consumer). Thus a producer will
//  reserve a slot on the time frame of the queue as such.
//
//  |---|---|---|---|
//    ^   ^   ^   ^
//   p0  p1  p2   p3
//   p4  p5  p6  ...
// Same is true for consumers as well. p0 will wait c0 to finish writing.
// (in initial case system behave as if c0 is already done).
//
// All in all we split the producers/consumers on to multiple single element
// queue's with a naive (round-robin) load balancing scheme.
//
// Problem with this approach is that when a producer/consumer commits
// to an enqueue/dequeue it has to finish it or the design collapses.
// So only other way to wake the waiting producer/consumer is to
// totally terminate the queue.
//
// Also T must be default constructible.
//
#include <cstdint>
#include <utility>
#include <vector>
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
#include <functional>
#include <future>
#include <memory_resource>
#include <memory>
#include <new>

// From https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size.html
#ifdef __cpp_lib_hardware_interference_size
    constexpr std::size_t CACHE_LINE = std::hardware_constructive_interference_size;
#else
    // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │ ...
    constexpr std::size_t CACHE_LINE = 64;
#endif

template<class T>
class MPMCQueueAtomic
{
    using a_uint64_t    = std::atomic_uint64_t;
    using a_bool_t      = std::atomic_bool;
    static constexpr uint64_t CONSUMED = 0;
    static constexpr uint64_t PRODUCED = 1;
    //
    struct alignas(CACHE_LINE) QueueSlot
    {
        a_uint64_t  generation = 0;
        T           item;
    };

    private:
    std::vector<QueueSlot>  data;
    alignas(CACHE_LINE) a_uint64_t    enqueueLoc;
    alignas(CACHE_LINE) a_uint64_t    dequeueLoc;
    a_bool_t            isTerminated;

    static uint64_t ComposeGeneration(uint64_t generation, uint64_t status);


    
    protected:
    public:
    // Constructors & Destructor
                        MPMCQueueAtomic(size_t bufferSize);
                        MPMCQueueAtomic(const MPMCQueueAtomic&) = delete;
                        MPMCQueueAtomic(MPMCQueueAtomic&&) = delete;
    MPMCQueueAtomic&    operator=(const MPMCQueueAtomic&) = delete;
    MPMCQueueAtomic&    operator=(MPMCQueueAtomic&&) = delete;
                        ~MPMCQueueAtomic() = default;

    // Interface
    void Dequeue(T&);
    void Enqueue(T&&);
    //
    bool IsEmpty();
    bool IsFull();
    // Awakes all threads and forces them to leave queue
    void Terminate();
    bool IsTerminated() const;
    // Removes the queued tasks
    void RemoveQueuedTasks(bool reEnable);
};

template<class T>
uint64_t MPMCQueueAtomic<T>::ComposeGeneration(uint64_t generation, uint64_t status)
{
    return (generation & 0x7FFFFFFFFFFFFFFF) | status << 63;
    //Bit::Compose<63, 1>(curGeneration, PRODUCED);
}

template<class T>
MPMCQueueAtomic<T>::MPMCQueueAtomic(size_t bufferSize)
    : data(bufferSize)
    , enqueueLoc(0)
    , dequeueLoc(0)
    , isTerminated(false)
{
    // There has to be a one slot in the queue
    // to bit casting to work
    assert(bufferSize > 0);
}

template<class T>
void MPMCQueueAtomic<T>::Dequeue(T& item)
{
    if (isTerminated) return;
    // We do not have "try" functionality by design
    // Here we only atomic increment and guarantee a slot
    // Here we have minimal contention (no compswap loops etc.)
    uint64_t myLoc = dequeueLoc.fetch_add(1u);
    // Fallback of this is that we got the slot so we have to commit.
    // This means we must wait somewhere.
    uint64_t curGeneration = myLoc / data.size();
    uint64_t curLoc = myLoc % data.size();
    uint64_t genDesiredState = ComposeGeneration(curGeneration, PRODUCED);

    // We will wait on the slot itself.
    // Slot has generation which can only be incremented
    // by a consumer. generation = N means
    // all the processing of all generation before N
    // (excluding N) is done.
    //QueueSlot& curElem = data[curLoc];
    QueueSlot& curElem = data.at(curLoc);
    a_uint64_t& atomicGen = curElem.generation;
    uint64_t expectedGen;
    //
    while((expectedGen = atomicGen.load()) != genDesiredState && !isTerminated)
        atomicGen.wait(expectedGen);
    //
    if(isTerminated) return;
    //
    // We got the required state, we can push the data now
    item = std::move(curElem.item);
    // Signal
    uint64_t genSignalState = ComposeGeneration(curGeneration + 1, CONSUMED);

    atomicGen.store(genSignalState);
    atomicGen.notify_all();
}

template<class T>
void MPMCQueueAtomic<T>::Enqueue(T&& item)
{
    if (isTerminated) return;
    // We do not have "try" functionality by design
    // Here we only atomic increment and guarantee a slot
    // Here we have minimal contention (no compswap loops etc.)
    uint64_t myLoc = enqueueLoc.fetch_add(1u);
    // Fallback of this is that we got the slot so we have to commit.
    // This means we must wait somewhere.
    uint64_t curGeneration = myLoc / data.size();
    uint64_t curLoc = myLoc % data.size();
    uint64_t genDesiredState = ComposeGeneration(curGeneration, CONSUMED);

    // We will wait on the slot itself.
    // Slot has generation which can only be incremented
    // by a consumer. generation = N means
    // all the processing of all generation before N
    // (excluding N) is done.
    //QueueSlot& curElem = data[curLoc];
    QueueSlot& curElem = data.at(curLoc);
    a_uint64_t& atomicGen = curElem.generation;
    uint64_t expectedGen;
    //
    while((expectedGen = atomicGen.load()) != genDesiredState && !isTerminated)
    //
        atomicGen.wait(expectedGen);
    //
    if(isTerminated) return;
    //
    // We got the required state, we can push the data now
    curElem.item = std::move(item);
    // Signal
    uint64_t genSignalState = ComposeGeneration(curGeneration, PRODUCED);;
    atomicGen.store(genSignalState);
    atomicGen.notify_all();
}

template<class T>
void MPMCQueueAtomic<T>::Terminate()
{
    isTerminated = true;
    for(QueueSlot& element : data)
    {
        // Guarantee a change on generation item
        // so that the notify wakes all threads
        element.generation++;
        element.generation.notify_all();
    }
}

template<class T>
bool MPMCQueueAtomic<T>::IsTerminated() const
{
    return isTerminated;
}

template<class T>
bool MPMCQueueAtomic<T>::IsEmpty()
{
    return (enqueueLoc - dequeueLoc) == 0;
}

template<class T>
bool MPMCQueueAtomic<T>::IsFull()
{
    return (enqueueLoc - dequeueLoc) == data.size();
}

template<class T>
void MPMCQueueAtomic<T>::RemoveQueuedTasks(bool reEnable)
{
    // TODO: Although this function works
    // it is not complete. We need to change the design
    // of the thread pool to remove the ABA problem.
    // also some form of cleanup is needed.

    // ***ABA Problem!***
    // Assume generation was 0
    // we increment it, so all the threads waiting over the generation
    // is awake (generation could've been any value so we increment)
    //isTerminated = true;
    for(QueueSlot& element : data)
    {
        element.generation = 0;
        element.item = T();
        element.generation.notify_all();
    }
    enqueueLoc = 0;
    dequeueLoc = 0;

    // ***ABA Problem! Cont.***
    // If someone was waiting for 0 and
    // couldn't wake up on time, will see the generation as zero
    // and sleep?
    // is awake (generation could've been any value so we increment)
    // for(QueueSlot& element : data)
    // {
    //     element.generation.notify_all();
    //     element.generation = 0;
    // }

    if(reEnable) isTerminated = false;
}

// TODO: Check if std has these predefined somewhere?
// (i.e. concept "callable_as")
template<class T>
concept ThreadBlockWorkC = requires(const T& t)
{
    { t(uint32_t(), uint32_t()) } -> std::same_as<void>;
};

template<class T>
concept ThreadTaskWorkC = requires(const T& t)
{
    { t() } -> std::same_as<std::invoke_result_t<T>>;
};

template<class T>
concept ThreadDetachableTaskWorkC = requires(const T& t)
{
    { t() } -> std::same_as<void>;
};


template<class T>
concept ThreadInitFuncC = requires(const T& t)
{
    { t(std::thread::native_handle_type(), uint32_t()) } -> std::same_as<void>;
};

template<class T>
struct MultiFuture
{
    std::vector<std::future<T>> futures;
    //
    void            WaitAll() const;
    bool            AnyValid() const;
    std::vector<T>  GetAll();
};

template<>
struct MultiFuture<void>
{
    std::vector<std::future<void>> futures;
    //
    void WaitAll() const
    {
        for(const std::future<void>& f : futures)
        {
            f.wait();
        }
    }
    //
    bool AnyValid() const
    {
        // This is technically valid for "void" futures.
        // We allow user to submit empty work block works
        // It will return empty multi future
        if(futures.empty()) return true;

        for(const std::future<void>& f : futures)
        {
            if(f.valid()) return true;
        }
        return false;
    }
    //
    void GetAll()
    {
        if(futures.empty()) return;
        for(std::future<void>& f : futures)
        {
            f.get();
        }
        return;
    }
};

// Type erase via shared_ptr<void>
// We try to reduce allocation count with this.
// TODO: Group promise/callable to a single shared pointer later
struct TPCallable
{
    using Deleter       = void(*)(void);
    //using InternalFunc  = std::function<void(const void*, void*, uint32_t, uint32_t)>;
    using InternalFunc  = void(*)(const void*, void*, uint32_t, uint32_t);
    //
    std::shared_ptr<void>       workFunc;
    std::shared_ptr<void>       promise;
    uint32_t                    start   = 0;
    uint32_t                    end     = 0;
    InternalFunc                call    = nullptr;

    template<ThreadBlockWorkC T, class R>
    static void BlockTaskCall(const void* workFuncRaw, void* promiseRaw,
                              uint32_t start, uint32_t end)
    {
        using P = std::promise<R>;
        const T* wfPtr = reinterpret_cast<const T*>(workFuncRaw);
        P* promisePtr = reinterpret_cast<P*>(promiseRaw);
        wfPtr = std::launder(wfPtr); promisePtr = std::launder(promisePtr);
        const T& wf = *wfPtr;
        P& promise = *promisePtr;
        try
        {
            if constexpr(std::is_same_v<R, void>)
            {
                wf(start, end);
                promise.set_value();
            }
            else
            {
                promise.set_value(wf(start, end));
            }
        }
        catch(...)
        {
            promise.set_exception(std::current_exception());
        }
    }

    template<ThreadTaskWorkC T, class R>
    static void TaskCall(const void* workFuncRaw, void* promiseRaw, uint32_t, uint32_t)
    {
        using P = std::promise<R>;
        const T* wfPtr = reinterpret_cast<const T*>(workFuncRaw);
        P* promisePtr = reinterpret_cast<P*>(promiseRaw);
        wfPtr = std::launder(wfPtr); promisePtr = std::launder(promisePtr);
        const T& wf = *wfPtr;
        P& promise = *promisePtr;
        try
        {
            if constexpr(std::is_same_v<R, void>)
            {
                wf();
                promise.set_value();
            }
            else
            {
                promise.set_value(wf());
            }
        }
        catch(...)
        {
            promise.set_exception(std::current_exception());
        }
    }

    template<ThreadBlockWorkC T>
    static void DetachedBlockTaskCall(const void* workFuncRaw, void*,
                                       uint32_t start, uint32_t end)
    {
        const T* wfPtr = reinterpret_cast<const T*>(workFuncRaw);
        wfPtr = std::launder(wfPtr);
        const T& wf = *wfPtr;
        try
        {
            wf(start, end);
        }
        catch(...)
        {
            std::fprintf(stderr, "Exception caught on a detached block task! Terminating process\n");
            std::exit(EXIT_FAILURE);
        }
    }

    template<ThreadDetachableTaskWorkC T>
    static void DetachedTaskCall(const void* workFuncRaw, void*, uint32_t, uint32_t)
    {
        const T* wfPtr = reinterpret_cast<const T*>(workFuncRaw);
        wfPtr = std::launder(wfPtr);
        const T& wf = *wfPtr;
        try
        {
            wf();
        }
        catch(...)
        {
            std::fprintf(stderr, "Exception caught on a detached task! Terminating process\n");
            std::exit(EXIT_FAILURE);
        }
    }

    public:
    // Constructors & Destructor
    // TODO: How to define this outside of the class?
    // Type erasure via templated constructor
    // Destruction is handled by shared_ptr automatically.
    template<ThreadBlockWorkC T, class R>
    TPCallable(std::shared_ptr<T> work, std::shared_ptr<std::promise<R>> p,
               uint32_t startIn, uint32_t endIn)
        : workFunc(work)
        , promise(p)
        , start(startIn)
        , end(endIn)
    {
        assert(work != nullptr);
        assert(p != nullptr);
        call = static_cast<InternalFunc>(&BlockTaskCall<T, R>);
    }

    template<ThreadTaskWorkC T, class R>
    TPCallable(std::shared_ptr<T> work, std::shared_ptr<std::promise<R>> p)
        : workFunc(work)
        , promise(p)
        , start(0)
        , end(0)
    {
        assert(work != nullptr);
        assert(p != nullptr);
        call = static_cast<InternalFunc>(&TaskCall<T, R>);
    }

    template<ThreadBlockWorkC T>
    TPCallable(std::shared_ptr<T> work, uint32_t startIn, uint32_t endIn)
        : workFunc(work)
        , promise(nullptr)
        , start(startIn)
        , end(endIn)
    {
        assert(work != nullptr);
        call = static_cast<InternalFunc>(&DetachedBlockTaskCall<T>);
    }

    template<ThreadDetachableTaskWorkC T>
    TPCallable(std::shared_ptr<T> work)
        : workFunc(work)
        , promise(nullptr)
        , start(0)
        , end(0)
    {
        assert(work != nullptr);
        call = static_cast<InternalFunc>(&TPCallable::DetachedTaskCall<T>);
    }
    // Constructors & Destructor continued...
                TPCallable() = default;
                TPCallable(const TPCallable&) = delete;
                TPCallable(TPCallable&&) = default;
    TPCallable& operator=(const TPCallable&) = delete;
    TPCallable& operator=(TPCallable&&) = default;
                ~TPCallable() = default;

    void operator()() const;
};

class ThreadPool
{
    public:
    static constexpr uint32_t DefaultQueueSize = 512;

    struct InitParams
    {
        uint32_t threadCount;
        uint32_t queueSize = DefaultQueueSize;
    };

    private:
    // TODO: These are costly on top of that shared memory is costly
    // and it has a bug on clang (see below) which does not generate some
    // code.
    // In the end we need to have a free list + a static arena allocator.
    // (since MPMC queue size is static).
    // It is not hard to implement anyway so we need to do it
    // since sync_pool_resource uses a mutex over unsync_pool_resource
    // which is lazy (it could've used a proper atomic stuff per pool etc.
    std::pmr::monotonic_buffer_resource     baseAllocator;
    std::pmr::synchronized_pool_resource    poolAllocator;
    //
    MPMCQueueAtomic<TPCallable>             taskQueue;
    //
    std::vector<std::jthread>               threads;
    std::atomic_uint64_t                    issuedTaskCount;
    // completedCounter is mostly written by worker threads,
    // issuedCounter is written by producer thread(s).
    // Putting a gap here should eliminate data transfer between
    // threads
    alignas(CACHE_LINE)
    std::atomic_uint64_t                    completedTaskCount;

    void RestartThreadsImpl(uint32_t threadCount);

    protected:
    public:
    // Constructors & Destructor
                    ThreadPool(size_t queueSize = DefaultQueueSize);
                    ThreadPool(InitParams);
                    ThreadPool(const ThreadPool&) = delete;
                    ThreadPool(ThreadPool&&) = delete;
    ThreadPool&     operator=(const ThreadPool&) = delete;
    ThreadPool&     operator=(ThreadPool&&) = delete;
                    ~ThreadPool();

    void            RestartThreads(uint32_t threadCount);
    uint32_t        ThreadCount() const;
    void            Wait();
    void            ClearTasks();

    //
    template<ThreadBlockWorkC WorkFunc>
    MultiFuture<void>
    SubmitBlocks(uint32_t totalWorkSize, WorkFunc&&,
                 uint32_t partitionCount = 0);
    //
    template<ThreadTaskWorkC WorkFunc>
    std::future<std::invoke_result_t<WorkFunc>>
    SubmitTask(WorkFunc&&);

    template<ThreadBlockWorkC WorkFunc>
    void SubmitDetachedBlocks(uint32_t totalWorkSize, WorkFunc&&,
                              uint32_t partitionCount = 0);

    template<ThreadDetachableTaskWorkC WorkFunc>
    void SubmitDetachedTask(WorkFunc&&);
};

template<class T>
void MultiFuture<T>::WaitAll() const
{
    for(const std::future<T>& f : futures)
    {
        f.wait();
    }
}

template<class T>
bool MultiFuture<T>::AnyValid() const
{
    for(const std::future<T>& f : futures)
    {
        if(f.valid()) return true;
    }
    return false;
}

template<class T>
std::vector<T> MultiFuture<T>::GetAll()
{
    std::vector<T> result;
    result.reserve(futures.size());
    for(std::future<T>& f : futures)
    {
        result.emplace_back(f.get());
    }
    return result;
}

inline
void TPCallable::operator()() const
{
    if(call) call(workFunc.get(), promise.get(), start, end);
}

inline void ThreadPool::RestartThreads(uint32_t threadCount)
{
    RestartThreadsImpl(threadCount);
}

template<ThreadBlockWorkC WorkFunc>
MultiFuture<void>
ThreadPool::SubmitBlocks(uint32_t totalWorkSize, WorkFunc&& wf,
                         uint32_t partitionCount)
{
    if(totalWorkSize == 0) return MultiFuture<void>{};

    // Determine partition size etc..
    if(partitionCount == 0)
        partitionCount = static_cast<uint32_t>(threads.size());
    partitionCount = std::min(totalWorkSize, partitionCount);
    uint32_t sizePerPartition = totalWorkSize / partitionCount;
    uint32_t residual = totalWorkSize - sizePerPartition * partitionCount;
    issuedTaskCount.fetch_add(partitionCount);

    // Store the work functor somewhere safe.
    // Work functor will be copied once to a shared_ptr,
    // used multiple times, then gets deleted automatically
    // TODO: Clang has a bug in which it does not generate
    // code for some functions.
    // https://github.com/llvm/llvm-project/issues/57561
    // Wrapping it to std function fixes the issue
    // (Although defeats the entire purpose of the new system...)
    //using WFType = std::remove_cvref_t<WorkFunc>;
    using WFType = std::function<void(uint32_t, uint32_t)>;
    auto sharedWork = std::allocate_shared<WFType>(std::pmr::polymorphic_allocator<WFType>(&poolAllocator),
                                                   std::forward<WorkFunc>(wf));

    // Enqueue the type erased works to the queue
    MultiFuture<void> result;
    result.futures.reserve(partitionCount);
    uint32_t startOffset = 0;
    for(uint32_t i = 0; i < partitionCount; i++)
    {
        uint32_t start = startOffset;
        uint32_t end = start + sizePerPartition;
        if(residual > 0)
        {
            end++;
            residual--;
        }
        startOffset = end;

        using AllocT = std::pmr::polymorphic_allocator<std::promise<void>>;
        auto promise = std::allocate_shared<std::promise<void>>(AllocT(&poolAllocator));
        auto future = promise->get_future();

        taskQueue.Enqueue(TPCallable(sharedWork, promise, start, end));
        result.futures.push_back(std::move(future));
    }
    assert(residual == 0);
    assert(startOffset == totalWorkSize);

    return result;
}

template<ThreadTaskWorkC WorkFunc>
std::future<std::invoke_result_t<WorkFunc>>
ThreadPool::SubmitTask(WorkFunc&& wf)
{
    issuedTaskCount.fetch_add(1);

    using WFType = std::remove_cvref_t<WorkFunc>;
    using ResultT = std::invoke_result_t<WFType>;
    using AllocT = std::pmr::polymorphic_allocator<std::promise<ResultT>>;
    // Store the work functor somewhere safe.
    // Work functor will be copied once to a shared_ptr,
    // used multiple times, then gets deleted automatically
    auto sharedWork = std::allocate_shared<WFType>(std::pmr::polymorphic_allocator<WFType>(&poolAllocator),
                                                   std::forward<WorkFunc>(wf));
    auto promise = std::allocate_shared<std::promise<ResultT>>(AllocT(&poolAllocator));
    auto future = promise->get_future();

    taskQueue.Enqueue(TPCallable(sharedWork, promise));
    return future;
}

template<ThreadBlockWorkC WorkFunc>
void ThreadPool::SubmitDetachedBlocks(uint32_t totalWorkSize, WorkFunc&& wf,
                                      uint32_t partitionCount)
{
    if(totalWorkSize == 0) return;

    // Determine partition size etc..
    if(partitionCount == 0)
        partitionCount = static_cast<uint32_t>(threads.size());
    partitionCount = std::min(totalWorkSize, partitionCount);
    uint32_t sizePerPartition = totalWorkSize / partitionCount;
    uint32_t residual = totalWorkSize - sizePerPartition * partitionCount;
    issuedTaskCount.fetch_add(partitionCount);

    // Store the work functor somewhere safe.
    // Work functor will be copied once to a shared_ptr,
    // used multiple times, then gets deleted automatically
    // TODO: Clang has a bug in which it does not generate
    // code for some functions.
    // https://github.com/llvm/llvm-project/issues/57561
    // Wrapping it to std function fixes the issue
    // (Although defeats the entire purpose of the new system...)
    //using WFType = std::remove_cvref_t<WorkFunc>;
    using WFType = std::function<void(uint32_t, uint32_t)>;
    auto sharedWork = std::allocate_shared<WFType>(std::pmr::polymorphic_allocator<WFType>(&poolAllocator),
                                                   std::forward<WorkFunc>(wf));
    // Enqueue the type erased works to the queue
    uint32_t startOffset = 0;
    for(uint32_t i = 0; i < partitionCount; i++)
    {
        uint32_t start = startOffset;
        uint32_t end = start + sizePerPartition;
        if(residual > 0)
        {
            end++;
            residual--;
        }
        startOffset = end;
        taskQueue.Enqueue(TPCallable(sharedWork, start, end));
    }
    assert(residual == 0);
    assert(startOffset == totalWorkSize);
}

template<ThreadDetachableTaskWorkC WorkFunc>
void ThreadPool::SubmitDetachedTask(WorkFunc&& wf)
{
    issuedTaskCount.fetch_add(1);
    using WFType = std::remove_cvref_t<WorkFunc>;
    // Store the work functor somewhere safe.
    // Work functor will be copied once to a shared_ptr,
    // used multiple times, then gets deleted automatically
    auto sharedWork = std::allocate_shared<WFType>(std::pmr::polymorphic_allocator<WFType>(&poolAllocator),
                                                   std::forward<WorkFunc>(wf));
    taskQueue.Enqueue(TPCallable(sharedWork));
}
