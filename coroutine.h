/*
 * Single header win32/linux c coroutine implementation.
 * 2019-09-26
 */

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(_WIN32) 
	__declspec(dllimport) void __stdcall SwitchToFiber(void *);
	__declspec(dllimport) void  __stdcall DeleteFiber(void *);
	__declspec(dllimport) void * __stdcall ConvertThreadToFiber(void*);
	__declspec(dllimport) void *__stdcall CreateFiber(unsigned long, void *, void *);
#else
    #include <ucontext.h>
    #include <signal.h>
#endif

#if !defined(JD_COROUTINE_FREE) || !defined(JD_COROUTINE_REALLOC)
    #define JD_COROUTINE_FREE(ud, ptr) free(ptr)
    #define JD_COROUTINE_REALLOC(ud, ptr, sz) realloc(ptr, sz)
#endif

#if !defined(JD_COROUTINE_MEMSET)
    #define JD_COROUTINE_MEMSET(ptr, val, count) memset(ptr, val, count)
#endif

#ifndef JD_COROUTINE_BUCKET_SIZE
    #define JD_COROUTINE_BUCKET_SIZE 32
#endif

struct JD_Coroutine;
struct JD_Coroutine_Runner;
typedef void(*JD_Coroutine_Proc)(JD_Coroutine*co);

struct JD_Coroutine {
    int id;
    JD_Coroutine_Runner * runner;
    JD_Coroutine_Proc proc;
    float wait_time_seconds;

#if defined(_WIN32) 
    void * fiber;
#else
    void * mem_stack;
    ucontext_t context;
#endif
};

struct JD_Coroutine_Runner {
    void * userdata;

    int stack_size_hint;
    int num_available_buckets;
    int coroutine_storage_watermark;
    int coroutine_id_watermark;
    int num_used_buckets;
    JD_Coroutine ** buckets;

#if defined(_WIN32) 
    void * return_fiber;
#else
    ucontext_t return_context;
#endif
};

JD_Coroutine_Runner jd_coroutine_runner_init(
    int stack_size_hint,
    void * userdata
);

bool jd_coroutine_init(
    JD_Coroutine * co,
    JD_Coroutine_Runner * runner,
    JD_Coroutine_Proc * proc
);

void jd_coroutine_runner_tick(
    JD_Coroutine_Runner * runner,
    float delta_time
);

void jd_coroutine_runner_free(
    JD_Coroutine_Runner * runner
);

void jd_coroutine_yield(
    JD_Coroutine * co,
    float wait_time
);

#if defined(JD_COROUTINE_IMPL)

void jd_coroutine_runner_push_bucket(
    JD_Coroutine_Runner * runner
) {
    assert(runner->num_used_buckets <= runner->num_used_buckets);

    if (runner->num_available_buckets >= runner->num_used_buckets) {
        runner->num_available_buckets += 8;

        runner->buckets = (JD_Coroutine**)JD_COROUTINE_REALLOC(
            runner->userdata,
            runner->buckets,
            sizeof(JD_Coroutine*) * runner->num_available_buckets
        );
    }

    int bucket_size = sizeof(JD_Coroutine) * JD_COROUTINE_BUCKET_SIZE;
    JD_Coroutine * bucket = (JD_Coroutine*)JD_COROUTINE_REALLOC(
        runner->userdata,
        0,
        bucket_size
    );
    JD_COROUTINE_MEMSET(bucket, 0, bucket_size);
    runner->buckets[runner->num_used_buckets] = bucket;

    runner->num_used_buckets += 1;
}

JD_Coroutine_Runner jd_coroutine_runner_init(
    int stack_size_hint,
    void * userdata
) {
    JD_Coroutine_Runner runner;
    runner.stack_size_hint = stack_size_hint;
    runner.userdata = userdata;
    runner.num_used_buckets = 0;
    runner.buckets = 0;
    runner.num_available_buckets = 0;
    runner.coroutine_storage_watermark = 0;
    runner.coroutine_id_watermark = 0;

#if defined(_WIN32) 
    runner.return_fiber = ConvertThreadToFiber(0);
#endif

    jd_coroutine_runner_push_bucket(&runner);

    return runner;
}

