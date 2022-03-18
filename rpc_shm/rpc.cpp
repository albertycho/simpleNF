#include "rpc.h"
#include <iostream>
#include <thread>
#include <chrono>

#include <arpa/inet.h> // for hton and ntoh

#define CTX_0 0

#ifdef DEBUG_RPC
#include <cstring>

#define DRPC(M, ...) fprintf(stdout, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DRPC_NOARG(M) fprintf(stdout, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__)
#else
#define DRPC(M, ...)
#define DRPC_NOARG(M)
#endif


inline uint32_t getAlignmentReq() {
    return 8;
}

// get header size (equal for all rpcs)
inline uint32_t calcTotalHeaderBytes() {
    uint32_t hbytes = 
        sizeof(uint32_t) +      // rpc_len = 4B 
        2*sizeof(char) +        // rpc_type = 1B BUT NEED 2X to align 2B accesses
        sizeof(uint16_t) +      // rpc_id = 2B
        sizeof(uint16_t) +      // senders_qp = 2B
        sizeof(uint16_t);       // senders_nid = 2B
    uint32_t alignmentBytes = hbytes % getAlignmentReq();
    return hbytes + alignmentBytes;
}

// get just the length field size (for vm platform w. sockets)
uint32_t getLenParamBytes() { return sizeof(uint32_t); }
// get payload size
uint32_t getPayloadBytes(RPCWithHeader* r) {return r->rpc_len - calcTotalHeaderBytes(); }

void RWRITE_Complete(uint8_t tid, wq_entry_t* head, void* owner)
{
    // nil
}

NIExposedBuffer* registerNewLocalBuffer(rpcNUMAContext* ctx, uint32_t** tptr,size_t buf_size,size_t tidx) {
    NIExposedBuffer* newLBufForQP = nullptr;
    int rcode = 0;
#if defined FLEXUS
    int fd_qp = 0;
    rcode = kal_reg_lbuff(fd_qp, tptr,(buf_size+EMULATOR_SW_PAGE_SIZE)/EMULATOR_SW_PAGE_SIZE);
    newLBufForQP = new NIExposedBuffer(tptr);
#elif defined QFLEX
    rcode = kal_reg_lbuff(ctx->kfd, tptr, (buf_size+EMULATOR_SW_PAGE_SIZE)/EMULATOR_SW_PAGE_SIZE,tidx);
    newLBufForQP = new NIExposedBuffer(tptr);
#elif defined ZSIM
	printf("buf_size: %d\n", buf_size);
	register_buffer((void*) (buf_size), (void*) 3);
    register_buffer((void*) (tptr), (void*) 2);
    newLBufForQP = new NIExposedBuffer(tptr);
#endif
    if( rcode < 0 ) {
        printf("Failed to allocate local buffer\n");
        return nullptr;
    }
    ctx->app_buffers.at(tidx) = newLBufForQP;
    return newLBufForQP;
}

soNUMAQP_T* registerNewSONUMAQP(rpcNUMAContext* ctx, size_t tidx)
{
    soNUMAQP_T* qp = nullptr;
    rmc_wq_t *wq;
    rmc_cq_t *cq;
    int rcode = 0;
    register_buffer((void*) (&wq), (void*) 0);
    //DLog("New wq registered at address %lld\n", wq);
    if( rcode < 0 ) {
        printf("Failed to register WQ from thread %lu.\n",tidx);
        return nullptr;
    }
    //register CQs
    register_buffer((void*) (&cq), (void*) 1);
    //DLog("New cq registered at address %lld\n", cq);
    if( rcode < 0 ) {
        printf("Failed to register CQ from thread %lu.\n",tidx);
        return nullptr;
    }

    qp = new soNUMAQP_T(tidx,&wq,&cq);
    ctx->qps.at(tidx) = qp;
    return qp;
}

void notify_done_to_zsim(){
#if defined ZSIM
	register_buffer((void*) 0, (void*) 0xdead);
#endif
}


void monitor_client_done(bool ** client_done){
#if defined ZSIM
	register_buffer((void*) client_done, (void*) 0xD);
#endif

}
void register_done_sending(bool ** done_sending){
#if defined ZSIM
	register_buffer((void*) done_sending, (void*) 0x16);
#endif

}

