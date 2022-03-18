#include "main.h"
#include <getopt.h>
#include <unistd.h>
//#include "util_from_hrd.h"
//#include "mica.h"
#include "rpc.h"


#ifdef __sparc__
    #include <sys/pset.h>
#else // linux
    #ifndef _GNU_SOURCE
    #define _GNU_SOURCE // to use pthread_set_affinity_np
    #endif
#include <sched.h>
#endif


/* Generate a random permutation of [0, n - 1] for client @clt_gid */
int* get_random_permutation(unsigned int n, unsigned int clt_gid, uint64_t* seed) {
//    unsigned int i, j, temp;
//    assert(n > 0);
//
//    /* Each client uses a different range in the cycle space of fastrand */
//    for (i = 0; i < clt_gid * n; i++) {
//        hrd_fastrand(seed);
//    }
//
//    printf("client %d: creating a permutation of 0--%d. This takes time..\n",
//            clt_gid, n - 1);
//
//    int* log = (int*)malloc(n * sizeof(int));
//    assert(log != NULL);
//    for (i = 0; i < n; i++) {
//        log[i] = i;
//    }
//
//    printf("\tclient %d: shuffling..\n", clt_gid);
//    for (i = n - 1; i >= 1; i--) {
//        j = hrd_fastrand(seed) % (i + 1);
//        temp = log[i];
//        log[i] = log[j];
//        log[j] = temp;
//    }
//    printf("\tclient %d: done creating random permutation\n", clt_gid);
//
//    return log;

	return 0;
}


