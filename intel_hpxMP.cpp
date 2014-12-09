#include "intel_hpxMP.h"
#include "loop_schedule.h"
#include <boost/shared_ptr.hpp>
#include <iostream>
#include <assert.h>

using std::cout;
using std::endl;

boost::shared_ptr<hpx_runtime> hpx_backend;

//typedef void (*omp_micro)(int , frame_pointer_t);
//typedef void (*microtask_t)( int *gtid, int *npr, ... );
//typedef void (*kmpc_micro)  ( kmp_int32 * global_tid, kmp_int32 * bound_tid, ... );

bool in_parallel = false;

struct task_args {
    kmpc_micro fork_func;
    int argc;
    void **argv;
};

void start_backend(){
    if( !hpx::get_runtime_ptr()){
        if(!hpx_backend) {//why wasn't this here before?
            hpx_backend.reset(new hpx_runtime());
        }
    }
}

int __kmpc_ok_to_fork(ident_t *loc){
    return 1;
}

void __kmpc_begin( ident_t *, kmp_int32 flags ){
    start_backend();
}

void __kmpc_end(ident_t *loc){
}

void omp_thread_func(int tid, void *fp) {
    //cout <<"thread function being called, tid = " << tid << endl;
    task_args *args = (task_args*)fp;
    void **argv = args->argv;
    int argc = args->argc - 1;

    assert(argc < 2);
    switch(argc) {
        case 0: args->fork_func(&tid, &tid);
                break;
        case 1: args->fork_func(&tid, &tid, argv[0]);
                break;
        default:
                args->fork_func(&tid, &tid);
    }
    //cout <<"thread function returning, tid = " << tid << endl;
}

void
__kmpc_fork_call(ident_t *loc, kmp_int32 argc, kmpc_micro microtask, ...)
{
    //cout << "entering the fork\n";
    task_args args;
    if(!hpx_backend) {
        start_backend();
    }

    void *argv[2] = {0,0};

    assert(argc < 3);

    va_list     ap;
    va_start(   ap, microtask );

    for( int i = 0; i < argc; i++ ){
        argv[i] = va_arg( ap, void * );
    }
    va_end( ap );
    args.argc = argc;
    args.argv = argv;

    args.fork_func = microtask;


    if( hpx::threads::get_self_ptr() ) {
        hpx_backend->fork(0, omp_thread_func, (void*)&args);
    } else {
        in_parallel = true;
        hpx_backend->fork(0, omp_thread_func, (void*)&args);
        in_parallel = false;
    }
    //cout << "leaving main fork" << endl;
}

// ----- Tasks -----
//sizeof_kmp_task_t includes the private variables for the task
kmp_task_t*
__kmpc_omp_task_alloc( ident_t *loc_ref, kmp_int32 gtid, kmp_int32 flags,
                       size_t sizeof_kmp_task_t, size_t sizeof_shareds,
                       kmp_routine_entry_t task_entry ){

    //kmp_tasking_flags_t *input_flags = (kmp_tasking_flags_t *) & flags;
    kmp_task_t *task = (kmp_task_t*)new char[sizeof_kmp_task_t + sizeof_shareds]; 
    //This gets deleted at the end of intel_task_setup
    task->routine = task_entry;
    task->shareds = (char*)task + sizeof_kmp_task_t;
    task->part_id = 0;

    return task;
}

int __kmpc_omp_task( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * new_task){
    hpx_backend->create_intel_task(new_task->routine, gtid, new_task);
    return 1;
}

//    TASK_CURRENT_NOT_QUEUED (0) if did not suspend and queue current task to be resumed later.
//    TASK_CURRENT_QUEUED (1) if suspended and queued the current task to be resumed later.
kmp_int32 
__kmpc_omp_task_with_deps( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * new_task,
                           kmp_int32 ndeps, kmp_depend_info_t *dep_list,
                           kmp_int32 ndeps_noalias, kmp_depend_info_t *noalias_dep_list ){
    //see__kmp_invoke_task
    //new_task->routine(gtid, new_task);
    //TODO:how to I handle immediate tasks?
    hpx_backend->create_intel_task(new_task->routine, gtid, new_task);
    return 1;
}