void notify_service_start(int count){
#if defined ZSIM
	register_buffer((void*) count, (void*) 0xE);
#endif
}
void notify_service_end(int count){
#if defined ZSIM
	register_buffer((void*) count, (void*) 0xF);
#endif
}
void timestamp(int count){
#if defined ZSIM
	register_buffer((void*) count, (void*) 0x13);
#endif
}
void notify_fruitless_cq_check(int count){
#if defined ZSIM
	register_buffer((void*) count, (void*) 0x10);
#endif
}

// parses some parameters and makes the rpcNUMA context with underlying
// soNUMA calls 
rpcNUMAContext* createRPCNUMAContext(int dev_fd, unsigned int aNodeID, unsigned int totalNodes, unsigned int qp_start, unsigned int qp_end, size_t context_local_pages, bool isSendingCtx, unsigned int serverThreadCount, unsigned int clientThreadCount, unsigned int packet_size )
{
    // defined in sonuma.h
    uint64_t ctx_size = EMULATOR_SW_PAGE_SIZE * 2;
#ifndef QFLEX
    uint64_t buf_size = EMULATOR_SW_PAGE_SIZE;
#endif

#ifdef FLEXUS
    int fd_msg = SR_CTX_ID;   //the context ID used for messaging
    int fd_qp = 0;
#elif defined QFLEX
    int kfd = kal_open((char*)RMC_DEV);  
    if( kfd < 0 ) {
        fprintf(stdout, "kal_open failed w. error type %s!\n",strerror(kfd));
        return nullptr;
    }
    // setup this process' pt integration (QFLEX)
    kal_setup_pt(kfd);
#elif defined ZSIM
    int fd_msg = SR_CTX_ID;   //the context ID used for messaging
    int fd_qp = 0;
    int kfd = 0;
#else
    int kfd = 0;
#endif

    printf("Beginning creation of rpcNUMA context.....\n");
    rpcNUMAContext* ctx = new rpcNUMAContext;
#ifdef QFLEX
    ctx->kfd = kfd;
#endif

    printf("Beginning map of accessible 1-sided memory context...\n");
    //register context
    ctx->pgas = NULL;
    if(kal_reg_ctx(kfd, SR_CTX_ID, &(ctx->pgas), ctx_size/EMULATOR_SW_PAGE_SIZE) < 0) {
        printf("Failed to allocate context\n");
        return nullptr;
    } else {
        fprintf(stdout, "Ctx buffer was registered, ctx_size=%ld, %ld pages.\n",
                ctx_size, ctx_size*sizeof(uint8_t) / EMULATOR_SW_PAGE_SIZE);
    }

    printf("Pointer to pgas: %p\n",&(*(ctx->pgas)));

    printf("Beginning map of send/recv paired memory locations...\n");
    char fmt[25];
    sprintf(fmt,"recv_slots_node%d.txt",aNodeID);

    ctx->msg_domain = new soNUMAMessagingDomain;
    ctx->msg_domain->recv_slots = nullptr;
    ctx->msg_domain->send_buf= nullptr;
    ctx->msg_domain->send_ctrl = nullptr;

    ctx->msg_domain->ctx_struct.ctx_id = SR_CTX_ID;
    int msg_size,num_nodes,msgs_per_node;
    msg_size = packet_size;
    num_nodes = totalNodes;
    msgs_per_node = MSGS_PER_PAIR;

    kal_reg_send_recv_bufs(ctx->kfd,&(ctx->msg_domain->ctx_struct), &(ctx->msg_domain->send_buf), msgs_per_node, msg_size, num_nodes, &(ctx->msg_domain->recv_slots), &(ctx->msg_domain->send_ctrl));
    ctx->msg_domain->per_node_chunk = (ctx->msg_domain->ctx_struct.msg_entry_size + CACHE_BLOCK_SIZE)*ctx->msg_domain->ctx_struct.msgs_per_dest;
    printf("Succeeded mapping send/recv slots for rpcvalet...\n");

    ctx->lbuf_size_bytes = msgs_per_node * msg_size;
	printf("msgs_per_node: %d, msg_size: %d\n", msgs_per_node, msg_size);

    /* Msutherl: refactor so that master thread creates context and only a bunch of nullptrs */
    size_t num_qps = qp_end - qp_start + 1;
    ctx->qps = std::vector<soNUMAQP_T*>(num_qps,nullptr);
    ctx->app_buffers = std::vector<NIExposedBuffer*>(num_qps,nullptr);
    /*
    for(unsigned int i = qp_start; i <= qp_end; i++ ) {
        //register local buffer with sonuma KAL fcns and put it into sonumaMsgContext
        uint8_t* tptr = nullptr;
        if(kal_reg_lbuff(fd_qp, &tptr,buf_size/EMULATOR_SW_PAGE_SIZE) < 0) {
            printf("Failed to allocate local buffer\n");
            return nullptr;
        }
        if(kal_reg_wq(fd_qp, &twq) < 0) {
            printf("Failed to register WQ\n");
            return nullptr;
        }
        //register CQs
        if(kal_reg_cq(fd_qp, &tcq) < 0) {
            printf("Failed to register CQ\n");
            return nullptr;
        }
    } // end loop over all qps to make
    */
    fprintf(stdout,"Init done!\n");
    return ctx;
}


