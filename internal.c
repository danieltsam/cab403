#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "data.h"

// Convert floor to integer
int floor_to_int(const char *floor) {
    if (floor[0] == 'B') {
        return -atoi(floor + 1);
    }
    return atoi(floor);
}

// Convert integer to floor string
void int_to_floor(int floor_int, char *floor_str, size_t size) {
    if (floor_int < 0) {
        snprintf(floor_str, size, "B%d", -floor_int);
    } else {
        snprintf(floor_str, size, "%d", floor_int);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Invalid format\n");
        return 1;
    }
    
    const char* car_name = argv[1];
    const char* operation = argv[2];
    
    char shm_name[100];
    snprintf(shm_name, sizeof(shm_name), "/car%s", car_name);
    
    int fd = shm_open(shm_name, O_RDWR, 0666);
    if (fd == -1) {
        printf("Unable to access car %s.\n", car_name);
        return 1;
    }
    
    car_shared_mem *shm = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        printf("Unable to access car %s.\n", car_name);
        close(fd);
        return 1;
    }
    
    close(fd);
    pthread_mutex_lock(&shm->mutex);
    
    if (strcmp(operation, "open") == 0) {
        shm->open_button = 1;
    } else if (strcmp(operation, "close") == 0) {
        shm->close_button = 1;
    } else if (strcmp(operation, "stop") == 0) {
        shm->emergency_stop = 1;
    } else if (strcmp(operation, "service_on") == 0) {
        shm->individual_service_mode = 1;
        shm->emergency_mode = 0;
    } else if (strcmp(operation, "service_off") == 0) {
        shm->individual_service_mode = 0;
    } else if (strcmp(operation, "up") == 0) {
        if (!shm->individual_service_mode) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation only allowed in service mode.\n");
            munmap(shm, sizeof(car_shared_mem));
            return 1;
        }
        if (strcmp(shm->status, "Open") == 0 || strcmp(shm->status, "Opening") == 0 || 
            strcmp(shm->status, "Closing") == 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while doors are open.\n");
            munmap(shm, sizeof(car_shared_mem));
            return 1;
        }
        if (strcmp(shm->status, "Between") == 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while elevator is moving.\n");
            munmap(shm, sizeof(car_shared_mem));
            return 1;
        }
        int current = floor_to_int(shm->current_floor);
        int_to_floor(current + 1, shm->destination_floor, sizeof(shm->destination_floor));
    } else if (strcmp(operation, "down") == 0) {
        if (!shm->individual_service_mode) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation only allowed in service mode.\n");
            munmap(shm, sizeof(car_shared_mem));
            return 1;
        }
        if (strcmp(shm->status, "Open") == 0 || strcmp(shm->status, "Opening") == 0 || 
            strcmp(shm->status, "Closing") == 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while doors are open.\n");
            munmap(shm, sizeof(car_shared_mem));
            return 1;
        }
        if (strcmp(shm->status, "Between") == 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while elevator is moving.\n");
            munmap(shm, sizeof(car_shared_mem));
            return 1;
        }
        int current = floor_to_int(shm->current_floor);
        int_to_floor(current - 1, shm->destination_floor, sizeof(shm->destination_floor));
    } else {
        pthread_mutex_unlock(&shm->mutex);
        printf("Invalid operation.\n");
        munmap(shm, sizeof(car_shared_mem));
        return 1;
    }
    
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);
    
    munmap(shm, sizeof(car_shared_mem));
    return 0;
}
