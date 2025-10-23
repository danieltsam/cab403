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
#define _POSIX_C_SOURCE 199309L
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include "data.h"

#define MILLISECOND 1000

static car_shared_mem *shm = NULL;
static int shm_fd = -1;
static char shm_name[100];
static int delay_ms = 0;
static int controller_fd = -1;
static volatile int should_exit = 0;
static volatile int dest_updated = 0; // Flag to track when destination changes
static pthread_mutex_t controller_mutex = PTHREAD_MUTEX_INITIALIZER;
static char car_name[64];
static char lowest_floor[8];
static char highest_floor[8];

// Function declarations
void handle_buttons();
void move_towards_destination();
void move_one_floor_towards(char *current_floor, const char *destination_floor, size_t max_len);
void execute_door_cycle();
void increment_time(struct timespec *t, long ms);
int my_usleep(__useconds_t usec);

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

// Send status update to controller
void send_status_update() {
    if (controller_fd != -1) {
        char status_msg[100];
        snprintf(status_msg, sizeof(status_msg), "STATUS %s %s %s",
                shm->status, shm->current_floor, shm->destination_floor);
        send_msg(controller_fd, status_msg);
    }
}

// Send message to controller
void send_message(int fd, const char *message) {
    if (fd != -1) {
        send_msg(fd, message);
    }
}

// Check if floor is within valid range
int is_in_range(const char *floor) {
    int floor_int = floor_to_int(floor);
    return (floor_int >= -99 && floor_int <= 999);
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
            pthread_mutex_lock(&shm->mutex);
            int should_connect = (shm->individual_service_mode == 0 && shm->emergency_mode == 0 && shm->safety_system == 1);
            pthread_mutex_unlock(&shm->mutex);
            
            if (should_connect) {
                controller_fd = connect_to_controller();
                if (controller_fd != -1) {
                    send_status_update();
                }
            }
            if (controller_fd == -1) {
                my_usleep(delay_ms * MILLISECOND);
                continue;
            }
        }

        // Use select() with timeout for non-blocking communication
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(controller_fd, &read_fds);
        struct timeval timeout = {0, delay_ms * 1000};  // timeout = delay_ms
        int ready = select(controller_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready > 0) {
            char *msg = recv_msg(controller_fd);
            if (!msg) {
                close(controller_fd);
                controller_fd = -1;
                continue;
            }

            if (strncmp(msg, "FLOOR ", 6) == 0) {
                char floor[8];
                sscanf(msg + 6, "%s", floor);
                pthread_mutex_lock(&shm->mutex);
                if (is_in_range(floor)) {
                    strncpy(shm->destination_floor, floor, sizeof(shm->destination_floor) - 1);
                    shm->destination_floor[sizeof(shm->destination_floor) - 1] = '\0';
                    dest_updated = 1; // Set flag to indicate destination changed
                    pthread_cond_broadcast(&shm->cond);
                }
                pthread_mutex_unlock(&shm->mutex);
            }
            free(msg);
        }
    }
    return NULL;
}