// do i ever really destroy things in this kind of code?
// - i guess i should
void destroyRPCNUMAContext(rpcNUMAContext* ctx)
{
    for(size_t i = 0; i < ctx->app_buffers.size(); i++ ) {
        delete ctx->app_buffers[i];
    }
    for(size_t i = 0; i < ctx->qps.size(); i++ ) {
        delete ctx->qps[i];
    }
    for(size_t i = 0; i < ctx->circ_buffers.size(); i++ ) {
        delete ctx->circ_buffers[i];
    }
    delete ctx->msg_domain;
    delete ctx;
}

void sendToNode_zsim(rpcNUMAContext* rpcContext, NIExposedBuffer* messageBuffer, size_t messageByteSize, unsigned int destNode, unsigned int clientFrom, unsigned int qpTarget, unsigned int sendQP, bool send_qp_terminate,char* raw_payload_data, bool skipcpy, unsigned int rpc_send_count)
{
    //DRPC("Client [%d] calls sendToNode [%d], sendQP [%d], qpTarget [%d]",clientFrom,destNode,sendQP,qpTarget);

    /* Calculate local buffer address based on rpc_send_count, wrapping around when its greater than num msgs outstanding. */
    ctx_entry_t *the_ctx = &(rpcContext->msg_domain->ctx_struct);

    size_t lbuf_offset = ((rpc_send_count % the_ctx->msgs_per_dest) * the_ctx->msg_entry_size);

    //adding size_t to uint32_t* adds the value*4 because pointer increments by 4..
    //at least that's what we see here, so divide by 4 to accomodate            
    lbuf_offset=lbuf_offset/4;       

    uint32_t* net_buffer_vaddr = messageBuffer->getUnderlyingAddress(lbuf_offset);
	//printf("rpc_send_count: %d, lbuf_offset: %d, net_buffer_vaddr: %lx, underlying_buffer_base: %lx\n", rpc_send_count, lbuf_offset, net_buffer_vaddr, messageBuffer->underlyingBuffer);

    //PASS2FLEXUS_DEBUG((uint64_t)lbuf_offset,MEASUREMENT,(uint64_t)net_buffer_vaddr);
    soNUMAQP_T* my_qp = rpcContext->qps.at(sendQP);
    // copy the struct into the netbuffer address



    if( skipcpy ) {
        *((uint32_t*)net_buffer_vaddr) = 0x1234; // simulate header write
    } else {
        //my_memcpy(net_buffer_vaddr,raw_payload_data,messageByteSize); // from libsonuma
        mempcpy(net_buffer_vaddr,raw_payload_data,messageByteSize);
    }


    /* Use rmc_hw_send for nebula - no software slot locking needed */
    //rmc_hw_send(my_qp->wq,the_ctx->ctx_id,net_buffer_vaddr,messageByteSize,destNode);
    int send_ret;
    do {
        send_ret=rmc_hw_send(my_qp->wq, the_ctx->ctx_id, net_buffer_vaddr, messageByteSize, destNode);
    } while (send_ret);

}

#if 0
void sendToNode(rpcNUMAContext* rpcContext, NIExposedBuffer* messageBuffer, size_t messageByteSize, unsigned int destNode, unsigned int clientFrom, unsigned int qpTarget, unsigned int sendQP, bool send_qp_terminate)
{
    /* Implementation of message using rmc_send from low-level libsoNUMA
     * 1) Get a send slot using atomic op
     * 2) That's it, send the message.
     */
    DRPC("Client [%d] calls sendToNode [%d], sendQP [%d], qpTarget [%d]",clientFrom,destNode,sendQP,qpTarget);
    soNUMAQP_T* my_qp = rpcContext->qps.at(sendQP);
    rmc_send_xen(my_qp->wq,
            (char*)messageBuffer->getUnderlyingAddress(0), 
            0, // lbuff_offset = 0,
            messageByteSize,
            destNode,
            qpTarget, 
            send_qp_terminate
            );
}
#endif

