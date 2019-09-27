#include <stdio.h>
#include <stdint.h>

#include <cstring>
#include <thread>
#include <assert.h>
#include <vector>

//#include "SDL2/SDL.h"

#define JD_COROUTINE_IMPL
#include "coroutine.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[i]))

double dt;
volatile bool should_quit = false;

void co_counter_quitter(JD_Coroutine * co) {
    for(int i = 2; i >= 0; i--) {
        printf("quitter: quitting in %d\n", i);
        jd_coroutine_yield(co, 1);
    }

    should_quit = true;
}

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

    for(int i = 21 ; i <= 30; i ++ ) {
        printf("counter: number %d\n", i);
        jd_coroutine_yield(co, .5);
    }

    jd_coroutine_begin(co->runner, co_counter_quitter);
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

void co_loading_bar(JD_Coroutine * co) {

    float time = 0;
    auto max_time = 1;
    auto loading_bar_length = 20;

    while(time <= max_time) {
        time += dt;
        auto t = (float)time / (float)max_time;
        if(t < 0) t = 0;
        if(t > 1) t = 1;

        printf("\r[");

        auto num_chars = (int)(t * (float)loading_bar_length);
        for(int i = 0; i < num_chars; i++) {
            printf("#");
        }

        for(int i = 0; i < loading_bar_length - num_chars; i++) {
            printf(".");
        }

        printf("] %.0f%%", t * 100);
        fflush(stdout);

        jd_coroutine_yield(co, 0);
    }
    printf("\n");

    jd_coroutine_begin(co->runner, co_counter);
    jd_coroutine_begin(co->runner, co_talker);
}

void co_stress(JD_Coroutine * co) {
    jd_coroutine_yield(co, 0);
}

int main() {
    printf("start\n");

    auto runner = jd_coroutine_runner_init(0, 0);
    jd_coroutine_begin(&runner, co_loading_bar);

    double target_delta = 1.0/60.0;
    dt = target_delta;

    double seconds_accum = 0;
    int num_entries = 0;

    int num_frames_remaining = 20000;
    double dt_avg = 0;
    int dt_num = 0;
    while(!should_quit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        auto startT = std::chrono::high_resolution_clock::now();

        {
          //  auto start = SDL_GetPerformanceCounter();

            jd_coroutine_runner_tick(&runner, dt);

         //   auto delta = SDL_GetPerformanceCounter() - start;
         //   seconds_accum += (double)delta / (double)SDL_GetPerformanceFrequency();
         //   num_entries += 1;
        }

        {
            auto delta = std::chrono::duration<float>(
                    std::chrono::high_resolution_clock::now() - startT
            ).count();

            if(target_delta > delta) {
                auto wait_time = target_delta - delta;
                std::this_thread::sleep_for(std::chrono::milliseconds((int)(wait_time * 1000)));
                dt = target_delta;
            }
            else {
                dt = delta;
            }

            dt_avg += dt;
            dt_num += 1;
        }
    }

    auto tick_time = seconds_accum / (double)num_entries * 1000.0f;
    //auto frame_time = dt_avg / (double)dt_num * 1000.0f;
    auto frame_time = target_delta  * 1000.0f;
    auto percentage = tick_time / frame_time;
    printf("Performance measurement:\n");
    printf("\tFrame time is %fms\n", frame_time);
    printf("\tjd_coroutine_runner_tick average time: %fms\n", tick_time);
    printf("jd_coroutine_runner_tick takes up %f%% of the frame time\n", percentage * 100);

    jd_coroutine_runner_free(&runner);

	return 0;
}
