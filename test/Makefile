CFLAGS=-pthread
TESTERS=test-call test-internal test-safety test-car-1 test-car-2 test-car-3 test-car-4 test-car-5 test-controller-1 test-controller-2 test-controller-3 test-controller-4

testers: $(TESTERS)
display-cars: display-cars.c
	$(CC) -o display-cars display-cars.c -lncurses -lm -pthread
clean:
	rm -f $(TESTERS) display-cars
.PHONY: testers clean