//marina: is this used?
#if 0
void remoteBufferWrite(rpcNUMAContext* rpcContext,NIExposedBuffer* messageBuffer, size_t messageByteSize, unsigned int destNode, unsigned int clientNodeID, unsigned int clientThread, int threadOnDestNode )
{
    /* Implementation of buffer write using 1 sided WRITE from low-level libsoNUMA
     * 1) calculate context offset to destination node
     * 2) calculate worker thread id and offset to specific receive buffer
     * 3) write messageBuffer there.
     */
    assert( rpcContext->isSenderCtx );
    assert( threadOnDestNode >= 0 ); // for WRITE 
    assert( rpcContext->qps.size() >= clientThread );

    /* 1) Calculate cbuf to do rwrite into. */
    size_t buf_idx = rpcContext->getCBufIdx(clientNodeID,clientThread,threadOnDestNode);
    CircularReceiveBuffer* circularBuffer = rpcContext->circ_buffers.at(buf_idx);

    /*  1a) check for space in the buffer before writing...
     *      - if full, do something like sleep() until I impl. smarter decision */
    size_t numSlotsFree = circularBuffer->getAvailSlots();
    DRPC("Remote buffer %d has %d slots free...\n",buf_idx,numSlotsFree);
    if( numSlotsFree == 0 ) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    /* 2) Offset into pgas calculate from buffer tail address */
#ifdef DEBUG_RPC
    uint64_t localOffset = circularBuffer->getTailOffset();
    DRPC("Doing RWRITE into circular buffer #%d. Tail Idx = %d, LOCAL Offset = %x\n",buf_idx,circularBuffer->getTailIdxValue(),localOffset);
    uint64_t increment = (circularBuffer->getBasePointer() - rpcContext->pgas);
    uint64_t globalOffset = increment + localOffset;
    DRPC("INCREMENT: %x. GLOBAL Offset = %x, ADDR = %x \n",increment, globalOffset, (rpcContext->pgas)+globalOffset);
#endif

    /* 3a) Flip sense reversal of SCB */
    circularBuffer->writeSRMetadata( circularBuffer->getTailIdxValue() );

    /* 3b) call rwrite_sync() to the address in the circular buffer metadata structure */
#if defined FLEXUS || defined QFLEX
    // This won't work in qflex
#else
    soNUMAQP_T* my_qp = rpcContext->qps.at(clientThread);
    rmc_rwrite_sync(my_qp->wq,
            my_qp->cq,
            messageBuffer->getUnderlyingAddress(0),
            0, // lbuff_offset = 0
            destNode,
            CTX_0, 
            globalOffset,
            messageByteSize
            );
#endif

    /* 4) increment the tail and check for over-run */
    circularBuffer->incTail(); 
}
#endif

#if 0
void 
receiveRPCRequest(rpcNUMAContext* rpcContext, herdCallback* cb, void* pointer_to_data_store, unsigned int serv_nid, unsigned int serv_qp_id, uint16_t* source_node_id,uint16_t* source_qp_id)
{
    /* Implementation using CQ polling ( raw rpc message will be in recv_slots )
     * 1) Get the correct CQ.
     * 2) Poll it using rmc_poll_cq_rpc( ... )
     *  2a) This invokes the callback.
     *  2b) It also sets the source node that actually did the RPC.
     * 3) Use rmc_recv( ... ) to perform flow control and free send slots
     */

    // 1) Get CQ
    assert( rpcContext->qps.size() >= serv_qp_id );
    soNUMAQP_T* my_qp = rpcContext->qps.at(serv_qp_id);

    // 2) poll it
    //uint8_t* raw_recv_ptr = rpcContext->msg_domain->recv_slots;
    uint16_t slot = 0; // FIXME: uncomment line below
    //rmc_poll_cq_rpc(my_qp->cq, raw_recv_ptr, cb , source_node_id , source_qp_id , &slot,(void*)pointer_to_data_store);
    // 3) free slot on send-side after message received
    rmc_recv_xen(my_qp->wq,*source_node_id,*source_qp_id,slot,true /* do a dispatch */);
}
#endif

