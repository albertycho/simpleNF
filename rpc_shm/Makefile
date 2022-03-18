CFLAGS  += -std=c++11
#SON_INC	:= -I${LIBSONUMA_DIR}
SON_INC	:= -I${LIBZSIMNIC_DIR}

OBJS 	:= rpc.o cbuf.o
#INCS	:= sonuma.h
INCS	:= zsim_nic_defines.h

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CFLAGS) $(SON_INC)

rpc: $(OBJS) 
	ar ru rpc.a $^

.PHONY: clean
clean:
	rm -f ./*.o *~ core *.a
