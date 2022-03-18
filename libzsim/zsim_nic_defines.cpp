#include "zsim_nic_defines.hpp"

#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <cstring>

#include <assert.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>


void register_buffer(void * val, void* field)
{
//variables: start addr of WQ/CQ
//			 size of WQ/CQ
//can distinguish the type of variable depending on value of rbx?
	__asm__ __volatile__(
		"xchg %%rbx, %%rbx;"
		::"b"((uint64_t)val),"c"((uint64_t)field) //clobbered registers
	);
}


int reg_wq(rmc_wq_t ** wq){
	register_buffer((void*) (wq), (void*) 0);
	return 0;
}

int reg_cq(rmc_cq_t **cq){
	register_buffer((void*) (cq), (void*) 1);
	return 0;
}


successStruct rmc_check_cq(rmc_wq_t *wq, rmc_cq_t *cq){
	successStruct ret;

	uint32_t tid;
	uint32_t wq_head = wq->head;
	uint32_t cq_tail = cq->tail;

	ret.success=1;
	ret.op=RMC_INVAL;
	//std::cout << "inside rmc_check_cq, before any while loop" << std::endl;
	wq_entry_t raw_wqe;

	int outer_loop_count=0;
	do{
		outer_loop_count++;
		if(outer_loop_count>1){
			//increment outterloop count for zsim stat
			register_buffer((void*)1, (void*)0x12);
		}

		//dbgprint
		//std::cout << "inside rmc_check_cq, first while loop" << std::endl;
		raw_wqe=wq->q[wq->head];

		cq_entry_t raw_cqe_entry = cq->q[cq_tail];
		bool tail_SR=raw_cqe_entry.SR;

		int inner_loop_count=0;
		while((tail_SR==cq->SR) && (ret.success!=0))
		{
			inner_loop_count++;
			if(inner_loop_count>1){
				//increment innerloop count for zsim stat
				register_buffer((void*)1, (void*)0x11);
			}
			//dbgprint
			//std::cout << "inside rmc_check_cq, second while loop" << std::endl;
			//FIXME: okay to unset cq valid here?
			cq->q[cq_tail].valid=0;
			cq->tail = cq->tail + 1;
			//std::cout << "app increments cq tail" << std::endl;
			if (cq->tail >= MAX_NUM_WQ) {
                cq->tail = 0;
                cq->SR ^= 1;
				//std::cout<<"APP - flips cq SR"<<std::endl;
            }

		
			if ( raw_cqe_entry.success == 0x7F) {
				ret.recv_buf_addr =  raw_cqe_entry.recv_buf_addr;
				ret.op = RMC_INCOMING_SEND;
				ret.tid = raw_cqe_entry.tid;
				return ret;		
			}
			if (raw_cqe_entry.success == 1) {
				//std::cout<<"rmc_check_cq - success:"<<std::hex<<raw_cqe_entry.success<<std::endl;
				ret.recv_buf_addr = raw_cqe_entry.recv_buf_addr;
				ret.op = RMC_INCOMING_RESP;
				ret.tid = raw_cqe_entry.tid;
				return ret;
			}

			//FIXME: tid supposed to be used like this?
			tid = raw_cqe_entry.tid;
			std::cout<<"APP - invalidating wq entry "<<tid<<std::endl;
	        
			//wq->q[tid].valid = 0;
	
		  	ret.op = wq->q[tid].op;
			if (ret.op == RMC_SABRE) {
				ret.success = cq->q[cq_tail].success;
				if (ret.success == 0) {
	                    ret.tid = tid;
				}
			}
			else
			{
				ret.op=0;
			}
			cq_tail = cq->tail;
	
			//original code calls handler here
	
			raw_cqe_entry = cq->q[cq_tail]; 
			tail_SR = raw_cqe_entry.SR;
	
	
			}


	}while (raw_wqe.valid && (ret.success != 0));

	//std::cout<<"rmc_check_cq - ret.op:"<<std::hex<<ret.op<<std::endl;

	register_buffer((void*)1, (void*)0x11);

	return ret;
}