int main(int argc, char* argv[]) {
    /* Use small queues to reduce cache pressure */
    /*
	struct rte_hash  dummy_check_hash;
    dummy_func_link_check();
	em_dummy_print_func();
    int dummyint = rte_jhash_dummy_int();
    printf("dummyint = %d\n", dummyint);
    */
    const pid_t pid = getpid();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2,&cpuset); //just pin thread-spawning thread to core 2
	int error = sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset);
    if (error) {
        printf("Could not bind herd main thread to core 2! (error %d)\n", error);
    } else {
        printf("Bound herd main thread to core\n");
    }

    //assert(HRD_Q_DEPTH == 128);

    /* All requests should fit into the master's request region */
    //assert(sizeof(struct mica_op) * NUM_CLIENTS * NUM_WORKERS * WINDOW_SIZE <
    //        RR_SIZE);

    /* Unsignaled completion checks. worker.c does its own check w/ @postlist */
    //assert(UNSIG_BATCH >= WINDOW_SIZE);     /* Pipelining check for clients */
    //assert(HRD_Q_DEPTH >= 2 * UNSIG_BATCH); /* Queue capacity check */

    unsigned int i;
    int c;
    int run_debug_mode = 0;
    unsigned int packet_size=512; // default 512 byte packet
    unsigned int batch_size = 1;
    unsigned int num_threads = 0; // Msutherl: Fixed for -Wpedantic c++14 
    unsigned int num_rem_threads = 0;
    unsigned int node_cnt = 0; // Msutherl
    unsigned int qps = 0; // Msutherl
    int postlist = -1;
    int update_percentage = -1;
    int machine_id = 1;
    int base_port_index = -1, num_server_ports = -1, num_client_ports = -1;
    struct thread_params* param_arr;
    pthread_t* thread_arr;

    unsigned int start_core = 3;


    uint64_t num_client_keys = 1024*1024, num_hash_buckets=2*1024*1024, log_cap = 8*1024*1024; // DEFAULTS, overwritten by parameters

    static const struct option opts[] = {
        {.name = "master", .has_arg = 1, .flag = NULL, .val = 'M'},
        {.name = "num-threads", .has_arg = 1, .flag = NULL, .val = 't'},
        {.name = "start-core", .has_arg = 1, .flag = NULL, .val = 's'},
        {.name = "base-port-index", .has_arg = 1, .flag = NULL,.val = 'b'},
        {.name = "num-server-ports", .has_arg = 1, .flag = NULL,.val = 'N'},
        {.name = "num-client-ports", .has_arg = 1, .flag = NULL,.val = 'n'},
        {.name = "is-client", .has_arg = 1,.flag = NULL, .val = 'c'},
        {.name = "update-percentage", .has_arg = 1,.flag = NULL, .val = 'u'},
        {.name = "machine-id", .has_arg = 1,.flag = NULL, .val = 'm'},
        {.name = "total-nodes", .has_arg = 1,.flag = NULL, .val = 'C'},
        {.name = "qps-to-create", .has_arg = 1,.flag = NULL, .val = 'q'},
        {.name = "num-remote-threads", .has_arg = 1,.flag = NULL, .val = 'O'},
        {.name = "num-keys", .has_arg = 1,.flag = NULL, .val = 'K'},
        {.name = "num-server-buckets", .has_arg = 1,.flag = NULL, .val = 'B'},
        {.name = "log-capacity-bytes", .has_arg = 1,.flag = NULL, .val = 'L'},
        {.name = "debug", .has_arg = 1,.flag = NULL, .val = 'D'},
        {.name = "packet-size", .has_arg = 1,.flag = NULL, .val = 'r'},
        {.name = "batchsize", .has_arg = 1,.flag = NULL, .val = 'd'},
        {.name = "postlist", .has_arg = 1,.flag = NULL, .val = 'p'} };

    /* Parse and check arguments */
    while (1) {
        c = getopt_long(argc, argv, "M:t:b:N:n:c:u:m:C:q:O:K:B:L:D:r:d:p", opts, NULL);
        if (c == -1) {
            printf("GETOPT FAILED (1 FAIL expected)\n");
            break;
        }
        switch (c) {
            case 'K':
                num_client_keys = atol(optarg);
                printf("l3fwd num_keys set to %d\n", num_client_keys);
                break;
            case 's':
                start_core = atol(optarg);
                break;
            case 'L':
                log_cap = atol(optarg);
                printf("l3fwd log_cap set to %d\n", log_cap);
                break;
            case 'B':
                num_hash_buckets = atol(optarg);
                break;
            case 'D':
                run_debug_mode = atoi(optarg);
                break;
            case 't':
                num_threads = atol(optarg);
                printf("l3fwd num_threads set to %d\n", num_threads);
                break;
            case 'b':
                base_port_index = atoi(optarg);
                break;
            case 'N':
                num_server_ports = atoi(optarg);
                break;
            case 'n':
                num_client_ports = atoi(optarg);
                break;
            case 'u':
                update_percentage = atoi(optarg);
                break;
            case 'm':
                machine_id = atoi(optarg);
                break;
            case 'p':
                postlist = atoi(optarg);
                break;
            case 'C':
                node_cnt = atoi(optarg);
                break;
            case 'O':
                num_rem_threads = atoi(optarg);
                break;
            case 'q':
                qps = atoi(optarg);
                printf("l3fwd qps set to %d\n", qps);
                break;
            case 'r':
                packet_size = atoi(optarg);
                printf("l3fwd packetsize set to %d\n", packet_size);
                break;
            case 'd':
                batch_size = atoi(optarg);
                printf("l3fwd batchsize set to %d\n", batch_size);
                break;
            default:
                printf("Invalid argument %d\n", c);
                assert(false);
        }
    }

    /* Common sanity checks for worker process and per-machine client process */

    assert( qps >= num_threads ); 

    rpcNUMAContext* rpcContext = NULL;
    rpcContext = createRPCNUMAContext(0, // dev_fd
        1, //doesn't matter in zsim env? //machine_id, // mynodeid
        node_cnt, // total nodes
        0, // start qp
        num_threads - 1, // end qp ( 1 qp per thread )
        0, // clocal-pages (FIXME: what is this?)
        1, // true if senders context
        num_threads, // used to make cbufs
        num_rem_threads, // used to make cbufs (PER CLIENT NODE)
		packet_size
    );

    /* Launch a single server thread or multiple client threads */
    printf("main: Using %d threads\n", num_threads);
    printf("main - update_percentage: %d\n", update_percentage); //dunno if we need update percentage.. putting print to suppress unused error
    param_arr = (struct thread_params*) malloc(num_threads * sizeof(struct thread_params));

    // setup barriers & initialization lock
    pthread_barrier_t* start_barrier = (pthread_barrier_t*) malloc(sizeof(pthread_barrier_t));
    if( start_barrier == NULL ) {
        printf("Allocating start barrier failed.\n");
        return 1;
    }
    pthread_mutex_t *init_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    if( init_lock == NULL ) {
        printf("Allocating init lock failed.\n");
        return 1;
    }
    if( pthread_barrier_init(start_barrier, NULL, num_threads) != 0 ) {
        printf("Initializing start barrier failed.\n");
        return 1;
    }
    if( pthread_mutex_init(init_lock, NULL) != 0 ) {
        printf("Initializing mutex failed.\n");
        return 1;
    }

    //cpu_set_t cpuset;

    thread_arr = (pthread_t*) malloc(num_threads * sizeof(pthread_t));
    int *thread_pin = (int*) calloc(num_threads,sizeof(int));
    /* Setup thread pin array for linux, remove old sparc */
    for(i=0;i<num_threads;i++) {
        thread_pin[i] = i+2;
    }

    for (i = 0; i < num_threads; i++) {
        param_arr[i].barrier = start_barrier;
        param_arr[i].init_lock = init_lock;
        param_arr[i].postlist = postlist;

        param_arr[i].sonuma_nid = machine_id;
        param_arr[i].node_cnt = node_cnt;
        param_arr[i].qps_per_node = qps;
        param_arr[i].ctx = rpcContext;

        param_arr[i].num_keys = num_client_keys;
        param_arr[i].num_hash_buckets = num_hash_buckets;
        param_arr[i].log_capacity_bytes = log_cap;
        param_arr[i].run_debug_mode = run_debug_mode;
        param_arr[i].start_core = start_core;

       
     
        param_arr[i].id = i;
        param_arr[i].base_port_index = base_port_index;
        param_arr[i].num_server_ports = num_server_ports;
        param_arr[i].num_client_ports = num_client_ports;
        param_arr[i].node_cnt = node_cnt;
        /* Msutherl: Always set # of other threads */
        param_arr[i].num_client_threads = num_rem_threads;
        param_arr[i].num_serv_threads = num_threads;
        param_arr[i].packet_size = packet_size;
        param_arr[i].batch_size= batch_size;

        //int core = thread_pin[i];
        printf("main launching pthread_create runworker\n");
        int err = pthread_create(&thread_arr[i], NULL, run_worker, &param_arr[i]);
        if (err) {
            printf("l3fwd main thread: pthread_create failed with err=%d\n", err);
        }

    }

    for (i = 0; i < num_threads; i++) {
        pthread_join(thread_arr[i], NULL);
    }

    free(init_lock);
    free(start_barrier);

    return 0;
}
