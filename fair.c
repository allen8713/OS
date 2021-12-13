#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

static inline int my_spin_lock (atomic_int *lock) {
    int val=0;
    if (likely(atomic_exchange_explicit(lock, 1, memory_order_acq_rel) == 0))
        return 0;
    do {
        do {
            asm("pause");
        } while (*lock != 0);
        val = 0;
    } while (!atomic_compare_exchange_weak_explicit(lock, &val, 1, memory_order_acq_rel, memory_order_relaxed));
    return 0;
}
static inline int my_spin_unlock(atomic_int *lock) {
    atomic_store_explicit(lock, 0, memory_order_release);
    return 0;
}

atomic_int a_lock;
atomic_long count_array[256];
int numCPU;

void sigHandler(int signo) {
    for (int i=0; i<numCPU; i++) {
        printf("%i, %ld\n", i, count_array[i]);
    }
    exit(0);
}

atomic_int in_cs=0;
atomic_int wait=1;

void thread(void *givenName) {
    int givenID = (intptr_t)givenName;
    srand((unsigned)time(NULL));
    unsigned int rand_seq;
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(givenID, &set);
    sched_setaffinity(gettid(), sizeof(set), &set);
    while(atomic_load_explicit(&wait, memory_order_acquire))
        ;
    while(1) {
        my_spin_lock(&a_lock);
        atomic_fetch_add(&in_cs, 1);
        atomic_fetch_add_explicit(&count_array[givenID], 1, memory_order_relaxed);
        if (in_cs != 1) {
            printf("violation: mutual exclusion\n");
            exit(0);
        }
        atomic_fetch_add(&in_cs, -1);
        my_spin_unlock(&a_lock);
        int delay_size = rand_r(&rand_seq)%73;
        for (int i=0; i<delay_size; i++)
            ;
    }
}

int main(int argc, char **argv) {
    signal(SIGALRM, sigHandler);
    alarm(5);
    numCPU = sysconf( _SC_NPROCESSORS_ONLN );
    pthread_t* tid = (pthread_t*)malloc(sizeof(pthread_t) * numCPU);

    for (long i=0; i< numCPU; i++)
        pthread_create(&tid[i],NULL,(void *) thread, (void*)i);
    atomic_store(&wait,0);

    for (int i=0; i< numCPU; i++)
        pthread_join(tid[i],NULL);
}
