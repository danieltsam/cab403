CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread
LDFLAGS = -lrt -lpthread

all: car controller call internal safety

car: car.c
	$(CC) $(CFLAGS) car.c -o car $(LDFLAGS)

controller: controller.c
	$(CC) $(CFLAGS) controller.c -o controller $(LDFLAGS)

call: call.c
	$(CC) $(CFLAGS) call.c -o call $(LDFLAGS)

internal: internal.c
	$(CC) $(CFLAGS) internal.c -o internal $(LDFLAGS)

safety: safety.c
	$(CC) $(CFLAGS) safety.c -o safety $(LDFLAGS)

clean:
	rm -f car controller call internal safety
	rm -f /dev/shm/car*

.PHONY: all clean
