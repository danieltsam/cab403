#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include "data.h"

static car_shared_mem *shm = NULL;
static int shm_fd = -1;
static char shm_name[100];
static int delay_ms = 0;
static int controller_fd = -1;
static volatile int should_exit = 0;
static char car_name[64];
static char lowest_floor[8];
static char highest_floor[8];

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

// Send message with length prefix
void send_msg(int fd, const char *msg) {
    uint16_t len = strlen(msg);
    uint16_t net_len = htons(len);
    write(fd, &net_len, sizeof(net_len));
    write(fd, msg, len);
}

// Receive message with length prefix
char* recv_msg(int fd) {
    uint16_t net_len;
    if (read(fd, &net_len, sizeof(net_len)) <= 0) return NULL;
    uint16_t len = ntohs(net_len);
    char *msg = malloc(len + 1);
    if (read(fd, msg, len) <= 0) {
        free(msg);
        return NULL;
    }
    msg[len] = '\0';
    return msg;
}

void signal_handler(int sig) {
    if (sig == SIGINT) {
        should_exit = 1;
        if (shm) {
            pthread_mutex_lock(&shm->mutex);
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
        }
    }
}

void init_shared_memory() {
    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    int created = (shm_fd != -1);
    if (!created) {
        shm_fd = shm_open(shm_name, O_RDWR, 0666);
        if (shm_fd == -1) {
            perror("shm_open");
            exit(1);
        }
    } else {
        if (ftruncate(shm_fd, sizeof(car_shared_mem)) == -1) {
            perror("ftruncate");
            exit(1);
        }
    }

    shm = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    if (created) {
        // Initialize mutex and condition variable
        pthread_mutexattr_t mutattr;
        pthread_mutexattr_init(&mutattr);
        pthread_mutexattr_setpshared(&mutattr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&shm->mutex, &mutattr);
        pthread_mutexattr_destroy(&mutattr);

        pthread_condattr_t condattr;
        pthread_condattr_init(&condattr);
        pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&shm->cond, &condattr);
        pthread_condattr_destroy(&condattr);

        // Initialize default values
        strcpy(shm->current_floor, lowest_floor);
        strcpy(shm->destination_floor, lowest_floor);
        strcpy(shm->status, "Closed");
        shm->open_button = 0;
        shm->close_button = 0;
        shm->safety_system = 0;
        shm->door_obstruction = 0;
        shm->overload = 0;
        shm->emergency_stop = 0;
        shm->individual_service_mode = 0;
        shm->emergency_mode = 0;
    }
}

int connect_to_controller() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    inet_aton("127.0.0.1", &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(sockfd);
        return -1;
    }

    char reg_msg[100];
    snprintf(reg_msg, sizeof(reg_msg), "CAR %s %s %s", car_name, lowest_floor, highest_floor);
    send_msg(sockfd, reg_msg);

    return sockfd;
}

void* controller_thread(void *arg) {
    while (!should_exit) {
        if (controller_fd == -1) {
            if (shm->safety_system == 1) {
                controller_fd = connect_to_controller();
                if (controller_fd != -1) {
                    char status_msg[100];
                    snprintf(status_msg, sizeof(status_msg), "STATUS %s %s %s", 
                            shm->status, shm->current_floor, shm->destination_floor);
                    send_msg(controller_fd, status_msg);
                }
            }
            if (controller_fd == -1) {
                usleep(delay_ms * 1000);
                continue;
            }
        }

        char *msg = recv_msg(controller_fd);
        if (!msg) {
            close(controller_fd);
            controller_fd = -1;
            continue;
        }

        if (strncmp(msg, "FLOOR ", 6) == 0) {
            pthread_mutex_lock(&shm->mutex);
            strcpy(shm->destination_floor, msg + 6);
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
        }
        free(msg);
    }
    return NULL;
}

void move_towards_destination() {
    int current = floor_to_int(shm->current_floor);
    int dest = floor_to_int(shm->destination_floor);

    if (current == dest) {
        // Open doors sequence
        strcpy(shm->status, "Opening");
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
        usleep(delay_ms * 1000);
        pthread_mutex_lock(&shm->mutex);

        strcpy(shm->status, "Open");
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
        usleep(delay_ms * 1000);
        pthread_mutex_lock(&shm->mutex);

        strcpy(shm->status, "Closing");
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
        usleep(delay_ms * 1000);
        pthread_mutex_lock(&shm->mutex);

        strcpy(shm->status, "Closed");
        pthread_cond_broadcast(&shm->cond);
    } else {
        // Move towards destination
        strcpy(shm->status, "Between");
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
        usleep(delay_ms * 1000);
        pthread_mutex_lock(&shm->mutex);

        if (current < dest) {
            int_to_floor(current + 1, shm->current_floor, sizeof(shm->current_floor));
        } else {
            int_to_floor(current - 1, shm->current_floor, sizeof(shm->current_floor));
        }
        strcpy(shm->status, "Closed");
        pthread_cond_broadcast(&shm->cond);
    }
}

void handle_buttons() {
    if (shm->open_button == 1) {
        shm->open_button = 0;
        if (strcmp(shm->status, "Open") == 0) {
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            usleep(delay_ms * 1000);
            pthread_mutex_lock(&shm->mutex);
            strcpy(shm->status, "Closing");
        } else if (strcmp(shm->status, "Closing") == 0 || strcmp(shm->status, "Closed") == 0) {
            strcpy(shm->status, "Opening");
        }
    }
    
    if (shm->close_button == 1) {
        shm->close_button = 0;
        if (strcmp(shm->status, "Open") == 0) {
            strcpy(shm->status, "Closing");
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Invalid format\n");
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    strcpy(car_name, argv[1]);
    strcpy(lowest_floor, argv[2]);
    strcpy(highest_floor, argv[3]);
    delay_ms = atoi(argv[4]);

    snprintf(shm_name, sizeof(shm_name), "/car%s", car_name);
    init_shared_memory();

    pthread_t controller_tid;
    pthread_create(&controller_tid, NULL, controller_thread, NULL);

    while (!should_exit) {
        pthread_mutex_lock(&shm->mutex);

        if (shm->emergency_mode == 1) {
            pthread_mutex_unlock(&shm->mutex);
            usleep(100000);
            continue;
        }

        handle_buttons();

        if (shm->individual_service_mode == 0) {
            // Normal operation
            if (strcmp(shm->current_floor, shm->destination_floor) != 0) {
                move_towards_destination();
            }
        } else {
            // Individual service mode - manual control
            if (strcmp(shm->current_floor, shm->destination_floor) != 0 &&
                strcmp(shm->status, "Closed") == 0) {
                move_towards_destination();
            }
        }

        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);

        // Send status update
        if (controller_fd != -1) {
            char status_msg[100];
            snprintf(status_msg, sizeof(status_msg), "STATUS %s %s %s",
                    shm->status, shm->current_floor, shm->destination_floor);
            send_msg(controller_fd, status_msg);
        }

        // Small delay to prevent busy waiting
        usleep(10000);
    }

    if (controller_fd != -1) {
        close(controller_fd);
    }
    
    if (shm) {
        munmap(shm, sizeof(car_shared_mem));
    }
    if (shm_fd != -1) {
        close(shm_fd);
    }
    shm_unlink(shm_name);
    
    return 0;
}