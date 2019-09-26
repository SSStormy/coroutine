#include <stdio.h>
#include <stdint.h>
#include "windows.h"
#include <thread>
#include <assert.h>
#include <vector>

#define JD_COROUTINE_IMPL
#include "coroutine.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[i]))

void co_counter_spinoff(JD_Coroutine * co) {
    printf("co_counter_spinoff: %d\n", co->id);

    printf("counter: started a coroutine in the middle of another coroutine...\n");
    jd_coroutine_yield(co, 1);
    printf("counter: ...and it yielded once\n");
    jd_coroutine_yield(co, 1);
    printf("counter: ...and it yielded twice\n");
}

void co_counter_follow_up(JD_Coroutine * co) {
    printf("co_counter_follow_up: %d\n", co->id);

    printf("counter: started a new coroutine\n");

    for(int i = 21 ; i <= 40; i ++ ) {
        printf("counter: number %d\n", i);
        jd_coroutine_yield(co, .5);
    }
}

void co_counter(JD_Coroutine * co) {
    printf("co_counter: %d\n", co->id);

    for(int i = 0 ; i <= 10; i ++ ) {
        printf("counter: number %d\n", i);
        jd_coroutine_yield(co, 0);
    }

    jd_coroutine_begin(co->runner, co_counter_spinoff);

    for(int i = 11 ; i <= 20; i ++ ) {
        printf("counter: number %d\n", i);

        jd_coroutine_yield(co, 1);
    }

    jd_coroutine_begin(co->runner, co_counter_follow_up);
}

void co_talker_follow_up(JD_Coroutine * co) {
    printf("co_talker_follow_up: %d\n", co->id);

    const char * words[] = {"The", "Witness", "is", "a", "firs", "person", "puzzle", "video", 
        "game.", "The", "playe", ", as", "an", "unnamed", "characte", ", explores", "an", 
        "island", "with", "numerous", "structures", "and", "natural", "formations", "The", 
        "island", "is", "roughly", "divided", "into", "eleven", "regions", "arranged", 
        "around", "a", "mountain", "that", "represents", "the", "ultimate", "goal", "for", 
        "the", "player."
    };

    for(int i = 0; i < ARRAY_SIZE(words); i++) {
        auto word = words[i];

        printf("talker follow up: %s\n", word);

        jd_coroutine_yield(co, .1f);
    }
}

void co_talker(JD_Coroutine * co) {
    printf("co_talker: %d\n", co->id);

    const char * words[] = {
        "The", "spectators", "of", "a", "well", "written", "tragedy", "get", "from", 
        "it", "sorrow", "terror", "anxiety", "and", "other", "emotions", "that", 
        "are", "in", "themselves", "disagreeable", "and", "uncomfortable", "and", "they", 
        "get", "pleasure", "from", "this!", "It's", "hard", "to", "understand."
    };

    for(int i = 0; i < ARRAY_SIZE(words); i++) {
        auto word = words[i];

        printf("talker: %s\n", word);

        jd_coroutine_yield(co, .2f);
    }

    jd_coroutine_begin(co->runner, co_talker_follow_up);
}

int main() {

    auto runner = jd_coroutine_runner_init(0);
    
    jd_coroutine_begin(&runner, co_counter);
    jd_coroutine_begin(&runner, co_talker);

	int dt_ms = 16;
    float dt = (float)dt_ms/1000.0f;
    while(true) {

       // printf("...pretending we're running a frame...\n");

        jd_coroutine_runner_tick(&runner, dt);

        std::this_thread::sleep_for(std::chrono::milliseconds(dt_ms));
    }

	return 0;
}