//FIXME check data address datatype
int rmc_hw_send(rmc_wq_t *wq, uint32_t ctx_id, void *data_address, uint64_t length, uint64_t nid)
{
	uint32_t wq_head = wq->head;

	if(wq->q[wq_head].valid!=0){
		return -1;
	}
	//original code manipulates length with cacheblock size (?)

	//DLog("[sonuma] rmc_hw_send called for wq %lld.",wq);

	length += CACHE_BLOCK_SIZE - 1;
	length >>= CACHE_BLOCK_BITS;  //number of cache lines

	create_wq_entry(RMC_SEND, wq->SR, (uint32_t)ctx_id, nid, (uint64_t)data_address, 0, length, (uint64_t)&(wq->q[wq_head]));

	wq->head =  wq->head + 1;
  	// check if WQ reached its end
  	if (wq->head >= MAX_NUM_WQ) {
    	wq->head = 0;
		wq->SR ^= 1;
		//std::cout<<"APP - flips wq SR"<<std::endl;
	}
	return 0;
}

void create_wq_entry(uint32_t op, bool SR, uint32_t cid, uint64_t nid,
            uint64_t buf_addr, uint64_t offset, uint64_t length,
            uint64_t wq_entry_addr) {
	
	wq_entry_t anEntry;
    anEntry.op = op;
    anEntry.SR = SR;
    anEntry.valid = 1;
    anEntry.buf_addr = buf_addr;
    anEntry.cid = cid;
    anEntry.nid = nid;
    anEntry.offset = offset;
    anEntry.length = length;
    *((wq_entry_t *)wq_entry_addr) = anEntry;
	//NOTIFY zsim of wq write
	register_buffer((void*)(NULL), (void*)NOTIFY_WQ_WRITE);

}

int rmc_hw_recv(rmc_wq_t *wq, uint32_t ctx_id, void *recv_buf, uint64_t length){

	uint32_t wq_head = wq->head;
	if(wq->q[wq_head].valid!=0){
		return -1;
	}
	create_wq_entry(RMC_RECV, wq->SR, ctx_id, 0, (uint64_t)recv_buf, 0, length, (uint64_t)&(wq->q[wq_head])); // node id and ctx offset don't care

	wq->head =  wq->head + 1;
  // check if WQ reached its end
  
	if (wq->head >= MAX_NUM_WQ) {
		wq->head = 0;
		wq->SR ^= 1;	
		//std::cout<<"APP - flips wq SR"<<std::endl;
	}

	//register_buffer((void*)(NULL), (void*)0x14);

	return 0;
}