RPCWithHeader
receiveRPCRequest_zsim_l3fwd(rpcNUMAContext* rpcContext, unsigned int serv_nid, unsigned int serv_qp_id, uint16_t* source_node_id,uint16_t* source_qp_id, bool* client_done, bool* done_sending)
{
	//printf("inside RPCReq_zsim\n");
    /* Implementation using CQ polling ( raw rpc message will be in recv_slots )
     * 1) Get the correct CQ.
     * 2) Poll it using rmc_poll_cq_rpc( ... )
     *  2a) This invokes the callback.
     *  2b) It also sets the source node that actually did the RPC.
     * 3) Use rmc_recv( ... ) to perform flow control and free send slots
     */
    // 1) Get CQ
    //assert( rpcContext->qps.size() >= serv_qp_id );
    soNUMAQP_T* my_qp = rpcContext->qps.at(serv_qp_id);

    // 2) poll it
	//uint64_t check_count=0;
    int count=0;
    successStruct retd_from_rmc;
    retd_from_rmc.op = RMC_INVAL;
    do {
        count++;
        if (count > 500) {
			//printf("looping in dowhielloop\n");
            RPCWithHeader rpc;
            rpc.payload_len = 0xbeef;
            return rpc;
        }
		//check_count++;
        retd_from_rmc = rmc_check_cq(my_qp->wq,my_qp->cq);
    //} while ( retd_from_rmc.op != (RMC_INCOMING_SEND) && !(*client_done) && !(*done_sending));
    } while ( retd_from_rmc.op != (RMC_INCOMING_SEND) && !(*client_done));
	//dbg
	//if(check_count<2){
	//	printf("rmc_check_cq count is less than 2 - does this happen?\n");
	//}
	//if(check_count>2){
	//	//send magic inst to let zsim log fruitless loop per core
	//	uint64_t extra_count = check_count - 2;
	//	notify_fruitless_cq_check((int)extra_count);
	//}

	//printf("after rmc_check_cq\n");
    /*if((*done_sending)) {
        if(retd_from_rmc.op!=RMC_INCOMING_SEND){
            printf("done sending seen during rmccheck, no packet, serverid:  %d\n", serv_qp_id);
            RPCWithHeader rpc;
            rpc.payload_len=0xbeef;
            return rpc;
        }
        printf("done sending seen during rmccheck, with vaid packet\n");
        if (retd_from_rmc.op == RMC_INCOMING_SEND) {
            printf("valid packet RMC_INCOMING_SEND, serverid: %d\n", serv_qp_id);
        }
        else {
            printf("valid packet but NOT RMC_INCOMING_SEND, serverid: %d\n", serv_qp_id);
        }
    }*/


    if((*client_done)) {
        if(retd_from_rmc.op!=RMC_INVAL){
            //printf("WARNING! client done recvd, but a valid packet also recvd, serverid: %d\n", serv_qp_id);
        }
        RPCWithHeader rpc;
        rpc.payload_len=0xdead;
        return rpc;
    }
    /* Calculate receive pointer from slot*/
    char* rbuf_slot_ptr = (char*) retd_from_rmc.recv_buf_addr;
    RPCWithHeader rpc;

    rpc.payload = rbuf_slot_ptr;
    rpc.senders_nid = retd_from_rmc.tid;
    *source_node_id = retd_from_rmc.tid;
    //*source_node_id = ((uint64_t)rbuf_slot_ptr - (uint64_t)rpcContext->msg_domain->ctx_struct.recv_buf_addr) / rpcContext->msg_domain->per_node_chunk;

	//printf("before returning from recvPRCReq_zsim\n");
    return rpc;
}

#if 0
void receiveRPCResponse(rpcNUMAContext* rpcContext, herdCallback* cb, unsigned int cl_nid , unsigned int cl_tid, unsigned int serv_tid) 
{
    /* Implementation using CQ polling ( message will be in recv_slots )
     * 1) Get the correct CQ.
     * 2) Poll it using rmc_poll_cq_rpc( ... )
     *  2a) This invokes the callback.
     * 3) Use rmc_recv( ... ) to perform flow control and free send slots
     */

    // 1) Get CQ
    assert( rpcContext->qps.size() >= cl_tid );
    soNUMAQP_T* my_qp = rpcContext->qps.at(cl_tid);

    uint16_t sending_qp = 0, sending_nid = 0;
    uint16_t slot = 0; // FIXME: uncomment below
    //uint8_t* raw_recv_ptr = rpcContext->msg_domain->recv_slots;
    // 2) poll it
    //rmc_poll_cq_rpc(my_qp->cq, raw_recv_ptr, cb , &sending_nid, &sending_qp, &slot, nullptr );

    // 3) free slot on send-side after message received
    rmc_recv_xen(my_qp->wq,sending_nid,sending_qp,slot,false);
}
#endif


