OBJS=lists.o bitops.o resource.o xmalloc.o mempool.o slab.o md5.o

ifdef IPV6
OBJS += ipv6.o
else
OBJS += ipv4.o
endif

include $(TOPDIR)/Rules
