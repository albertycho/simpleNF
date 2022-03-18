#ifndef _CBUF_H
#define _CBUF_H
#include <vector>
#include <cstdint>

class CircularReceiveBuffer {
    private:
        bool is_full;
        bool isEmpty() const;
    public:
        uint8_t* basePointer;
        std::size_t entryByteSize;
        std::size_t numEntries;
        std::size_t head;
        std::size_t tail;
        
        bool buffer_sr;
        std::vector<bool> sr_vector;

        // for senders copy
        std::size_t senders_head;
        bool is_sender;

        CircularReceiveBuffer(uint8_t** aBase, std::size_t aSize, std::size_t aNumEntries, bool senderCopy) :
            is_full(false), basePointer(*aBase), entryByteSize(aSize), numEntries(aNumEntries), head(0), tail(0), buffer_sr(false), sr_vector(aNumEntries,true), senders_head(0), is_sender(senderCopy) { }
        virtual ~CircularReceiveBuffer() { } 

        /* Getters/setters for offsets and indexes */
        uint8_t* getBasePointer() const;
        uint8_t* getHeadAddress() const;
        uint8_t* getTailAddress() const;
        uint64_t getHeadOffset() const;
        uint64_t getTailOffset() const;
        std::size_t getHeadIdxValue() { return head; }
        std::size_t getTailIdxValue() { return tail; }
        void incTail();

        /* Access via offset */
        uint8_t& operator[] (const int offset) {
            return *(basePointer + offset);
        }

        /* Other relevant communication API */
        void incHeadAfterMsgProcessed();
        void incSendersHeadToFreeSpace(unsigned inc);
        std::size_t getBufferSize() const ;
        std::size_t getAvailSlots() const ;
        void zeroEntryNumber(std::size_t index);
        void writeSRMetadata(std::size_t entry);

        /* Misc */
        std::size_t GetReservedMemoryBytes(void) const;
};

#endif /* _CBUF_H */