void do_Recv_zsim( rpcNUMAContext* rpcContext, unsigned int cl_nid , unsigned int cl_tid, unsigned int serv_tid,char* rbuf_slot_ptr,unsigned int freeSizeBytes)
{
    assert( rpcContext->qps.size() >= cl_tid );
    soNUMAQP_T* my_qp = rpcContext->qps.at(cl_tid);
    ctx_entry_t* ctx_ptr = &(rpcContext->msg_domain->ctx_struct);
    rmc_hw_recv(my_qp->wq, ctx_ptr->ctx_id, (void*)rbuf_slot_ptr, freeSizeBytes);
}

void flowControlUpdateSendersHead(CircularReceiveBuffer* buf, unsigned int inc)
{
    buf->incSendersHeadToFreeSpace(inc);
}

void setupCircularBuffers(rpcNUMAContext* ctx,unsigned int nidToCreateOn,size_t entryByteSize,size_t numEntries, size_t aNumberOfSenders, size_t aNumberOfReceivers, bool are_sendersCopies )
{
    uint8_t* bufferBasePointer = ctx->pgas;
    DRPC("Original bufferBasePointer = %p",bufferBasePointer);
    size_t numBuffers = aNumberOfSenders * aNumberOfReceivers;
    ctx->circ_buffers.resize( numBuffers );

    for(size_t bIt = 0; bIt < numBuffers; bIt++) {
        ctx->circ_buffers.at(bIt) = new CircularReceiveBuffer(&bufferBasePointer,EMULATOR_SW_PAGE_SIZE,MSGS_PER_PAIR,are_sendersCopies);
        DRPC("Created circular buffer number %d, with base %x", bIt, bufferBasePointer);
        bufferBasePointer += ctx->circ_buffers.at(bIt)->GetReservedMemoryBytes();
    }
}

/*
   SendersCopyOfCRB** setupSendersBuffers(rpcNUMAContext* ctx,unsigned int nidToCreateOn,size_t entryByteSize,size_t numEntries, size_t aNumberOfSenders, size_t aNumberOfReceivers)
   {
   uint8_t* ptrToCtxNodeBase = (ctx->pgas); // FIXME: should take nidToCreateOn into acc
   size_t numBufs = aNumberOfSenders * aNumberOfReceivers;
   SendersCopyOfCRB** bufArray = new SendersCopyOfCRB*[numBufs];

   for(size_t i = 0; i < numBufs ; i++) {
// init all of them
bufArray[i] = new SendersCopyOfCRB(&ptrToCtxNodeBase,entryByteSize,numEntries);
}

return bufArray;
}
*/

/*
uint8_t* trampolineToGetUnderlyingAddress(NIExposedBuffer* buf, const int byteOffset)
{
    return buf->getUnderlyingAddress(byteOffset);
}

NIExposedBuffer* trampolineToGetNIExposedBuffer(rpcNUMAContext* ctx, const size_t idx) {
    return ctx->getPtrToExposedBuffer(idx);
}
*/
uint8_t* pollOnBufferNumber(rpcNUMAContext* ctx , size_t bufNumber, pollFunction* dec)
{
    CircularReceiveBuffer* rbuf = ctx->circ_buffers.at(bufNumber);
    uint8_t* poll_loc = rbuf->getHeadAddress();
    if( dec(&poll_loc) )  {
        return poll_loc;
    } else {
        return NULL;
    }
}

void zeroOutRBufHead(rpcNUMAContext* ctx, size_t bufferNumber)
{
    CircularReceiveBuffer* ptr = ctx->circ_buffers.at(bufferNumber);
    ptr->zeroEntryNumber( ptr->getHeadIdxValue() );

}

void incrementBufferHead(rpcNUMAContext* ctx, size_t bufNumber)
{
    ctx->circ_buffers.at(bufNumber)->incHeadAfterMsgProcessed();
#ifdef DEBUG_RPC
    std::cout << "Buffer num [" << bufNumber << "] Head Update." << std::endl
        << "\tNew head index: " << ctx->circ_buffers.at(bufNumber)->getHeadIdxValue()
        << "\tNext buffer address to poll: " << (void*) ctx->circ_buffers.at(bufNumber)->getHeadAddress()
        << std::endl;
#endif
}

