#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <math.h>
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

static inline int my_spin_lock (atomic_int *lock) {
    int val=0;
    if (likely(atomic_exchange_explicit(lock, 1, memory_order_acq_rel) == 0))//assume lock successfully and lock it if the value is 0 return 0 if the value isn't 0 will return 1 and enter spinlock
        return 0;
    do {
        do {
            asm("pause");//nop to lesson the burden of cpu
        } while (*lock != 0);//read the value of lock directly, assuming there is memory coherence to make thread the right value
        val = 0;//if lock=0 set val to 0
    } while (!atomic_compare_exchange_weak_explicit(lock, &val, 1, memory_order_acq_rel, memory_order_relaxed));//one of threads head here will successfully lock and the others keep spinning
    //once inside the loop weak atomic will also be correct, acr_rel let the order of operatons cannot be changed
    return 0;

}
static inline int my_spin_unlock(atomic_int *lock) {
    atomic_store_explicit(lock, 0, memory_order_release);//set lock as 0 and send message of lock out
    //release makes the operations besides cannot move down
    return 0;
}
atomic_int a_lock;
atomic_long count_array[256];
int numCPU;
//five seconds later print the time of every cpus enter critical section and quit
void sigHandler(int signo) {
    int sum = 0;
    for (int i=0; i<numCPU; i++) {
        printf("%i, %ld\n", i, count_array[i]);
        sum += count_array;
    }
    double avg = (double)sum / numCPU;
    double standard = 0.0;
    for(int i = 0; i < numCPU; i++){
        double num = (double)count_array[i] - avg;
        num *= num;
        standard += num;
    }
    standard /= numCPU;
    standard = sqrt(standard);
    printf("standard = %f\n",standard);
    exit(0);
}

atomic_int in_cs=0;
atomic_int wait=1;

void thread(void *givenName) {
    int givenID = (intptr_t)givenName;//prevent warning change type
    srand((unsigned)time(NULL));//setting the seed of random function
    unsigned int rand_seq;
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(givenID, &set);//let nth thread on nth cpu core
    sched_setaffinity(gettid(), sizeof(set), &set);
    while(atomic_load_explicit(&wait, memory_order_acquire))//after generating every threads set wait as 0, let every threads execute almost simultaneously
        ;
    while(1) {
        my_spin_lock(&a_lock);//lock critical section
        atomic_fetch_add(&in_cs, 1);//when entering add in_cs as 1
        atomic_fetch_add_explicit(&count_array[givenID], 1, memory_order_relaxed);//add 1 to nth cpu enter time
        if (in_cs != 1) {//if in_cs > 1 means morethan one process in critical section, end the program
            printf("violation: mutual exclusion\n");
            exit(0);
        }
        atomic_fetch_add(&in_cs, -1);//minus one of in_cs before leaving
        my_spin_unlock(&a_lock);//unlock critical section
        int delay_size = rand_r(&rand_seq)%73;//wait for a random time
        for (int i=0; i<delay_size; i++)//use a for loop to make delay
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
