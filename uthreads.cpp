/*
 * User-Level Threads Library (uthreads)
 * Hebrew University OS course.
 * Author: neriya cohen, neriya.cohen1@cs.huji.ac.il
 */

#ifndef _UTHREADS_H
#define _UTHREADS_H

#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <stack>
#include <vector>
#include <algorithm>
#include <iostream>


#define MAX_THREAD_NUM 100 /* maximal number of threads */
#define STACK_SIZE 4096 /* stack size per thread (in bytes) */
#define RETURENED 1 /* signal that this thread had been jumped into right now*/
#define DOESNT_EXIST -1 /* value/sign that tid doesn't exist*/
#define REGULAR_BLOCKED -2 /* value/sign that tid doesn't exist*/
#define FAIL -1
#define SUCCESS 0

using namespace std;

vector<int> ready; /* ready queue fifo  */
int blocked[MAX_THREAD_NUM]; /* blocked array that both contains memory of blocked threads and time remaining to sleep */
bool sleeping_is_blocked[MAX_THREAD_NUM]; /* counts sleeping time remaining. 0 means continue to  */

typedef unsigned long address_t;
typedef void (*thread_entry_point)(void);

char* stacks[MAX_THREAD_NUM]; //MAX_THREAD_NUM pointers
int threads_quantum_counter[MAX_THREAD_NUM];
int current_tid = -1; // the current current_tid thread
int terminate_calling_process = 0;

sigjmp_buf env[MAX_THREAD_NUM]; /* saves the states of env (sp,pc, registers) */

unsigned long lib_quantum_counter = 0;

struct itimerval timer;
struct itimerval zero_timer;

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6z
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}
#endif

int lib_err(string msg);
void sys_err(string msg);
void blocked_to_ready(int tid);
void jump_to_thread(int tid);
void setup_thread(int tid, thread_entry_point entry_point);
int initiate_timer(int quantum_usecs);
void set_timer(int quantum_usecs);
void stop_clock();
void increase_counters();
int get_next_thread_id();
void scheduling_process();
void update_sleepers();
void wake_process_up(int key);
void reset_non_alloc_fields(int tid);
void free_n_reset_alloc_fields(int tid);
int get_n_pop_next_running();
void terminateall();
void end_of_quantum(int sig);
//// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~     const end      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~



