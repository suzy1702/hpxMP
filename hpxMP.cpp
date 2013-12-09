//  Copyright (c) 2013 Jeremy Kemp
//  Copyright (c) 2013 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "hpx_runtime.h"
#include "hpxMP.h"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>

using namespace std;
hpx_runtime hpx_backend;

//int num_threads = 0;
bool started = false;
int single_counter = 0;
int current_single_thread = -1;
int single_mtx_id = -1;

omp_micro thread_func = 0;

int init_num_threads() {
    int numThreads = 0;
    auto envNum = getenv("OMP_NUM_THREADS");
    if( envNum != 0)
       numThreads = atoi(envNum);
    return numThreads;
}

void omp_thread_func(void *firstprivates, void *fp) {
    int tid = __ompc_get_local_thread_num();
    thread_func(tid, fp);
}

void __ompc_fork(int Nthreads, omp_micro micro_task, frame_pointer_t fp) {
    hpx_backend.init(init_num_threads());
    if(single_mtx_id == -1) 
        single_mtx_id = hpx_backend.new_mtx();
    if(Nthreads <= 0)
        Nthreads = hpx_backend.get_num_threads();
    thread_func = micro_task;
    assert(!started);
    started = true;
    hpx_backend.fork(Nthreads, omp_thread_func, fp);
    started = false;
}

int __ompc_can_fork() {
    return !started;
}
int __ompc_get_local_thread_num() {
    return hpx_backend.get_thread_num();
}


void __ompc_static_init_4( int global_tid, omp_sched_t schedtype,
                           int *p_lower, int *p_upper, 
                           int *p_stride, int incr, 
                           int chunk) {
    int thread_num = __ompc_get_local_thread_num();
    int size;
    int *tmp;
    int num_threads = __ompc_get_num_threads();
    if(*p_upper < *p_lower) {
        tmp = p_upper;
        p_upper = p_lower;
        p_lower = tmp;
    }
    size = *p_upper - *p_lower + 1;
    int chunk_size = size/num_threads;
    if(thread_num < size % num_threads) {
        *p_lower += thread_num * (chunk_size+incr);
        *p_upper = *p_lower + chunk_size ;
    } else {
        *p_lower += (size % num_threads) * (chunk_size+incr) + (thread_num - size % num_threads ) * chunk_size;
        *p_upper = *p_lower + chunk_size - incr;
    }
}

void __ompc_static_init_8( omp_int32 global_tid, omp_sched_t schedtype,
                      omp_int64 *p_lower, omp_int64 *p_upper, omp_int64 *p_stride,
                      omp_int64 incr, omp_int64 chunk ){
    omp_int64 thread_num = __ompc_get_local_thread_num();
    omp_int64 size;
    omp_int64 *tmp;
    int num_threads = __ompc_get_num_threads();
    if(*p_upper < *p_lower) {
        tmp = p_upper;
        p_upper = p_lower;
        p_lower = tmp;
    }
    size = *p_upper - *p_lower + 1;
    int chunk_size = size/num_threads;
    if(thread_num < size % num_threads) {
        *p_lower += thread_num * (chunk_size+incr);
        *p_upper = *p_lower + chunk_size ;
    } else {
        *p_lower += (size % num_threads) * (chunk_size+incr) + (thread_num - size % num_threads ) * chunk_size;
        *p_upper = *p_lower + chunk_size - incr;
    }
}

void __ompc_barrier() {
    __ompc_ebarrier();
}

void __ompc_ebarrier() {
    hpx_backend.barrier_wait();
}

int __ompc_get_num_threads(){
    return hpx_backend.get_num_threads();
}

int __ompc_master(int global_tid){
    if(__ompc_get_local_thread_num() == 0) 
        return 1;
    return 0;
}

void __ompc_end_master(int global_tid){
}

void* single_worker(int tid) {
    int num_threads = __ompc_get_local_thread_num();
    if(current_single_thread == -1 && single_counter == 0) {
        current_single_thread = tid;
        single_counter = 1 - num_threads;
    } else {
        single_counter++;
    }
    return (void*)(current_single_thread == tid);
}

int __ompc_single(int global_tid){
    bool is_first = hpx_backend.run_mtx(single_worker, single_mtx_id);
    if(is_first) {
        return 1;
    }
    return 0;
    /*int tid = __ompc_get_local_thread_num();
    mutex_type::scoped_lock l(single_mtx);
    if(current_single_thread == -1 && single_counter == 0) {
        current_single_thread = tid;
        single_counter = 1 - num_threads;
    } else {
        single_counter++;
    }
    if(current_single_thread == tid) 
        return 1;
    return 0;*/
}

void* end_single_worker(int tid) {
    if(single_counter == 0) {
        current_single_thread = -1;
    }
    return (void*)(0);
}

void __ompc_end_single(int global_tid){
    hpx_backend.run_mtx(end_single_worker, single_mtx_id);
    /*
    mutex_type::scoped_lock l(single_mtx);
    if(single_counter == 0) {
        current_single_thread = -1;
    }*/
}

int __ompc_task_will_defer(int may_delay){
    //in the OpenUH runtime, this also checks if a task limit has been reached
    //leaving that to hpx to decide
    return may_delay;
}
void __ompc_task_firstprivates_alloc(void **firstprivates, int size){
    *firstprivates = malloc(size);
}

void __ompc_task_firstprivates_free(void *firstprivates){
    free(firstprivates);
}
/*
void task_setup(omp_task_func task_func, int thread_num, void *firstprivates, void *fp) {
    thread_data *data_struct = new thread_data;
    data_struct->thread_num = thread_num;
    auto thread_id = hpx::threads::get_self_id();
    hpx::threads::set_thread_data( thread_id, reinterpret_cast<size_t>(data_struct));
    task_func(firstprivates, fp);
}

void __ompc_task_create( omp_task_func taskfunc, void *frame_pointer,
                         void *firstprivates, int may_delay,
                         int is_tied, int blocks_parent) {

    auto *data = reinterpret_cast<thread_data*>(
                hpx::threads::get_thread_data(hpx::threads::get_self_id()));
    int current_tid = data->thread_num;
    data->task_handles.push_back(hpx::async(task_setup, taskfunc, current_tid, firstprivates, frame_pointer));
}

void __ompc_task_wait(){
    auto *data = reinterpret_cast<thread_data*>(
                hpx::threads::get_thread_data(hpx::threads::get_self_id()));
    hpx::wait(data->task_handles); 
    data->task_handles.clear();
}

void __ompc_task_exit(){
}

void __ompc_serialized_parallel(int global_tid) {
    //It appears this function does nothing
}
void __ompc_end_serialized_parallel(int global_tid) {
    //It appears this function does nothing
}

//OMP Library functions
//TODO: move to another file
int omp_get_num_threads() {
    return num_threads;
}

int omp_get_max_threads() {
    return num_threads;
}

int omp_get_thread_num() {
    return __ompc_get_local_thread_num();
}
*/