void *main_operation_thread(void *arg) {
    (void)arg;
    struct timespec last_safety_check;
    clock_gettime(CLOCK_MONOTONIC, &last_safety_check);
    
    while (!should_exit) {
        // Safety system heartbeat check based on actual time
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last_safety_check.tv_sec) * 1000 + 
                         (now.tv_nsec - last_safety_check.tv_nsec) / 1000000;
        
        if (elapsed_ms >= delay_ms) {
            last_safety_check = now;
            
            pthread_mutex_lock(&shm->mutex);
            // Only check safety system if connected and not in emergency mode or individual service mode
            if (controller_fd != -1 && shm->individual_service_mode == 0 && shm->emergency_mode == 0) {
                if (shm->safety_system == 1) {
                    shm->safety_system = 2;
                    pthread_cond_broadcast(&shm->cond);
                } else if (shm->safety_system == 2) {
                    shm->safety_system = 3;
                    pthread_cond_broadcast(&shm->cond);
                } else if (shm->safety_system >= 3) {
                    printf("Safety system disconnected! Entering emergency mode.\n");
                    shm->emergency_mode = 1;
                    pthread_cond_broadcast(&shm->cond);
                    pthread_mutex_unlock(&shm->mutex);
                    pthread_mutex_lock(&controller_mutex);
                    if (controller_fd != -1) {
                        send_message(controller_fd, "EMERGENCY");
                        close(controller_fd);
                        controller_fd = -1;
                    }
                    pthread_mutex_unlock(&controller_mutex);
                    pthread_mutex_lock(&shm->mutex);
                }
            }
            pthread_mutex_unlock(&shm->mutex);
        }
        
        pthread_mutex_lock(&shm->mutex);
        int is_individual_mode = shm->individual_service_mode;
        int is_emergency = shm->emergency_mode;
        int current_status_is_closed = (strcmp(shm->status, "Closed") == 0);
        pthread_mutex_unlock(&shm->mutex);
        
        // Handle buttons (handles doors in individual service mode)
        if (is_individual_mode || !is_emergency) {
            handle_buttons();
        }
        
        pthread_mutex_lock(&shm->mutex);
        
        // If handle_buttons changed the status, skip the rest of this iteration
        if (current_status_is_closed && strcmp(shm->status, "Closed") != 0) {
            pthread_mutex_unlock(&shm->mutex);
            continue;
        }
        
        // Handle mode changes
        if (shm->individual_service_mode == 1) {
            if (controller_fd != -1) {
                pthread_mutex_unlock(&shm->mutex);
                pthread_mutex_lock(&controller_mutex);
                send_message(controller_fd, "INDIVIDUAL SERVICE");
                close(controller_fd);
                controller_fd = -1;
                pthread_mutex_unlock(&controller_mutex);
                pthread_mutex_lock(&shm->mutex);
            }
            
            // Handle manual movement in individual service mode - floor by floor
            if (strcmp(shm->status, "Closed") == 0 && strcmp(shm->current_floor, shm->destination_floor) != 0) {
                if (!is_in_range(shm->destination_floor)) {
                    strncpy(shm->destination_floor, shm->current_floor, sizeof(shm->destination_floor) - 1);
                    pthread_mutex_unlock(&shm->mutex);
                    continue;
                }
                
                strcpy(shm->status, "Between");
                pthread_cond_broadcast(&shm->cond);
                pthread_mutex_unlock(&shm->mutex);
                
                my_usleep(delay_ms * MILLISECOND);
                
                pthread_mutex_lock(&shm->mutex);
                move_one_floor_towards(shm->current_floor, shm->destination_floor, sizeof(shm->current_floor));
                
                // Check if we've arrived at destination
                if (strcmp(shm->current_floor, shm->destination_floor) == 0) {
                    strcpy(shm->status, "Closed");
                    pthread_cond_broadcast(&shm->cond);
                    pthread_mutex_unlock(&shm->mutex);
                } else {
                    // Still moving, keep status as Between
                    pthread_mutex_unlock(&shm->mutex);
                }
            } else {
                pthread_mutex_unlock(&shm->mutex);
                my_usleep(1 * MILLISECOND);
            }
            continue;
        }
        
        if (shm->emergency_mode == 1) {
            pthread_mutex_unlock(&shm->mutex);
            continue;
        }
        
        // Normal operation
        if (strcmp(shm->status, "Closed") == 0) {
            int cmp = strcmp(shm->current_floor, shm->destination_floor);
            
            if (cmp == 0 && dest_updated) {
                // Controller sent us to current floor - open doors
                dest_updated = 0;
                pthread_mutex_unlock(&shm->mutex);
                execute_door_cycle();
            } else if (cmp != 0) {
                // Change status to between to start the actual journey
                strcpy(shm->status, "Between");
                pthread_cond_broadcast(&shm->cond);
                pthread_mutex_unlock(&shm->mutex);
                send_status_update(); // status between ... message

                // Loop until we get to our destination
                while(strcmp(shm->current_floor, shm->destination_floor) != 0 && !should_exit) {
                    if(strcmp(shm->status, "Between") == 0){
                        my_usleep(delay_ms * MILLISECOND);
                        pthread_mutex_lock(&shm->mutex);
                        // Check if we should still be moving i.e. not emergency not service
                        if (shm->emergency_mode == 0 && strcmp(shm->status, "Between") == 0){
                            move_one_floor_towards(shm->current_floor, shm->destination_floor, sizeof(shm->current_floor));
                            // Check if we have arrived 
                            if (strcmp(shm->current_floor, shm->destination_floor) == 0) {
                                // Destination has been reached and we need to unlock and start the door sequence
                                dest_updated = 0; // Clear flag
                                pthread_mutex_unlock(&shm->mutex);
                                execute_door_cycle();
                                break; // No longer in the movement loop leave it
                            } else {
                                // Not at the floor send a status update
                                pthread_mutex_unlock(&shm->mutex);
                                send_status_update();
                            }
                        } else {
                            pthread_mutex_unlock(&shm->mutex);
                            break;
                        }
                    } else {
                        break;
                    }
                } 

            } else {
                // Current floor is the destination floor so we do not need to do anything until given a new dest
                pthread_mutex_unlock(&shm->mutex);
                my_usleep(1 * MILLISECOND);  // Check frequently for button presses
            }
        } else {
            pthread_mutex_unlock(&shm->mutex);
            my_usleep(1 * MILLISECOND);
        }
    }
    
    return NULL;
}