/**
 * @brief initializes the thread library.
 *
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs){
    if (quantum_usecs < 1) return lib_err("init can't get non-positive quantum_usecs\n");
    for (int i = 0; i<MAX_THREAD_NUM; i++){
        sleeping_is_blocked[i] = false;
        threads_quantum_counter[i]=DOESNT_EXIST;
        stacks[i]=nullptr;
        blocked[i]=DOESNT_EXIST;
    }

    initiate_timer(quantum_usecs);

    // initiates thread 0 as the curr running process
    current_tid = 0;
    threads_quantum_counter[current_tid] = 0;

    increase_counters();
    return SUCCESS;
}
/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(thread_entry_point entry_point){
    if(entry_point == nullptr) return lib_err("spawn can't get null entry point\n"); // sanity
    int next_tid = get_next_thread_id();
    if (next_tid == DOESNT_EXIST) return lib_err("reached maximum possible threads\n"); // sanity
    setup_thread(next_tid,entry_point);
    ready.insert(ready.begin(),next_tid);
    return next_tid;
}
/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate(int tid){
    if(tid < 0 || tid >= MAX_THREAD_NUM || DOESNT_EXIST == threads_quantum_counter[tid])
    {return lib_err("terminate on id that doesn't exists it illegal\n");} // sanity

    if (tid != 0){
        if (current_tid == tid) {
            reset_non_alloc_fields(tid);
            free_n_reset_alloc_fields(tid);
            scheduling_process();
            return 0;
        } // scheduling_process will terminate the process after switching out of it
        reset_non_alloc_fields(tid);
        free_n_reset_alloc_fields(tid);
        return 0;
    }

    stop_clock();
    terminateall();
    exit(0);
}
/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid){
    // sanity checks
    if (tid==0) return lib_err("blocking thread 0 is illegal\n");
    if (1>tid || tid >= MAX_THREAD_NUM || threads_quantum_counter[tid]==-1)
        return lib_err("blocking a thread that doesnt exist is illegal\n");
    // do nothing check
    if(blocked[tid]!=DOESNT_EXIST) return 0; // if already blocked

    if(blocked[tid]!= REGULAR_BLOCKED and blocked[tid]!=DOESNT_EXIST) sleeping_is_blocked[tid] = true; // if process is asleep - make is blocked as well

    else blocked[tid] = REGULAR_BLOCKED; // block regularly if its legit to block it, as well as not asleep

    ready.erase(std::remove(ready.begin(), ready.end(), tid), ready.end()); // remove from ready if it's there

    if (tid == current_tid){
        scheduling_process(); // next ready last element(=first in) -> running
    }
    return SUCCESS;
}
/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid){
    // sanity check
    if (1 > tid || MAX_THREAD_NUM <= tid || threads_quantum_counter[tid]==DOESNT_EXIST){
        return -1;
    }
    if (blocked[tid]) {
        if (sleeping_is_blocked[tid] != 0){ blocked[tid] = sleeping_is_blocked[tid]; sleeping_is_blocked[tid] = 0;} // resume sleeping
        else blocked_to_ready(tid);
    } // if it exists and block - move it to ready queue. else don't touch
    return 0;
}
/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY threads list.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid==0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums){
    // sanity check
    if(num_quantums<1 || current_tid<0 || current_tid>=MAX_THREAD_NUM || threads_quantum_counter[current_tid]==DOESNT_EXIST)
        return lib_err("sleep time must be > 0, and on existing threads\n");
    if(current_tid == 0) return lib_err("putting thread 0 to sleep is illegal\n");
    blocked[current_tid]=num_quantums; // ex: sleep(3) -> q1, sleep(2) -> q2, sleep(1) -> -> q3, sleep(0) -> q4, sleep(-1) move to ready at start of q4
    scheduling_process();
    return 0;
}
/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid(){
    return current_tid;
}
/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums(){
    return lib_quantum_counter;
}
/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid){
    if (tid>=MAX_THREAD_NUM) return lib_err("thread id must be within "+ to_string(MAX_THREAD_NUM) + "\n");
    if (tid<0) return lib_err("thread id must be higher -1\n");
    return threads_quantum_counter[tid];
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~       helper/cleaner code function       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/**
 * init timer ti quantom_usecs
 */
int initiate_timer(int quantum_usecs){

    struct sigaction sa = {0};

    zero_timer.it_value.tv_usec = 0;
    zero_timer.it_value.tv_sec = 0;

    zero_timer.it_interval.tv_usec = 0;
    zero_timer.it_interval.tv_sec = 0;

    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &end_of_quantum;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        sys_err("init failed to mask SIGVTALRM\n");
    }

    timer.it_interval.tv_sec = quantum_usecs/((int)1e6); // following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usecs%((int)1e6); // following time intervals, microseconds part

    timer.it_value.tv_sec = quantum_usecs/((int)1e6); // following time intervals, seconds part
    timer.it_value.tv_usec = quantum_usecs%((int)1e6); // following time intervals, microseconds part

    return 0;
}
/**
 * move from ready to running.
 * stop timer, move to sleep/ready/waiting by index?, restart timer, update current_process = ready.pop()
 */
void scheduling_process(){
    update_sleepers();
    int ret_val = sigsetjmp(env[current_tid], 1);
    bool did_just_save = ret_val == 0;
    if (did_just_save)
    {
        jump_to_thread(get_n_pop_next_running());
        if(terminate_calling_process){ // if terminate_calling_process != 0, it'll be a number of the tread to terminate
            uthread_terminate(terminate_calling_process); // terminate the calling thread.
            terminate_calling_process = 0;
        }
    }
    increase_counters();
}
/**
 * remove 1 quantum from each sleeping process.
 * if process reamaining sleeping time is 0 or less, wake him up.
 */