void jd_coroutine_runner_free(JD_Coroutine_Runner * runner) {

    for (int bucket_index = 0;
        bucket_index < runner->num_used_buckets;
        bucket_index++
        ) {
        JD_Coroutine * coroutines = runner->buckets[bucket_index];

        int max = bucket_index == runner->num_used_buckets - 1
            ? runner->coroutine_storage_watermark
            : JD_COROUTINE_BUCKET_SIZE
            ;

        for (int coroutine_index = 0;
            coroutine_index < max;
            coroutine_index++
            ) {
            JD_Coroutine * coroutine = coroutines + coroutine_index;

            if (!coroutine) {
                continue;
            }
#if defined(_WIN32)
            if (coroutine->fiber) {
                DeleteFiber(coroutine->fiber);
            }
#else
            JD_COROUTINE_FREE(runner->userdata, coroutine->mem_stack);
#endif
        }

        JD_COROUTINE_FREE(runner->userdata, coroutines);
    }

    JD_COROUTINE_FREE(runner->userdata, runner->buckets);

    runner->buckets = 0;
}

void jd_coroutine_entry(JD_Coroutine * co) {
    while (true) {
        if (co->proc) {
            co->proc(co);
        }

        co->proc = 0;
        jd_coroutine_yield(co, 0);
    }
}

void jd_coroutine_begin(
    JD_Coroutine_Runner * runner,
    JD_Coroutine_Proc proc
) {
    JD_Coroutine * co = 0;

    for (int bucket_index = 0;
        bucket_index < runner->num_used_buckets;
        bucket_index++
        ) {
        JD_Coroutine * coroutines = runner->buckets[bucket_index];

        int max = bucket_index == runner->num_used_buckets - 1
            ? runner->coroutine_storage_watermark
            : JD_COROUTINE_BUCKET_SIZE
            ;
        for (int coroutine_index = 0;
            coroutine_index < max;
            coroutine_index++
            ) {
            JD_Coroutine * coroutine = coroutines + coroutine_index;

            if (coroutine && !coroutine->proc) {
                co = coroutine;
                goto found;
            }
        }
    }

    if (!co) {
        if (runner->coroutine_storage_watermark >= JD_COROUTINE_BUCKET_SIZE) {
            jd_coroutine_runner_push_bucket(runner);
            runner->coroutine_storage_watermark = 0;
        }

        co = runner->buckets[runner->num_used_buckets - 1] + runner->coroutine_storage_watermark;
        co->runner = runner;
        co->proc = 0;
        co->wait_time_seconds = 0;
        co->id = runner->coroutine_id_watermark++;

#if defined(_WIN32) 
        assert(runner->return_fiber);
        co->fiber = CreateFiber(runner->stack_size_hint, jd_coroutine_entry, co);
#else
        int stack_size = runner->stack_size_hint == 0
            ? 0x10000
            : runner->stack_size_hint
            ;

        getcontext(&co->context);
        void * mem_stack = JD_COROUTINE_REALLOC(
            runner->userdata,
            0,
            stack_size
        );
        co->mem_stack = mem_stack;

        stack_t new_stack;
        new_stack.ss_sp = mem_stack;
        new_stack.ss_flags = 0;
        new_stack.ss_size = stack_size;
        co->context.uc_stack = new_stack;

        co->context.uc_link = 0;

        makecontext(&co->context, (void(*)())jd_coroutine_entry, 1, co);
#endif
        runner->coroutine_storage_watermark += 1;
    }

found:
    assert(co);
    assert(!co->proc);
    assert(co->runner);

#if defined(_WIN32) 
    assert(co->fiber);
#endif

    co->wait_time_seconds = 0;
    co->proc = proc;
}

void jd_coroutine_yield(
    JD_Coroutine * co,
    float wait_time_seconds
) {
    if (wait_time_seconds > 0) {
        co->wait_time_seconds += wait_time_seconds;
    }

#if defined(_WIN32) 
    SwitchToFiber(co->runner->return_fiber);
#else
    swapcontext(&co->context, &co->runner->return_context);
#endif
}

void jd_coroutine_runner_tick(
    JD_Coroutine_Runner * runner,
    float delta_time
) {
    for (int bucket_index = 0;
        bucket_index < runner->num_used_buckets;
        bucket_index++
        ) {
        JD_Coroutine * coroutines = runner->buckets[bucket_index];

        int max = bucket_index == runner->num_used_buckets - 1
            ? runner->coroutine_storage_watermark
            : JD_COROUTINE_BUCKET_SIZE
            ;

        for (int coroutine_index = 0;
            coroutine_index < max;
            coroutine_index++
            ) {
            JD_Coroutine * coroutine = coroutines + coroutine_index;

            if (!coroutine || !coroutine->proc) {
                continue;
            }

            if (coroutine->wait_time_seconds > 0) {
                coroutine->wait_time_seconds -= delta_time;
                continue;
            }

#if defined(_WIN32) 
            SwitchToFiber(coroutine->fiber);
#else
            swapcontext(&coroutine->runner->return_context, &coroutine->context);
#endif
        }
    }
}

#endif

#if defined(__cplusplus)
}
#endif
