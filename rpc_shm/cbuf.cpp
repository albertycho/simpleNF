#include "cbuf.h"
#include <assert.h>
#include <cstring>

using namespace std; // don't yell at me, this is a cpp file

uint8_t* 
CircularReceiveBuffer::getBasePointer() const { 
    return basePointer;
}

uint8_t* 
CircularReceiveBuffer::getHeadAddress() const { 
    return (basePointer + (head*entryByteSize));
}
uint8_t* 
CircularReceiveBuffer::getTailAddress() const {
    return (basePointer + (tail*entryByteSize));
}

uint64_t
CircularReceiveBuffer::getHeadOffset() const {
    return (head*entryByteSize);
}
uint64_t 
CircularReceiveBuffer::getTailOffset() const {
    return (tail*entryByteSize);
}

void
CircularReceiveBuffer::incTail() { 
    assert( is_sender );
    tail++;
    if ( tail >= numEntries ) {
        tail = 0;
        buffer_sr = !buffer_sr;
    }
    if( tail == senders_head ) is_full = true;
    assert( sr_vector.at(tail) != buffer_sr );
}

void 
CircularReceiveBuffer::incHeadAfterMsgProcessed() {
    ++head;
    if( head >= numEntries ) {
        head = 0;
    }
}

void
CircularReceiveBuffer::incSendersHeadToFreeSpace(unsigned inc) {
    assert(is_sender);
    senders_head += inc;
    if( senders_head >= numEntries ) {
        senders_head = 0;
    }
    if( tail != senders_head ) is_full = false;
}

void 
CircularReceiveBuffer::zeroEntryNumber(size_t index) {
    size_t offset = index * entryByteSize;
    memset( (basePointer + offset) , 0, entryByteSize );
}

void 
CircularReceiveBuffer::writeSRMetadata(size_t entry) {
    sr_vector.at(entry) = buffer_sr;
}

size_t 
CircularReceiveBuffer::GetReservedMemoryBytes(void) const {
    return (numEntries * entryByteSize);
}

bool 
CircularReceiveBuffer::isEmpty() const {
    return ( !is_full && (senders_head == tail) );
}

size_t 
CircularReceiveBuffer::getBufferSize() const {
    size_t sz = numEntries;
    if( !isEmpty() ) {
        if( tail >= senders_head ) { // regular case, e.g., tail = 2, head = 0
            sz = (tail - senders_head); // tail points at a free slot, so don't need +1
        } else { // wraparound happened
            /* 1) indexes [senders_head,numEntries-1] are full */
            sz = (numEntries - senders_head);
             /* 2) ... and [0,tail] (recall tail is empty) */
            sz += (tail);
        }
    } 
    return sz;
}

/* Method only called from senders, checks how many free slots are available. */
size_t
CircularReceiveBuffer::getAvailSlots() const {
    if( !isEmpty() ) { 
        size_t currentSize = getBufferSize();
        return (numEntries - currentSize);
    } else return numEntries;
}