size_t getBufferHeadIndex(rpcNUMAContext* ctx, size_t bufferNumber)
{
    return ctx->circ_buffers.at(bufferNumber)->getHeadIdxValue();
}

size_t getBufferIDX(rpcNUMAContext* ctx, unsigned int cl_nid, unsigned int cl_tid, unsigned int serv_tid) {
    return ctx->getCBufIdx(cl_nid,cl_tid,serv_tid);
}

void addToContainer(rpcNUMAContext* ctx, double element)
{
    ctx->rpcServiceTimeStats.append(element);
}

void writeContainerToFile(rpcNUMAContext* ctx, const char* fname)
{
    std::string arg(fname);
    ctx->rpcServiceTimeStats.writeToFile( arg );
}

void readContainerFromFile(rpcNUMAContext* ctx, const char* fname, size_t num_elements)
{
    std::string arg(fname);
    ctx->rpcServiceTimeStats.readFromFile( arg, num_elements );
}

size_t 
rpcNUMAContext::getCBufIdx(unsigned cl_nid, unsigned cl_tid, unsigned sr_thread) const {
    /* Layout:
     *  each serv. thread gets n_clients * thrs_per_client = BufsPerServThread
     *  Server thread N gets buffers w. ids: 
     *   [ N*(bufsPerServThread), (N+1)*bufsPerServThread - 1 ]
     *   e.g., w. 2 clients, 2 thrs_client
     *        serv. 0 -> [0-3]
     *        serv. 1 -> [4-7]
     *
     *        Inside each server-region, e.g., [0-3]:
     *          Client m, thread t gets buffer: (m * numClients) + t 
     *              * NOTE: Clients go from [1-(numClients-1)]
     *                 m = 0, t = 1 -> Buffer 1
     *                 m = 1, t = 0 -> Buffer 2
     */
    //DRPC("cl_nid: %u, cl_tid: %u, sr_thread: %u\n",cl_nid,cl_tid,sr_thread);
    unsigned zeroShifted_clNid = cl_nid - 1;
    size_t bufsPerServThread = getNumClients() * threadsPerClient;
    size_t serv_region_idx = sr_thread * bufsPerServThread;
    size_t client_buffer = (zeroShifted_clNid*getNumClients()) + cl_tid;
    //DRPC("Bufs per serv thread: %u, serv_region_idx: %u, client_buffer_within_region : %u, final: %u\n",bufsPerServThread,serv_region_idx,client_buffer, serv_region_idx + client_buffer);

    return serv_region_idx + client_buffer;
}


/* All functions for RPC layer encap/decap */
RPCWithHeader createRPCStruct(char aType, uint16_t anRPCId, uint16_t aQPId, uint16_t aNID,uint32_t aPayloadLen, char* aPayloadPointer)
{
    RPCWithHeader rpc;
    rpc.rpc_len = calcTotalHeaderBytes() + aPayloadLen;
    rpc.rpc_type = aType;
    rpc.rpc_id=anRPCId;
    rpc.senders_qp = aQPId;
    rpc.senders_nid = aNID;
    rpc.payload = aPayloadPointer;
    rpc.payload_len = aPayloadLen;
    return rpc;
}

