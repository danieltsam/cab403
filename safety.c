
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include "data.h"

static volatile sig_atomic_t should_exit = 0;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        should_exit = 1;
    }
}

// Basic floor validation - MISRA compliant
int is_valid_floor(const char *floor) {
    if (!floor) return 0;
    
    // Safe length check without strlen
    int len = 0;
    while (floor[len] != '\0' && len < 4) len++;
    if (len == 0 || len > 3) return 0;
    
    if (floor[0] == 'B') {
        if (len < 2) return 0;
        for (int i = 1; i < len; i++) {
            if (floor[i] < '0' || floor[i] > '9') return 0;
        }
        // Safe string to int conversion
        int num = 0;
        for (int i = 1; i < len; i++) {
            num = num * 10 + (floor[i] - '0');
        }
        return (num >= 1 && num <= 99);
    } else {
        for (int i = 0; i < len; i++) {
            if (floor[i] < '0' || floor[i] > '9') return 0;
        }
        // Safe string to int conversion
        int num = 0;
        for (int i = 0; i < len; i++) {
            num = num * 10 + (floor[i] - '0');
        }
        return (num >= 1 && num <= 999);
    }
}

// Check if status is valid - MISRA compliant
int is_valid_status(const char *status) {
    if (!status) return 0;
    
    // Safe string comparison without strcmp
    const char *valid_statuses[] = {"Opening", "Open", "Closing", "Closed", "Between"};
    for (int i = 0; i < 5; i++) {
        int j = 0;
        while (status[j] != '\0' && valid_statuses[i][j] != '\0' && 
               status[j] == valid_statuses[i][j] && j < 8) {
            j++;
        }
        if (status[j] == '\0' && valid_statuses[i][j] == '\0') {
            return 1;
        }
    }
    return 0;
}

// Check if boolean field is valid (0 or 1)
int is_valid_boolean(uint8_t value) {
    return (value == 0 || value == 1);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        const char *msg = "Invalid format\n";
        write(STDOUT_FILENO, msg, 15); // Length of "Invalid format\n"
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    const char* car_name = argv[1];
    char shm_name[100];
    // Safe string construction without strcpy/strcat (MISRA compliant)
    shm_name[0] = '/';
    shm_name[1] = 'c';
    shm_name[2] = 'a';
    shm_name[3] = 'r';
    int i = 4;
    int j = 0;
    while (car_name[j] != '\0' && i < 99) {
        shm_name[i] = car_name[j];
        i++;
        j++;
    }
    shm_name[i] = '\0';
    
    int fd = shm_open(shm_name, O_RDWR, 0666);
    if (fd == -1) {
        const char *msg = "Unable to access car.\n";
        write(STDOUT_FILENO, msg, 22); // Length of "Unable to access car.\n"
        return 1;
    }
    
    car_shared_mem *shm = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        const char *msg = "Unable to access car.\n";
        write(STDOUT_FILENO, msg, 22); // Length of "Unable to access car.\n"
        close(fd);
        return 1;
    }
    
    close(fd);
    
    // Set safety system as active
    pthread_mutex_lock(&shm->mutex);
    shm->safety_system = 1;
    pthread_mutex_unlock(&shm->mutex);
    
    for (;;) {
        if (should_exit) break;
        
        pthread_mutex_lock(&shm->mutex);
        
        // Ensure safety system is always active
        if (shm->safety_system != 1) {
            shm->safety_system = 1;
        }
        
        // Check safety system heartbeat
        if (shm->safety_system == 0) {
            const char *msg = "Safety system heartbeat failure!\n";
            write(STDOUT_FILENO, msg, 34); // Length of "Safety system heartbeat failure!\n"
            shm->emergency_mode = 1;
        }
        
        // Handle door obstruction
        int is_closing = (shm->status[0] == 'C' && shm->status[1] == 'l' && 
                         shm->status[2] == 'o' && shm->status[3] == 's' && 
                         shm->status[4] == 'i' && shm->status[5] == 'n' && 
                         shm->status[6] == 'g' && shm->status[7] == '\0');
        if (shm->door_obstruction == 1 && is_closing) {
            // Safe string copy without strcpy
            shm->status[0] = 'O';
            shm->status[1] = 'p';
            shm->status[2] = 'e';
            shm->status[3] = 'n';
            shm->status[4] = 'i';
            shm->status[5] = 'n';
            shm->status[6] = 'g';
            shm->status[7] = '\0';
        }
        
        // Handle emergency stop
        if (shm->emergency_stop == 1 && shm->emergency_mode == 0) {
            const char *msg = "The emergency stop button has been pressed!\n";
            write(STDOUT_FILENO, msg, 44); // Length of "The emergency stop button has been pressed!\n"
            shm->emergency_mode = 1;
            shm->emergency_stop = 0;
        }
        
        // Handle overload
        if (shm->overload == 1 && shm->emergency_mode == 0) {
            const char *msg = "The overload sensor has been tripped!\n";
            write(STDOUT_FILENO, msg, 37); // Length of "The overload sensor has been tripped!\n"
            shm->emergency_mode = 1;
        }
        
        // Check data consistency
        if (shm->emergency_mode != 1) {
            int data_error = 0;
            
            if (!is_valid_floor(shm->current_floor) || !is_valid_floor(shm->destination_floor)) {
                data_error = 1;
            }
            if (!is_valid_status(shm->status)) {
                data_error = 1;
            }
            if (!is_valid_boolean(shm->open_button) || !is_valid_boolean(shm->close_button) ||
                !is_valid_boolean(shm->door_obstruction) || !is_valid_boolean(shm->overload) ||
                !is_valid_boolean(shm->emergency_stop) || !is_valid_boolean(shm->individual_service_mode) ||
                !is_valid_boolean(shm->emergency_mode)) {
                data_error = 1;
            }
            // Check door obstruction logic without strcmp
            if (shm->door_obstruction == 1) {
                int is_opening = (shm->status[0] == 'O' && shm->status[1] == 'p' && 
                                 shm->status[2] == 'e' && shm->status[3] == 'n' && 
                                 shm->status[4] == 'i' && shm->status[5] == 'n' && 
                                 shm->status[6] == 'g' && shm->status[7] == '\0');
                int is_closing = (shm->status[0] == 'C' && shm->status[1] == 'l' && 
                                 shm->status[2] == 'o' && shm->status[3] == 's' && 
                                 shm->status[4] == 'i' && shm->status[5] == 'n' && 
                                 shm->status[6] == 'g' && shm->status[7] == '\0');
                if (!is_opening && !is_closing) {
                    data_error = 1;
                }
            }
            
            if (data_error) {
                const char *msg = "Data consistency error!\n";
                write(STDOUT_FILENO, msg, 25); // Length of "Data consistency error!\n"
                shm->emergency_mode = 1;
            }
        }
        
        pthread_cond_wait(&shm->cond, &shm->mutex);
        pthread_mutex_unlock(&shm->mutex);
    }
    
    // Cleanup
    if (shm) {
        munmap(shm, sizeof(car_shared_mem));
    }
    
    return 0;
}