int kal_reg_send_recv_bufs(int kfd,ctx_entry_t *ctx_entry, send_buf_entry_t **send_buf_base,uint64_t send_buf_entries, uint64_t msg_entry_size, uint16_t num_nodes, uint32_t **recv_buf_base,send_buf_management_t **send_ctrl) {
    if (ctx_entry == NULL) {
        perror("[sonuma] No context entry allocated.");
        return -1;
    }
    if (msg_entry_size%CACHE_BLOCK_SIZE) {
        perror("[sonuma] msg_entry_size has to be multiple of cache block size");
        return -1;
    }

    //init ctx_entry
    ctx_entry->msg_entry_size = msg_entry_size;
    ctx_entry->num_nodes = num_nodes;
    ctx_entry->msgs_per_dest = send_buf_entries;

    //printf("Size of send_buf_management_t is %d\n", sizeof(send_buf_management_t));
    assert(sizeof(send_buf_management_t) == CACHE_BLOCK_SIZE);
    *send_ctrl = (send_buf_management_t *)malloc(num_nodes * sizeof(send_buf_management_t));
    if (*send_ctrl == NULL) {
        perror("[sonuma] Send buffer buffer ctrl structure could not be allocated.");
        return -1;
    }
    int i = 0;
    for (i=0; i<num_nodes; i++) {
        (*send_ctrl)[i].head = 0;
        (*send_ctrl)[i].tail = send_buf_entries - 1;
        //pthread_mutex_init(&((*send_ctrl)[i].mutex), NULL);
        //(*send_ctrl)[i].mutex = PTHREAD_MUTEX_INITIALIZER;
        (*send_ctrl)[i].lock = 0;
    }
    ////dlog("[sonuma] Send buffer ctrl structure of %d entries allocated. Head / tail initialized to 0 / %d", num_nodes, send_buf_entries-1);

/*
    //init send buffer
    uint64_t bufferSize = send_buf_entries*num_nodes*sizeof(send_buf_entry_t);
    if(*send_buf_base == NULL) {
        //dlog("[sonuma] Send buffer starting from address %p hasn't been allocated yet. Allocating...", *send_buf_base);
        int code = posix_memalign((void**)send_buf_base, EMULATOR_SW_PAGE_SIZE, bufferSize);
        if( code != 0 ) {
            fprintf(stderr,"[sonuma] Send buffer posix_memalign returned the following error: %d", code);
        }
        if (*send_buf_base == NULL) {
            perror("[sonuma] Send buffer buffer could not be allocated.");
            return -1;
        }
        //dlog("Send buffer for ctx %d was allocated starting from address 0x%p. Total size: %d.Number of entries: %d", ctx_entry->ctx_id, *send_buf_base, bufferSize, send_buf_entries*num_nodes);
    }

   std::memset(*send_buf_base,0,bufferSize);

    //init recv buffer (same as send buffer)
    int recv_buf_size = (msg_entry_size + CACHE_BLOCK_SIZE)*num_nodes*send_buf_entries;
    if (*recv_buf_base == NULL) {
        //dlog("[sonuma] Receive buffer starting from address %p hasn't been allocated yet. Allocating...", *recv_buf_base);
        int code = posix_memalign((void**)recv_buf_base, EMULATOR_SW_PAGE_SIZE,recv_buf_size);
        if( code != 0 ) {
            fprintf(stderr,"[sonuma] Recv buffer posix_memalign returned the following error: %d", code);
        }
        if (*recv_buf_base == NULL) {
            perror("[sonuma] Receive buffer buffer could not be allocated.");
            return -1;
        }
        //dlog("Recv buffer for ctx %d was allocated starting from address 0x%p. Number of entries: %d, Total size: %d",ctx_entry->ctx_id, *recv_buf_base, send_buf_entries, recv_buf_size);
    }

    //dlog("[sonuma] Buffers for messaging over ctx %d set up successfully.", ctx_entry->ctx_id);

    //PASS2FLEXUS_CONFIG(msg_entry_size, MSG_BUF_ENTRY_SIZE, ctx_entry->ctx_id);
    //PASS2FLEXUS_CONFIG(num_nodes, NUM_NODES, ctx_entry->ctx_id);
    //PASS2FLEXUS_CONFIG(send_buf_entries, MSG_BUF_ENTRY_COUNT, ctx_entry->ctx_id);
*/
    ctx_entry->send_buf_addr = *send_buf_base;
    ctx_entry->recv_buf_addr = *recv_buf_base;
    ctx_entry->buf_management = *send_ctrl;

    return 0;
}


// from sonuma.c

int kal_reg_ctx(int fd, int ctx_id, uint8_t **ctx_ptr, uint32_t num_pages) {
    int ctx_size, retcode; 
    unsigned long i;
    ctx_size = num_pages * EMULATOR_SW_PAGE_SIZE;
    if(*ctx_ptr == NULL) {
        ////dlog("[sonuma] Context starting from address %p hasn't been allocated yet. Allocating...", *ctx_ptr);
        retcode = posix_memalign((void**)ctx_ptr , EMULATOR_SW_PAGE_SIZE, ctx_size);
        if( retcode != 0 ) { 
            ////dlog("[sonuma] CTX posix_memalign returned %d", retcode);
        }
       DLog("Ctx %d was allocated starting from address 0x%p. Number of pages: %d", ctx_id, *ctx_ptr, num_pages);
    }
    return 0;
}