#ifndef _LIBRPCNUMA_H
#define _LIBRPCNUMA_H

// from Kalia
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
//#include <libmemcached/memcached.h>
//#include "sonuma.h"
#include "zsim_nic_defines.hpp"


// Msutherl: Change these for different experiments
#define MSGS_PER_PAIR 256
#define RPC_MAX_PAYLOAD 512

#ifdef __cplusplus // Msutherl: hacking
#include <vector>
#include <iostream>

#include "cbuf.h"

template< typename T >
class StatsContainer {
    private:
        std::vector< T > underlyingContainer;

    public:
        void append(T& appendMe);
        T& get(size_t idx);
        void writeToFile(std::string fname);
        void readFromFile(std::string fname, size_t num_elements);
};

#include "statContainer.tpp"

/* Local, networking-exposed buffer */
class NIExposedBuffer {
    public:
        uint32_t* underlyingBuffer;

    NIExposedBuffer(uint32_t** aPremappedBuffer) : underlyingBuffer(*aPremappedBuffer) { }
    uint32_t& operator[] (const int idx) {
        return underlyingBuffer[idx];
    }
    uint32_t* getUnderlyingAddress(const int byteOffset) {
        return (underlyingBuffer + byteOffset);
    }

    std::ostream& operator<<(std::ostream& os) {
        for(size_t i = 0; i < 64; i++) { // FIXME: currently size is HERD hardcoded
            os << "0x" << std::hex << *(underlyingBuffer + i) << " " << std::dec; 
        }
        os << std::endl;
        return os;
    }
};

class SendersCopyOfCRB : public CircularReceiveBuffer {
};

/* Abstract type for any VIA-style architecture */
class AbstractQP_T {
    public:
        unsigned int qp_id;
        unsigned int getQPID() { return this->qp_id; }

        AbstractQP_T(unsigned int anID) : qp_id(anID) { }
};

class soNUMAQP_T : AbstractQP_T {
    public:
        rmc_wq_t* wq;
        rmc_cq_t* cq;

        soNUMAQP_T(unsigned int anID, rmc_wq_t** ptrToWQ, rmc_cq_t** ptrToCQ) :
            AbstractQP_T(anID), wq( *ptrToWQ ), cq( *ptrToCQ ) { }
};

class ibQP_T : AbstractQP_T {
    // Msutherl: I'll never implement this UNTIL WE BUY AN IB CLUSTER.
}; 

class soNUMAMessagingDomain {
    public:
        // actual definitions
        uint32_t* recv_slots;
        /* Add stuff for explicit RPCValet */
        ctx_entry_t ctx_struct;
        send_buf_entry_t *send_buf;
        send_buf_management_t *send_ctrl;
        uint64_t per_node_chunk;
};

class rpcNUMAContext {
    public:
        std::vector<soNUMAQP_T*> qps;
        soNUMAMessagingDomain* msg_domain;

        std::vector< NIExposedBuffer* > app_buffers;
        std::vector< CircularReceiveBuffer* > circ_buffers;

        StatsContainer<double> rpcServiceTimeStats;

        int kfd;
        uint8_t* pgas;
        unsigned int nodeID;
        unsigned int totalNodeCount;
        unsigned int threadsPerClient;
        uint32_t rpc_id;
        uint64_t lbuf_size_bytes;

        bool isSenderCtx;
        unsigned long long poll_count;

        NIExposedBuffer* getPtrToExposedBuffer(const size_t idx) const {
            return app_buffers.at(idx);
        }

        unsigned int getNumClients() const { 
            return totalNodeCount - 1;
        }

        size_t getCBufIdx(unsigned cl_nid, unsigned cl_tid, unsigned sr_thread) const ;
        uint32_t getRPCID() { return rpc_id++; } 
        uint64_t getLBufSizeBytes() const { return lbuf_size_bytes; }
        /* TODO: Add function to create new NIExposedBuffer* when thread registers it*/
        /* TODO: Add function to create new soNUMAQP_T* when thread registers it*/
};
#else
// C, forward declare some stuff
typedef struct rpcNUMAContext rpcNUMAContext;
typedef struct soNUMAMessagingDomain soNUMAMessagingDomain;
typedef struct AbstractQP_T AbstractQP_T;
typedef struct soNUMAQP_T soNUMAQP_T;
typedef struct ibQP_T ipQP_T;
typedef struct NIExposedBuffer NIExposedBuffer;
typedef struct CircularReceiveBuffer CircularReceiveBuffer;
#endif

typedef struct {
        // field in header              // bytes in encapsulated buffer
        uint32_t rpc_len;               // = 4B
        char rpc_type;                  // = 1B
        uint16_t rpc_id;                // = 2B
        uint16_t senders_qp;            // = 2B
        uint16_t senders_nid;            // = 2B
        // payload pointer
        char* payload;                  // size defined by (rpc_len) - header bytes 

        ///// NOT SENT ON WIRE
        uint32_t payload_len;

} RPCWithHeader;
/* Functions to interact w. RPC buffers, to encapsulate/de-encapsulate */
// create this software structure in rpc layer
RPCWithHeader createRPCStruct(char aType, uint16_t anRPCId, uint16_t aQPId, uint16_t aNID, uint32_t aPayloadLen, char* aPayloadPointer);
// take this software and pack it into an rpc buffer to send to RMC layer
void packPayload(RPCWithHeader* r,char* buf);
RPCWithHeader unpackBufferToRPCLayer(char* aRawBuffer);