void move_one_floor_towards(char *current_floor, const char *destination_floor, size_t max_len) {
    int current = floor_to_int(current_floor);
    int dest = floor_to_int(destination_floor);
    
    if (current < dest) {
        int_to_floor(current + 1, current_floor, max_len);
    } else if (current > dest) {
        int_to_floor(current - 1, current_floor, max_len);
    }
}

void move_towards_destination() {
    int current = floor_to_int(shm->current_floor);
    int dest = floor_to_int(shm->destination_floor);

    if (current == dest) {
        // Start door opening sequence
        strcpy(shm->status, "Opening");
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
        send_status_update();
        pthread_mutex_lock(&shm->mutex);
    } else {
        // Move towards destination
        strcpy(shm->status, "Between");
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
        send_status_update();
        my_usleep(delay_ms * MILLISECOND);
        pthread_mutex_lock(&shm->mutex);

        if (current < dest) {
            int_to_floor(current + 1, shm->current_floor, sizeof(shm->current_floor));
        } else {
            int_to_floor(current - 1, shm->current_floor, sizeof(shm->current_floor));
        }
        
        // Check if we've arrived at destination
        if (floor_to_int(shm->current_floor) == dest) {
            strcpy(shm->status, "Closed");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            pthread_mutex_lock(&shm->mutex);
        } else {
            // Still moving, keep Between status
            strcpy(shm->status, "Between");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            pthread_mutex_lock(&shm->mutex);
        }
    }
}