kmp_int32 __kmpc_omp_taskwait( ident_t *loc_ref, kmp_int32 gtid ){
    hpx_backend->task_wait();
    return 0;
}

void
__kmpc_omp_wait_deps( ident_t *loc_ref, kmp_int32 gtid, kmp_int32 ndeps, 
                      kmp_depend_info_t *dep_list, kmp_int32 ndeps_noalias, 
                      kmp_depend_info_t *noalias_dep_list ){
    //Not handling dependencies at the moment
    if(ndeps > 0 || ndeps_noalias > 0){
        cout <<"task has more than 0 dependencies. This runtime does not support task dependencies\n";
        assert(ndeps == 0);
        assert(ndeps_noalias == 0);
    }
}

void __kmpc_omp_task_begin_if0( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * task ){
    task->routine(gtid, task);
}
void
__kmpc_omp_task_complete_if0( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t *task) {
    //This pairs up with task_begin_if0, waiting for the task that if0 starts.
    //For now, I am just going to execute the thread in task begin, and do nothing here.
}

// ----- End Tasks -----

void
__kmpc_for_static_init_4( ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype, kmp_int32 *plastiter,
                          kmp_int32 *plower, kmp_int32 *pupper,
                          kmp_int32 *pstride, kmp_int32 incr, kmp_int32 chunk ){
    __ompc_static_init_4(gtid, (omp_sched_t)2, plower, pupper, pstride, incr, chunk);
}

void
__kmpc_for_static_init_4u( ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype, kmp_int32 *plastiter,
                          uint32_t *plower, uint32_t *pupper,
                          kmp_int32 *pstride, kmp_int32 incr, kmp_int32 chunk ){
    kmp_static_init_4u(gtid, (omp_sched_t)2, plower, pupper, pstride, incr, chunk);
}

void
__kmpc_for_static_fini( ident_t *loc, kmp_int32 global_tid ){
    //Only seems to do internal tracking in intel runtime
}

void 
__kmpc_push_num_threads( ident_t *loc, 
                         kmp_int32 global_tid, 
                         kmp_int32 num_threads ){
    //TODO: this needs to be local to the task
    //hpx_backend->set_num_threads(num_threads);
    omp_task_data *data = hpx_backend->get_task_data();
    data->set_threads_requested( num_threads );
}

void
__kmpc_barrier(ident_t *loc, kmp_int32 global_tid) {
    hpx_backend->barrier_wait();
}

int  __kmpc_cancel_barrier(ident_t* loc_ref, kmp_int32 gtid){
    hpx_backend->barrier_wait();
    return 0;
}

int __kmpc_global_thread_num(ident_t *loc){
    //cout << "global_thread_num not implemented\n";
    //return 0;
    if(hpx_backend)
        return hpx_backend->get_thread_num();
    return 0;
}

int __kmpc_single(ident_t *loc, int tid){
    if(!in_parallel)
        return 1;
    parallel_region *team = hpx_backend->get_team();
    int num_threads = hpx_backend->get_num_threads();

    team->single_mtx.lock();
    if(team->current_single_thread == -1 && team->single_counter == 0) {
        team->current_single_thread = tid;
        team->single_counter = 1 - num_threads;
    } else {
        team->single_counter++;
    }
    team->single_mtx.unlock();
    if(team->current_single_thread == tid) {
        return 1;
    }
    return 0;
}

//in intel, only the single thread calls this
void __kmpc_end_single(ident_t *loc, int tid){
    if(!in_parallel)
        return;
    parallel_region *team = hpx_backend->get_team();
    team->single_mtx.lock();
    if(team->single_counter == 0) {
        team->current_single_thread = -1;
    }
    team->single_mtx.unlock();
}

int __kmpc_master(ident_t *loc, int global_tid){
    //TODO: if master can be called from tasks, then this doesn't work.
    if(hpx_backend->get_thread_num() == 0) {
        return 1;
    }
    return 0;
}

void __kmpc_end_master(ident_t *loc, int global_tid){
}

void
__kmpc_critical( ident_t * loc, kmp_int32 global_tid, kmp_critical_name * crit ) {
    parallel_region *team = hpx_backend->get_team();
    team->crit_mtx.lock();
    //hpx_backend->crit_mtx.lock();
}