#ifdef __cplusplus
extern "C" {
#endif

/* Boolean function that determines whether polled location is valid */
typedef bool (pollFunction)(uint8_t** poll_loc);
typedef void (herdCallback)(uint8_t* slot_ptr, rpcArg_t* rpc_arguments);

/* Messaging library functions, can swap in any underlying impl
 * (e.g., WRITE+SEND, SEND+RECV x2, SRx2 + LB)
 */
rpcNUMAContext* createRPCNUMAContext(int dev_fd, unsigned int aNodeID, unsigned int totalNodes, unsigned int qp_start, unsigned int qp_end, size_t context_local_pages, bool isSendingCtx, unsigned int serverThreadCount, unsigned int clientThreadCount, unsigned int packet_size );
void destroyRPCNUMAContext(/*...*/);
NIExposedBuffer* registerNewLocalBuffer(rpcNUMAContext* ctx, uint32_t** tptr,size_t buf_size,size_t tidx);
soNUMAQP_T* registerNewSONUMAQP(rpcNUMAContext* ctx, size_t tidx);
void ctx_ready_timing(rpcNUMAContext* ctx);
void ctx_disable_arm_timers(rpcNUMAContext* ctx);

void notify_done_to_zsim();
void monitor_client_done(bool ** client_done);
void register_done_sending(bool ** done_sending);
void notify_service_start(int count);
void notify_service_end(int count);
void notify_fruitless_cq_check(int count);
void timestamp(int count);


/* Receive and send RPC endpoint functions */
void receiveRPCResponse(rpcNUMAContext* rpcContext, herdCallback* cb, unsigned int cl_nid , unsigned int cl_tid, unsigned int serv_tid);
void receiveRPCRequest(rpcNUMAContext* rpcContext, herdCallback* cb, void* pointer_to_data_store, unsigned int serv_nid, unsigned int serv_qp_id, uint16_t* source_node_id,uint16_t* source_qp_id);
RPCWithHeader receiveRPCResponse_flex(rpcNUMAContext* rpcContext, unsigned int cl_nid , unsigned int cl_tid, unsigned int serv_tid);
RPCWithHeader receiveRPCRequest_flex(rpcNUMAContext* rpcContext, void* pointer_to_data_store, unsigned int serv_nid, unsigned int serv_qp_id, uint16_t* source_node_id,uint16_t* source_qp_id);
void sendToNode(rpcNUMAContext* rpcContext, NIExposedBuffer* messageBuffer, size_t messageByteSize, unsigned int destNode, unsigned int clientFrom, unsigned int qpTarget, unsigned int sendQP, bool send_qp_terminate);
void sendToNode_flex(rpcNUMAContext* rpcContext, NIExposedBuffer* messageBuffer, size_t messageByteSize, unsigned int destNode, unsigned int clientFrom, unsigned int qpTarget, unsigned int sendQP, bool send_qp_terminate,char* raw_payload_data, bool skipcpy,unsigned int rpc_send_count);
void remoteBufferWrite(rpcNUMAContext* rpcContext, NIExposedBuffer* messageBuffer, size_t messageByteSize, unsigned int destNode, unsigned int clientFrom, unsigned int clientThread, int threadOnDestNode );
void do_Recv_flex( rpcNUMAContext* rpcContext, unsigned int cl_nid , unsigned int cl_tid, unsigned int serv_tid,char* rbuf_slot_ptr,unsigned int freeSizeBytes);

void sendToNode_zsim(rpcNUMAContext* rpcContext, NIExposedBuffer* messageBuffer, size_t messageByteSize, unsigned int destNode, unsigned int clientFrom, unsigned int qpTarget, unsigned int sendQP, bool send_qp_terminate,char* raw_payload_data, bool skipcpy,unsigned int rpc_send_count);
RPCWithHeader receiveRPCRequest_zsim_l3fwd(rpcNUMAContext* rpcContext, unsigned int serv_nid, unsigned int serv_qp_id, uint16_t* source_node_id,uint16_t* source_qp_id,bool* client_done, bool* done_sending);
void do_Recv_zsim( rpcNUMAContext* rpcContext, unsigned int cl_nid , unsigned int cl_tid, unsigned int serv_tid,char* rbuf_slot_ptr,unsigned int freeSizeBytes);


NIExposedBuffer* trampolineToBufConstructor(unsigned int);
void trampolineToBufDestructor(NIExposedBuffer*);
uint8_t* trampolineToGetUnderlyingAddress(NIExposedBuffer*,const int);
NIExposedBuffer* trampolineToGetNIExposedBuffer(rpcNUMAContext* ctx,const size_t idx);

/* Functions to access 1 sided request buffers */
void setupCircularBuffers(rpcNUMAContext* ctx,unsigned int nidToCreateOn,size_t entryByteSize,size_t numEntries,size_t aNumberofSenders, size_t aNumberOfReceivers,bool are_sendersCopies );
void flowControlUpdateSendersHead(CircularReceiveBuffer* buf, unsigned int inc);
uint8_t* pollOnBufferNumber(rpcNUMAContext*, size_t, pollFunction*);
void zeroOutRBufHead(rpcNUMAContext* ctx, size_t bufferNumber);
void incrementBufferHead(rpcNUMAContext* , size_t);
size_t getBufferHeadIndex(rpcNUMAContext* ctx, size_t bufferNumber);
size_t getBufferIDX(rpcNUMAContext* ctx, unsigned int cl_nid, unsigned int cl_tid, unsigned int serv_tid);

/* Functions to write stuff to underlying stat container (in C++) */
void addToContainer(rpcNUMAContext* ctx, double element);
void writeContainerToFile(rpcNUMAContext* ctx, const char* fname);
void readContainerFromFile(rpcNUMAContext* ctx, const char* fname, size_t num_elements);

/* Helper functions to access member variables from C */
uint64_t get_lbuf_size(rpcNUMAContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _LIBRPCNUMA_H */
