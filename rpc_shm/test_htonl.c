#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

// Command for compilation:
// - gcc -O3 -std=gnu99 -o ntoh_test test_htonl.c

inline unsigned long long cpu_tsc_start(void) {
    unsigned cyc_low, cyc_high;
    unsigned long long rval;
    asm volatile(
            "cpuid\n\t"
            "rdtsc\n\t"
            "movl %%edx, %0\n\t"
            "movl %%eax, %1\n\t" :
            "=r" (cyc_high), "=r" (cyc_low) ::
            "%rax", "%rbx", "%rcx", "%rdx" // clobbered registers
            );
    rval = ((unsigned long long) cyc_high << 32) | cyc_low;
    return rval;
}

inline unsigned long long cpu_tsc_end(void) {
    unsigned cyc_low, cyc_high;
    unsigned long long rval;
    asm volatile(
            "rdtscp\n\t"
            "movl %%edx, %0\n\t"
            "movl %%eax, %1\n\t"
            "cpuid\n\t": /* serialize to prevent code AFTER the call from MOVing in */
            "=r" (cyc_high), "=r" (cyc_low) ::
            "%rax", "%rbx", "%rcx", "%rdx" // clobbered registers
            );
    rval = ((unsigned long long) cyc_high << 32) | cyc_low;
    return rval;
}

int main(int argc, char** argv)
{
    int size = atoi(argv[1]);
    long long accum = 0;
    unsigned long long total_time = 0;
    int* arr = malloc( size * sizeof(int) );
    for(int i = size-1; i >=0; i-- ) {
        arr[i] = i;
    }

    unsigned long long start, end;
    // convert all vals to ntohl
    for(int i = 0; i < size; i++ ) {
        start = cpu_tsc_start();
        accum += htonl(arr[size-i] - arr[i]);
        end = cpu_tsc_end();
        total_time += (end-start);
    }
    accum = accum % 5;
    free(arr);

    printf("Num loop iterations: %d, total time (cpu ticks): %llu, time per htonl (ticks): %0.2f.\n"
            "Accum: %llu\n",size,total_time,(float)total_time / (float)size, accum);

    total_time += accum;
    total_time = accum << 10 & total_time>>10;
    return total_time;
}