void handle_buttons() {
    pthread_mutex_lock(&shm->mutex);
    
    // In individual service mode, handle buttons immediately
    if (shm->individual_service_mode == 1) {
        if (shm->close_button == 1 && strcmp(shm->status, "Open") == 0) {
            shm->close_button = 0;
            strcpy(shm->status, "Closing");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            
            my_usleep(delay_ms * MILLISECOND);
            
            pthread_mutex_lock(&shm->mutex);
            if(strcmp(shm->status, "Closing") == 0) {
                strcpy(shm->status, "Closed");
                pthread_cond_broadcast(&shm->cond);
            }
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            return;
        }
        
        if (shm->open_button == 1 && strcmp(shm->status, "Closed") == 0) {
            shm->open_button = 0;
            strcpy(shm->status, "Opening");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            
            my_usleep(delay_ms * MILLISECOND);
            
            pthread_mutex_lock(&shm->mutex);
            if(strcmp(shm->status, "Opening") == 0) {
                strcpy(shm->status, "Open");
                pthread_cond_broadcast(&shm->cond);
            }
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            return;
        }
        
        pthread_mutex_unlock(&shm->mutex);
        return;
    }
    
    // Normal mode - close button has highest priority when door is Open
    if (shm->close_button == 1 && strcmp(shm->status, "Open") == 0) {
        shm->close_button = 0;
        strcpy(shm->status, "Closing");
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
        send_status_update();
        
        my_usleep(delay_ms * MILLISECOND);
        
        pthread_mutex_lock(&shm->mutex);
        if(strcmp(shm->status, "Closing") == 0) {
            strcpy(shm->status, "Closed");
            pthread_cond_broadcast(&shm->cond);
        }
        pthread_mutex_unlock(&shm->mutex);
        send_status_update();
        return;
    }

    // Normal mode - open button when at destination floor OR when no controller connected
    if(shm->open_button == 1 && strcmp(shm->status, "Closed") == 0 && 
       (strcmp(shm->current_floor, shm->destination_floor) == 0 || controller_fd == -1)) {
        pthread_mutex_unlock(&shm->mutex);
        execute_door_cycle();
        return;
    }
    
    pthread_mutex_unlock(&shm->mutex);
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

    // Create main operation thread
    pthread_t main_operation_tid;
    pthread_create(&main_operation_tid, NULL, main_operation_thread, NULL);
    
    // Wait for threads to complete
    pthread_join(controller_tid, NULL);
    pthread_join(main_operation_tid, NULL);

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

int my_usleep(__useconds_t usec) {
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    return nanosleep(&ts, NULL);
}

void increment_time(struct timespec *t, long ms) {
    t->tv_sec  += ms / 1000;
    t->tv_nsec += (ms % 1000) * 1000000L;
    if (t->tv_nsec >= 1000000000L) {
        t->tv_sec++;
        t->tv_nsec -= 1000000000L;
    }
}

void execute_door_cycle(void) {
    struct timespec cycle_start;
    clock_gettime(CLOCK_MONOTONIC, &cycle_start);

    // --- Opening phase ---
    pthread_mutex_lock(&shm->mutex);
    shm->open_button = 0;
    strcpy(shm->status, "Opening");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);
    send_status_update();

    // --- Open at t=delay_ms ---
    struct timespec open_time = cycle_start;
    increment_time(&open_time, delay_ms);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &open_time, NULL);

    pthread_mutex_lock(&shm->mutex);
    if(strcmp(shm->status, "Opening") == 0) {
        strcpy(shm->status, "Open");
        pthread_cond_broadcast(&shm->cond);
    }
    pthread_mutex_unlock(&shm->mutex);
    send_status_update();

    // --- Wait in Open state until close_button or t=2*delay_ms ---
    struct timespec close_time = cycle_start;
    increment_time(&close_time, 2 * delay_ms);

    while (1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        pthread_mutex_lock(&shm->mutex);
        
        // 1. If user pressed close_button early
        if (shm->close_button == 1 && strcmp(shm->status, "Open") == 0) {
            shm->close_button = 0;
            strcpy(shm->status, "Closing");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            break;
        }
        
        // if state changed externally
        if (strcmp(shm->status, "Open") != 0) {
            pthread_mutex_unlock(&shm->mutex);
            break;
        }
        
        pthread_mutex_unlock(&shm->mutex);

        // 2. If scheduled close time has arrived
        if ((now.tv_sec > close_time.tv_sec) ||
            (now.tv_sec == close_time.tv_sec && now.tv_nsec >= close_time.tv_nsec)) {
            pthread_mutex_lock(&shm->mutex);
            if(strcmp(shm->status, "Open") == 0) {
                strcpy(shm->status, "Closing");
                pthread_cond_broadcast(&shm->cond);
            }
            pthread_mutex_unlock(&shm->mutex);
            send_status_update();
            break;
        }

        my_usleep(1000);  // 1ms
    }

    // --- Closing phase ---
    struct timespec closing_start;
    clock_gettime(CLOCK_MONOTONIC, &closing_start);
    struct timespec new_closed_time = closing_start;
    increment_time(&new_closed_time, delay_ms);

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &new_closed_time, NULL);

    pthread_mutex_lock(&shm->mutex);
    if (strcmp(shm->status, "Closing") == 0) {
        strcpy(shm->status, "Closed");
        pthread_cond_broadcast(&shm->cond);
    }
    pthread_mutex_unlock(&shm->mutex);
    send_status_update();
}