all : fountain.o slice_queue.o datadiode-send.o datadiode-recv.o datadiode-recovery.o \
	datadiode-send datadiode-recv datadiode-recovery datadiode-syslog
fountain.o : fountain.c fountain.h
	cc -Wall -c fountain.c
slice_queue.o : slice_queue.c slice_queue.h
	cc -Wall -c slice_queue.c
datadiode-recovery.o : datadiode-recovery.c
	cc -Wall -c datadiode-recovery.c
datadiode-send.o : datadiode-send.c
	cc -Wall -c datadiode-send.c
datadiode-recv.o : datadiode-recv.c 
	cc -Wall -c datadiode-recv.c
datadiode-send:
	cc -Wall -o datadiode-send fountain.o datadiode-send.o
datadiode-recv:
	cc -Wall -o datadiode-recv datadiode-recv.o -lpthread
datadiode-recovery:
	cc -Wall -o datadiode-recovery datadiode-recovery.o fountain.o slice_queue.o
datadiode-syslog:
	cc -Wall -o datadiode-amplify-syslog datadiode-amplify-syslog.c
	cc -Wall -o datadiode-deamplify-syslog datadiode-deamplify-syslog.c
clean :
	rm -rf datadiode-send datadiode-recv datadiode-recovery
	rm -rf slice_queue.o datadiode-recovery.o fountain.o datadiode-send.o datadiode-recv.o 
	rm -rf datadiode-amplify-syslog datadiode-deamplify-syslog
