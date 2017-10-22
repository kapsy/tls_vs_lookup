#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dispatch/dispatch.h>

#define Assert(Expression) if(!(Expression)) {abort();}
#define rdtsc __builtin_readcyclecounter

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef intptr_t intptr;
typedef uintptr_t uintptr;

typedef size_t memory_index;
typedef float real32;
typedef double real64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef int32 b32;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float r32;

static dispatch_semaphore_t SemaphoreHandle;

// __thread int TLSValue;
// __thread int Counter;
static __thread int IndexForThread;

// NOTE: (KAPSY) Interesting to note that the performance gap is non existant
// when using 4 threads, and the TLS version has better performance when the
// thread count increases.
#define MAX_THREADS 12
#define START_COUNT (1 << 24)
// NOTE: (KAPSY) Try setting the stride less than 5
// (32*4 bytes = 128 byte cache line) and watch the cycle count jump!
#define CACHE_STRIDE (1 << 5)

static int IDs[MAX_THREADS*CACHE_STRIDE];
static u32 IDIndex;

static int Counters[MAX_THREADS*CACHE_STRIDE];
static int IDsSetup;

inline u32
AtomicAddU32_U32(u32 volatile *TheValue, u32 Addend)
{
    // NOTE: (KAPSY) Returns the original value, prior to adding.
    u32 Result = __sync_fetch_and_add(TheValue, Addend);
    return(Result);
}

inline b32
AtomicCompareExchangeU32_B32(u32 volatile *TheValue, u32 OldValue, u32 NewValue)
{
    b32 Result = __sync_bool_compare_and_swap(TheValue, OldValue, NewValue);

    return(Result);
}

// NOTE: (KAPSY) 10x faster if we add a stride to the counters to separate the
// cache lines!!!
static void *
ThreadProcTLS(void *Payload)
{
    IndexForThread = AtomicAddU32_U32(&IDIndex, 1);
    Counters[IndexForThread*CACHE_STRIDE] = START_COUNT;

    while(!AtomicCompareExchangeU32_B32(&IDIndex, MAX_THREADS, MAX_THREADS)){}

    while(--Counters[IndexForThread*CACHE_STRIDE]) { }

    dispatch_semaphore_signal(SemaphoreHandle);

    return(0);
}

static void
PrintCounts()
{
    for(int Index = 0;
            Index < MAX_THREADS;
            ++Index)
    {
        printf("Index: %d Count: %d\n",
                Index, Counters[Index*CACHE_STRIDE]);
    }
}

static void
PrintIndexesAndCounts()
{
    for(int Index = 0;
            Index < MAX_THREADS;
            ++Index)
    {
        printf("Index: %d ThreadID: %d Count: %d\n",
                Index, IDs[Index*CACHE_STRIDE], Counters[Index*CACHE_STRIDE]);
    }
}

static void *
ThreadProcLinearSearch(void *Payload)
{
    uint64_t ThreadID = 0;
    pthread_threadid_np(0, &ThreadID);
    int Index = AtomicAddU32_U32(&IDIndex, 1);
    IDs[Index*CACHE_STRIDE] = (uint32_t)ThreadID;
    Counters[Index*CACHE_STRIDE] = START_COUNT;

    while(!AtomicCompareExchangeU32_B32(&IDIndex, MAX_THREADS, MAX_THREADS)){}

#if 1
    int ElementsRemaining = 1;
    while(ElementsRemaining)
    {
        uint64_t ThreadID = 0;
        pthread_threadid_np(0, &ThreadID);

        // NOTE: (KAPSY) We perform the search for each iteration to simulate
        // finding the index for that threads counter, as this would have to be
        // done if we didn't use TLS. Worth noting that this is almost
        // certainly slower with the cache stride, and a hash lookup table
        // would probably be better.
        int Index = 0;
        for(; Index < MAX_THREADS; ++Index)
        {
            if(ThreadID == IDs[Index*CACHE_STRIDE])
            {
                break;
            }
        }
        --Counters[Index*CACHE_STRIDE];
        ElementsRemaining = Counters[Index*CACHE_STRIDE];
    }
#endif

    dispatch_semaphore_signal(SemaphoreHandle);

    return(0);
}

int main(int argc, char **argv)
{
    unsigned int InitialSemaphoreCount = 0;
    SemaphoreHandle = dispatch_semaphore_create(InitialSemaphoreCount);

    int ThreadCount = MAX_THREADS;

#if 1
    // NOTE: (KAPSY) Using TLS to store the counter index.
    {
        long long StartCount = rdtsc();

        for(int i = 0; i < ThreadCount; ++i)
        {
            pthread_t Thread;
            int Result = pthread_create(&Thread, 0, &ThreadProcTLS, 0);
        }

        for(int i = 0; i < ThreadCount; ++i)
        {
            dispatch_semaphore_wait(SemaphoreHandle, DISPATCH_TIME_FOREVER);
        }

        long long EndCount = rdtsc();
        float MegaCycles = (float)((EndCount - StartCount) / (1000.f * 1000.f));
        printf("ThreadProc Threads finished: MegaCycles:%.2f\n\n", MegaCycles);

        PrintCounts();
    }
#else
    // NOTE: (KAPSY) Using a lookup table to find the counter index based on
    // the thread id.
    {
        long long StartCount = rdtsc();

        for(int i = 0; i < ThreadCount; ++i)
        {
            pthread_t Thread;
            int Result = pthread_create(&Thread, 0, &ThreadProcLinearSearch, 0);
        }

        for(int i = 0; i < ThreadCount; ++i)
        {
            dispatch_semaphore_wait(SemaphoreHandle, DISPATCH_TIME_FOREVER);
        }

        long long EndCount = rdtsc();
        float MegaCycles = (float)((EndCount - StartCount) / (1000.f * 1000.f));
        printf("ThreadProc Threads finished: MegaCycles:%.2f\n\n", MegaCycles);

        PrintIndexesAndCounts();
    }
#endif

    printf("EXIT_SUCCESS\n");

    return(EXIT_SUCCESS);
}
