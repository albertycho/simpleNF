#include "main.h"
#include "rpc.h"
#include <stdio.h>
#include "/nethome/acho44/zsim/zSim/misc/hooks/zsim_hooks.h"



#define CPU_FREQ 2.0 // Flexus

#ifdef DEBUG_HERD

#define DHERD(M, ...) fprintf(stdout, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DHERD_NOARG(M) fprintf(stdout, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__)
#else
#define DHERD(M, ...)
#define DHERD_NOARG(M)
#endif

static __inline__ unsigned long long rdtsc(void)
{
  unsigned long hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi)<<32) ;
}





void batch_process_simpNF(rpcNUMAContext* rpcContext, RPCWithHeader* rpcs,  uint64_t *source_node_ids, NIExposedBuffer* myLocalBuffer, uint32_t batch_size, uint32_t packet_size, int tmp_count, int wrkr_lid, uint32_t sonuma_nid){
    for(int i=0; i<batch_size;i++){
        //char raw_data[2048];
        //memcpy(raw_data,rpcs[i].payload, packet_size);
	
		//just sends message back to sender. possibly add a simple check for something here..

        sendToNode_zsim( rpcContext, 
          myLocalBuffer, // where the response will come from
          packet_size, //(is_get && !skip_ret_cpy) ? resp_arr[0].val_len : 64, // sizeof is a full resp. for GET, CB for PUT
          source_node_ids[i], // node id to reply to comes from the cq entry
          0,//params.sonuma_nid,  // my nodeid unused
          0,//source_qp_to_reply, // qp to reply to comes from the payload unused
          wrkr_lid, // source qp
          true, // use true because response needs to go to a specific client
          //(char*) resp_arr[0].val_ptr, // raw data
          (char*) rpcs[i].payload,
          true, //don't cpy
          ((tmp_count-batch_size)+i)
        ); 
        do_Recv_zsim(rpcContext, sonuma_nid, wrkr_lid, 0, rpcs[i].payload, 64);
        

    }
}



void* run_worker(void* arg) {
    struct thread_params params = *(struct thread_params*)arg;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(params.id + params.start_core,&cpuset);
    int error = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (error) {
        printf("Could not bind worker thread %d to core %d! (error %d)\n", params.id, params.id+1, error);
    } else {
        printf("Bound worker thread %d to core %d\n", params.id, params.id+3);
    }
    unsigned int packet_size = params.packet_size;
    unsigned int wrkr_lid = params.id; /* Local ID of this worker thread*/

    unsigned int batch_size = params.batch_size;
    printf("simpNF batch size: %d\n", batch_size);

    /* MICA instance id = wrkr_lid, NUMA node = 0 */
    //struct mica_kv kv;
    //mica_init(&kv, wrkr_lid, 0, params.num_hash_buckets, params.log_capacity_bytes);
    //mica_populate_fixed_len(&kv, params.num_keys, MICA_MAX_VALUE);

    rpcNUMAContext* rpcContext = params.ctx; /* Msutherl: Created in main.*/

    /* Msutherl: map local buffer and qp */
    uint64_t buf_size = get_lbuf_size(rpcContext);
    uint32_t* lbuf = NULL;
    NIExposedBuffer* myLocalBuffer = NULL;
    myLocalBuffer = registerNewLocalBuffer(rpcContext,&lbuf,buf_size,wrkr_lid);
    //DLog("Local buffer at address %lld\n",myLocalBuffer);
    registerNewSONUMAQP(rpcContext,wrkr_lid);

#if defined ZSIM
    printf("sanity check for defined ZSIM\n");
#endif

	/* We can detect at most NUM_CLIENTS requests in each step */
    //struct mica_resp resp_arr[NUM_CLIENTS];
    long long rolling_iter = 0; /* For throughput measurement */
    long long nb_tx_tot = 0;

    /* Setup the pointers to pass to the RPC callback, so it can access the data store */
    //struct mica_pointers* datastore_pointer = (struct mica_pointers*) malloc(sizeof(struct mica_pointers*));
    //datastore_pointer->kv = &kv;
    //datastore_pointer->response_array = resp_arr;
    
    //rpcArg_t args;
    //args.pointerToAppData = datastore_pointer;

    pthread_barrier_wait(params.barrier);

	bool * client_done;
    bool * done_sending; //for avoiding hang when batching

#if defined ZSIM
	monitor_client_done(&client_done);
    register_done_sending(&done_sending);
	printf("monitor_client_done register done, returned addr:%lx\n");
#else
	bool dummy_true = true;
	client_done = &dummy_true;
#endif

	int tmp_count=0;

    uint32_t batch_counter = 0;
    RPCWithHeader* rpcs = calloc(batch_size, sizeof(RPCWithHeader));
    uint64_t *source_node_ids=calloc(batch_size, sizeof(uint64_t));


	
	printf("simpNF: before entering while loop\n");
    while (1) {
        uint64_t source_qp_to_reply;
        rpcs[batch_counter] = receiveRPCRequest_zsim_l3fwd( rpcContext,
                 params.sonuma_nid,
                 wrkr_lid,
                 (uint16_t *)(&(source_node_ids[batch_counter])),
                 (uint16_t *)(&source_qp_to_reply), //unused
                 client_done,
                 done_sending );

 


        if((rpcs[batch_counter].payload_len==0xdead)){
            printf("recved client done");
            break;
        }
//

        //don't increment if we broke out of recvRPCreq due to all packets sent
        // (there could be the case where we get the last packet, AND all_packets_sent is set,
        //   in which case we do increment batch couter)

	    if(rolling_iter==0){
		  zsim_heartbeat();
	    }


        // timestamp from core collection at zsim won't work with batching.
        // not worth it to change zsim to support it now, 
        // just send dummy timestamps to meet count


        if(tmp_count<1010){ // disable batching for warmup closed loop packets
            batch_size=1;
        }
        else{
            batch_size=params.batch_size;
        }
        
        if((rpcs[batch_counter].payload_len!=0xbeef)){
            batch_counter++;
	        tmp_count++;
            timestamp(tmp_count);
            timestamp(tmp_count);
            timestamp(tmp_count);
            timestamp(tmp_count);
            if(*done_sending){
                //printf("WARNING: got done sending after rpcrecv, serverid: %d, batch_counter: %d\n",wrkr_lid, batch_counter);
            }    
        }
        else{
            batch_size=batch_counter;
            //printf("rpcrecv returned 0xbeef without packet servid: %d, current batch_counter: %d\n",wrkr_lid, batch_counter);
        }

        if(batch_counter>=batch_size){
            if (batch_counter > batch_size) {
                printf("WARNING: batch_counter > batch_size, servid: %d\n", wrkr_lid);
            }
            //if(batch_size!=4){
            //    printf("batchsize %d\n", batch_size);
            //}
            uint32_t sonuma_nid = params.sonuma_nid;
            batch_process_simpNF(rpcContext, rpcs, source_node_ids, myLocalBuffer, batch_size, packet_size, tmp_count, wrkr_lid, sonuma_nid);

            batch_counter = 0;
            //if(*done_sending){
            //    break;
            //}
        }
		/* End new RPCValet */
        rolling_iter++;
        nb_tx_tot++;
		//notify_service_end(tmp_count);
    } // end infinite loop

    //free(datastore_pointer);
	zsim_heartbeat();
	printf("simpNF worker: requests serviced:%d\n", tmp_count);


	notify_done_to_zsim();

    return NULL;
}