void update_sleepers(){
    for (int i = 1; i <MAX_THREAD_NUM; i++) {
        if(blocked[i] == REGULAR_BLOCKED || blocked[i] == DOESNT_EXIST){continue;} // not a sleeper
        if(blocked[i] == 0){ // slept its due
            if(sleeping_is_blocked[i]) { // if its blocked
                blocked[i] = REGULAR_BLOCKED;
                sleeping_is_blocked[i] = false; // not sleeping anymore
            }
            else blocked_to_ready(i);} // a sleeper that waited it's time
        else {blocked[i]--;} // you waited another quantum, here's your prize: waiting time -= 1
    }
}
/**
 * move a locked thread to ready 'queue'
 * @param tid
 */
void blocked_to_ready(int tid){
    blocked[tid] = DOESNT_EXIST;
    ready.insert(ready.begin(),tid);
}
/**
 *
 * @return next empty number as tid, or -1 if there is none.
 */
int get_next_thread_id(){
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if(threads_quantum_counter[i] == DOESNT_EXIST){
            return i;
        }
    }
    return FAIL;
}
/**
 * set the timer to run by c_timer values
 * @param c_timer
 */
void set_timer(itimerval c_timer){
    if (setitimer(ITIMER_VIRTUAL, &c_timer, NULL))
    {
        sys_err("setitimer error\n");
    }
}
/**
 * stops the VIRTUAL TIMER
 */
void stop_clock(){
    set_timer(zero_timer);
}
/**
 * restart the VIRTUAL TIMER
 */
void increase_counters(){
    set_timer(timer);
    if (threads_quantum_counter[current_tid]!=DOESNT_EXIST) threads_quantum_counter[current_tid]++; // in case iof terminating, tT.Q.C[tid] is restored to -1 as it had been before it was born, but it calls
    lib_quantum_counter++;
}
/**
 * @brief jump to thread
 */
void jump_to_thread(int tid)
{
    current_tid = tid;
    siglongjmp(env[tid], RETURENED);
}
/**
 * should be called only if stacks and env of tid will be freed - or else.
 * @param tid
 */
void reset_non_alloc_fields(int tid){
    threads_quantum_counter[tid] = DOESNT_EXIST;
    blocked[tid] = DOESNT_EXIST;
    sleeping_is_blocked[tid] = 0;
    ready.erase(std::remove(ready.begin(), ready.end(), tid), ready.end());
}
/**
 * as name implies
 * @param tid
 */
void free_n_reset_alloc_fields(int tid){
    delete stacks[tid];
    stacks[tid] = nullptr;
}
/**
 * allocates a new heap of the head of the process
 * update the environment of tid thread of the allocated memory(sp, pc) and make the mask list empty
 * @param tid
 * @param entry_point
 */
void setup_thread(int tid, thread_entry_point entry_point){

    stacks[tid] = new char[STACK_SIZE];
    address_t sp = (address_t) stacks[tid] + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) entry_point;
    sigsetjmp(env[tid],1);
    (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
    (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
    threads_quantum_counter[tid]= 0;
    sigemptyset(&env[tid]->__saved_mask);

}
/**
 * as the name implies
 * @return
 */
int get_n_pop_next_running() {
    int ntid = ready.back();
    ready.pop_back();
    return ntid;
}
/**
 * terminate every non zero process
 */
void terminateall() {
    stop_clock();
    for (int i = 1; i<MAX_THREAD_NUM; i++){
        if(threads_quantum_counter[i]!= DOESNT_EXIST) uthread_terminate(i);
    }
}
/**
 * short: runs when quantum ends.
 * shortless: enter current to ready(method:fifo), make read.back() the running process
 * @return
 */
void end_of_quantum(int sig){
    ready.insert(ready.begin(),current_tid);
    scheduling_process();
}
/**
 *
 */
int lib_err(string msg){
    std::cerr<<"thread library err: " << msg;
    return FAIL;
}
/**
 *
 */
void sys_err(string msg){
    std::cerr<<"system err: " << msg;
    exit(1);
}
#endif