void
__kmpc_end_critical(ident_t *loc, kmp_int32 global_tid, kmp_critical_name *crit) {
    parallel_region *team = hpx_backend->get_team();
    team->crit_mtx.unlock();
    //hpx_backend->crit_mtx.unlock();
}

void __kmpc_flush(ident_t *loc, ...){
    __sync_synchronize();
}

//I think I need to pair up *data to with the memory allocated to represend the threadlocal version
void* __kmpc_threadprivate_cached( ident_t *loc, kmp_int32 tid, void *data, size_t size, void ***cache){
    if(!hpx_backend) {
        start_backend();
    }
    parallel_region *team = hpx_backend->get_team();
    int num_threads = team->num_threads;
    if(!(*cache)){
        team->thread_mtx.lock();
        if(!(*cache)){
            *cache = new void*[num_threads]{0};//FIXME: this never gets deallocated
        }
        team->thread_mtx.unlock();
    }
    //if(tid == 0) {
    //    return data;
    //}
    if( !((*cache)[tid]) ) {
        (*cache)[tid] = new char[size];
        std::memcpy((*cache)[tid], data, size);
    }
    return (*cache)[tid];
}

//Only one of the threads (called the single thread) should have the didit variable set to 1
//This function copies the copyprivate variable of the task that got ran the single
// into the other implicit tasks, at the end of the single region
// it also needs to cause other implicit tasks to wait until the single task is complete
void
__kmpc_copyprivate( ident_t *loc, kmp_int32 gtid, size_t cpy_size, void *cpy_data, void(*cpy_func)(void*,void*), kmp_int32 didit )
{
    void **data_ptr = &(hpx_backend->get_team()->copyprivate_data);
    //what is t.t_copypriv_data? What is it if there are more than 1 variables copied?
    //data_ptr = & __kmp_team_from_gtid( gtid )->t.t_copypriv_data;
    if(didit) {
        *data_ptr = cpy_data;
    }
    hpx_backend->barrier_wait();
    cpy_func(cpy_data, *data_ptr);
    hpx_backend->barrier_wait();
}

//Library functions:--------------------------------------------------
int omp_get_thread_num(){
    if(hpx_backend)
        return hpx_backend->get_thread_num();
    else
        return 1;
}

int omp_get_num_threads(){
    if(in_parallel){
        return hpx_backend->get_team()->num_threads;
        //return hpx_backend->get_task_data()->nthreads_var;//not sure why this was added
    } else {
        return 1;
    }
}

int omp_get_max_threads() {
    if(!hpx_backend) {
        start_backend();
    }                      
    return hpx_backend->get_num_threads();
}

int omp_get_num_procs(){
    return hpx_backend->get_num_procs();
}

double omp_get_wtime(){
    if(!hpx_backend)
        start_backend();
    return hpx_backend->get_time();
}

double omp_get_wtick(){
    return .000000001;
}

//TODO: change in_parallel to this? 
//    or, is this necessary once single is fixed?
int omp_in_parallel(){
    if(!hpx_backend) {
        start_backend();
    }
    int active_levels = hpx_backend->get_task_data()->icv.active_levels;
    return (active_levels > 0);
}


void omp_set_dynamic(int dynamic_threads){
    if(!hpx_backend) {
        start_backend();
    }
    hpx_backend->get_task_data()->icv.dyn = (dynamic_threads != 0);
}

int omp_get_dynamic(){
    if(!hpx_backend) {
        start_backend();
    }
    return hpx_backend->get_task_data()->icv.dyn;
}

void omp_init_lock(omp_lock_t *lock){
    if(!hpx_backend) {
        start_backend();
    }
    lock = new omp_lock_t;
}

void omp_destroy_lock(omp_lock_t *lock) {
    delete lock;
}
/*
void __kmpc_atomic_fixed4_add(  ident_t *id_ref, int gtid, kmp_int32 * lhs, kmp_int32 rhs ){
    *lhs = *lhs + rhs;
}

void __kmpc_atomic_fixed4_sub(  ident_t *id_ref, int gtid, kmp_int32 * lhs, kmp_int32 rhs ){
    *lhs = *lhs - rhs;
}

void __kmpc_atomic_float8_add(  ident_t *id_ref, int gtid, double * lhs, double rhs){
    *lhs = *lhs + rhs;
}*/