void packPayload(RPCWithHeader* rpc, char* buf)
{
    /* ASSUMES OTHER SIDE CAN READ IN THE SAME ENDIANNESS. */
#if defined FLEXUS || defined QFLEX || defined ZSIM
    uint32_t net_message_len = rpc->rpc_len;
    memcpy(buf,&net_message_len,sizeof(uint32_t));
    buf += sizeof(uint32_t);
    memcpy(buf,&(rpc->rpc_type),sizeof(char)); // no byte order needed
    buf += 2*sizeof(char);
    uint16_t net_rpc_id = rpc->rpc_id;
    memcpy(buf,&net_rpc_id,sizeof(uint16_t));
    buf += sizeof(uint16_t);
    uint16_t net_senders_qp = rpc->senders_qp;
    memcpy(buf,&net_senders_qp,sizeof(uint16_t));
    buf += sizeof(uint16_t);
    uint16_t net_senders_nid = rpc->senders_nid;
    memcpy(buf,&net_senders_nid,sizeof(uint16_t));
    buf += sizeof(uint16_t);
    if( (uint64_t) buf % getAlignmentReq() ) {
        buf += ((uint64_t)buf % getAlignmentReq());
    }
    memcpy(buf,rpc->payload,rpc->payload_len);
#else
    uint32_t net_message_len = htonl(rpc->rpc_len);
    memcpy(buf,&net_message_len,sizeof(uint32_t));
    buf += sizeof(uint32_t);

    memcpy(buf,&(rpc->rpc_type),sizeof(char));
    buf += 2*sizeof(char);

    uint16_t net_rpc_id = htons(rpc->rpc_id);
    memcpy(buf,&net_rpc_id,sizeof(uint16_t));
    buf += sizeof(uint16_t);

    uint16_t net_senders_qp = htons(rpc->senders_qp);
    memcpy(buf,&net_senders_qp,sizeof(uint16_t));
    buf += sizeof(uint16_t);

    uint16_t net_senders_nid = htons(rpc->senders_nid);
    memcpy(buf,&net_senders_nid,sizeof(uint16_t));
    buf += sizeof(uint16_t);

    if( (uint64_t) buf % getAlignmentReq() ) {
        buf += ((uint64_t)buf % getAlignmentReq());
    }

    // FIXME: rpc is not sent "portably"
    // - could encode in string if need arises
    if( rpc->payload_len != 0 ) {
        memcpy(buf,rpc->payload,rpc->payload_len);
        buf += rpc->payload_len;
    }
#endif
}

// Does the reverse of pack(...)
RPCWithHeader unpackBufferToRPCLayer(char* buf)
{
    char* aNetworkBuffer = buf;
    uint32_t rpc_len;
    uint16_t senders_qp, senders_nid, rpc_id;
    char rpc_type;
#if defined(FLEXUS) || defined(QFLEX) || defined(ZSIM)
    rpc_len = *((uint32_t*)aNetworkBuffer);
    aNetworkBuffer += sizeof(uint32_t);

    rpc_type = *((char*)aNetworkBuffer); // no ntoh
    aNetworkBuffer += 2*sizeof(char);

    rpc_id = *((uint16_t*)aNetworkBuffer);
    aNetworkBuffer += sizeof(uint16_t);

    senders_qp = *((uint16_t*)aNetworkBuffer);
    aNetworkBuffer += sizeof(uint16_t);

    senders_nid = *((uint16_t*)aNetworkBuffer);
    aNetworkBuffer += sizeof(uint16_t);

    if( (uint64_t) aNetworkBuffer % getAlignmentReq() ) {
        aNetworkBuffer += ((uint64_t)aNetworkBuffer % getAlignmentReq());
    }

    return createRPCStruct( rpc_type, rpc_id, senders_qp, senders_nid,(rpc_len- calcTotalHeaderBytes()/*payload len*/), (buf + calcTotalHeaderBytes()/*offset to payload pointer*/));
#else // VM platform
    uint32_t* mlen_tmptr = (uint32_t*) aNetworkBuffer;
    rpc_len = ntohl(*mlen_tmptr);
    aNetworkBuffer += sizeof(uint32_t);

    char* mtype_tmptr = aNetworkBuffer;
    rpc_type = *mtype_tmptr;
    aNetworkBuffer += sizeof(char);

    uint16_t* rpcid_tmptr = (uint16_t*) aNetworkBuffer;
    rpc_id = ntohs(*rpcid_tmptr);
    aNetworkBuffer += sizeof(uint16_t);

    uint16_t* senderQP_tmptr = (uint16_t*) aNetworkBuffer;
    senders_qp = ntohs(*senderQP_tmptr);
    aNetworkBuffer += sizeof(uint16_t);

    uint16_t* senderNID_tmptr = (uint16_t*) aNetworkBuffer;
    senders_nid = ntohs(*senderNID_tmptr);
    aNetworkBuffer += sizeof(uint16_t);

    return createRPCStruct( rpc_type, rpc_id, senders_qp, senders_nid,(rpc_len- calcTotalHeaderBytes()/*payload len*/), (buf + calcTotalHeaderBytes()/*offset to payload pointer*/));
#endif
}

/*
void ctx_ready_timing(rpcNUMAContext* ctx) {
    ready_for_timing(ctx->kfd);
}

void ctx_disable_arm_timers(rpcNUMAContext* ctx) {
    kal_disable_arm_timers(ctx->kfd);
}
*/
uint64_t get_lbuf_size(rpcNUMAContext *ctx) {
  return ctx->getLBufSizeBytes();
}



