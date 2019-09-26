/*
 * Single header win32/linux c coroutine implementation.
 * 2019-09-26
 */

#if !defined(JD_COROUTINE_FREE) || !defined(JD_COROUTINE_REALLOC)
    #define JD_COROUTINE_FREE(ud, ptr) free(ptr)
    #define JD_COROUTINE_REALLOC(ud, ptr, sz) realloc(ptr, sz)
#endif

#if !defined(JD_COROUTINE_MEMSET)
    #define JD_COROUTINE_MEMSET(ptr, val, count) memset(ptr, val, count)
#endif

#ifndef JD_COROUTINE_BUCKET_SIZE
	#define JD_COROUTINE_BUCKET_SIZE 1
#endif

struct JD_Coroutine;
struct JD_Coroutine_Runner;
typedef void(*JD_Coroutine_Proc)(JD_Coroutine*co);

struct JD_Coroutine {
    int id;
    JD_Coroutine_Runner * runner;
    JD_Coroutine_Proc proc;
    float wait_time_seconds;

    void * fiber;
};

struct JD_Coroutine_Runner {
    void * return_fiber;
    void * userdata;
    
    int num_available_buckets;
    int coroutine_storage_watermark;
    int coroutine_id_watermark;
    int num_used_buckets;
    JD_Coroutine ** buckets;
};

JD_Coroutine_Runner jd_coroutine_runner_init(
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

// NOTE(justas): impl

void jd_coroutine_runner_push_bucket(
        JD_Coroutine_Runner * runner
) {
    assert(runner->num_used_buckets <= runner->num_used_buckets);

    if(runner->num_available_buckets >= runner->num_used_buckets) {
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
        void * userdata
) {
    JD_Coroutine_Runner runner;
    runner.userdata = userdata;
    runner.num_used_buckets = 0;
    runner.buckets = 0;
    runner.num_available_buckets = 0;
    runner.coroutine_storage_watermark = 0;
    runner.coroutine_id_watermark = 0;

    runner.return_fiber = ConvertThreadToFiber(0);

    jd_coroutine_runner_push_bucket(&runner);

    return runner;
}

void jd_coroutine_runner_free(JD_Coroutine_Runner * runner) {
    for(int i = 0; i < runner->num_available_buckets; i++) {
        JD_COROUTINE_FREE(runner->userdata, runner->buckets[i]);
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
    assert(runner->return_fiber);

    JD_Coroutine * co = 0;

    for(int bucket_index = 0;
            bucket_index < runner->num_used_buckets;
            bucket_index++
    ) {
        JD_Coroutine * coroutines = runner->buckets[bucket_index];

		int max = bucket_index == runner->num_used_buckets - 1
			? runner->coroutine_storage_watermark
			: JD_COROUTINE_BUCKET_SIZE
		;
        for(int coroutine_index = 0;
                coroutine_index < max;
                coroutine_index++
        ) {
            JD_Coroutine * coroutine = coroutines + coroutine_index;

            if(coroutine && !coroutine->proc) {
                co = coroutine;
                goto found;
            }
        }
    }

    if(!co) {
        if(runner->coroutine_storage_watermark >= JD_COROUTINE_BUCKET_SIZE) {
            jd_coroutine_runner_push_bucket(runner);
            runner->coroutine_storage_watermark = 0;
        }

        co = runner->buckets[runner->num_used_buckets - 1] + runner->coroutine_storage_watermark;
        co->runner = runner;
        co->proc = 0;
        co->wait_time_seconds = 0;
        co->id = runner->coroutine_id_watermark++;
        co->fiber = CreateFiber(0, (LPFIBER_START_ROUTINE)jd_coroutine_entry, co);

        runner->coroutine_storage_watermark += 1;
    }

found:
    assert(co);
    assert(!co->proc);
    assert(co->runner);
    assert(co->fiber);

    co->wait_time_seconds = 0;
    co->proc = proc;
}

void jd_coroutine_yield(
        JD_Coroutine * co, 
        float wait_time_seconds
) {
    if(wait_time_seconds > 0) {
        co->wait_time_seconds += wait_time_seconds;
    }

    SwitchToFiber(co->runner->return_fiber);
}

void jd_coroutine_runner_tick(
        JD_Coroutine_Runner * runner,
        float delta_time
) {
    for(int bucket_index = 0;
            bucket_index < runner->num_used_buckets;
            bucket_index++
    ) {
        JD_Coroutine * coroutines = runner->buckets[bucket_index];

		int max = bucket_index == runner->num_used_buckets - 1
			? runner->coroutine_storage_watermark
			: JD_COROUTINE_BUCKET_SIZE
			;

        for(int coroutine_index = 0;
                coroutine_index < max;
                coroutine_index++
        ) {
            JD_Coroutine * coroutine = coroutines + coroutine_index;

            if(!coroutine || !coroutine->proc) {
                continue;
            }

            if(coroutine->wait_time_seconds > 0) {
				coroutine->wait_time_seconds -= delta_time;
                continue;
            }

            SwitchToFiber(coroutine->fiber);
        }
    }
}

#endif
