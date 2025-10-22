#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>

#define MAX_CARS 10
#define MAX_QUEUE 20

typedef struct {
    int in_use;
    int socket_fd;
    char car_name[64];
    int floor_min;
    int floor_max;
    int current_floor;
    char status[16];
    int queue[MAX_QUEUE];
    int queue_size;
} Car;

static Car cars[MAX_CARS];
static pthread_mutex_t cars_mutex = PTHREAD_MUTEX_INITIALIZER;
static int listening_socket = -1;
static volatile int should_exit = 0;

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

void signal_handler(int sig) {
    if (sig == SIGINT) {
        should_exit = 1;
        if (listening_socket != -1) {
            close(listening_socket);
        }
    }
}

Car* find_car(int socket_fd) {
    for (int i = 0; i < MAX_CARS; i++) {
        if (cars[i].in_use && cars[i].socket_fd == socket_fd) {
            return &cars[i];
        }
    }
    return NULL;
}

Car* find_available_car(int source_floor, int dest_floor) {
    for (int i = 0; i < MAX_CARS; i++) {
        if (cars[i].in_use && 
            source_floor >= cars[i].floor_min && source_floor <= cars[i].floor_max &&
            dest_floor >= cars[i].floor_min && dest_floor <= cars[i].floor_max) {
            return &cars[i];
        }
    }
    return NULL;
}

void add_to_queue(Car *car, int floor) {
    if (car->queue_size < MAX_QUEUE) {
        car->queue[car->queue_size++] = floor;
    }
}

void* handle_client(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    char *msg = recv_msg(client_fd);
    if (!msg) {
        close(client_fd);
        return NULL;
    }

    if (strncmp(msg, "CAR ", 4) == 0) {
        // Car registration
        char name[64], min_floor[8], max_floor[8];
        sscanf(msg + 4, "%s %s %s", name, min_floor, max_floor);
        
        pthread_mutex_lock(&cars_mutex);
        for (int i = 0; i < MAX_CARS; i++) {
            if (!cars[i].in_use) {
                cars[i].in_use = 1;
                cars[i].socket_fd = client_fd;
                strcpy(cars[i].car_name, name);
                cars[i].floor_min = floor_to_int(min_floor);
                cars[i].floor_max = floor_to_int(max_floor);
                cars[i].current_floor = cars[i].floor_min;
                strcpy(cars[i].status, "Closed");
                cars[i].queue_size = 0;
                break;
            }
        }
        pthread_mutex_unlock(&cars_mutex);
        
        // Send status update request
        char status_msg[100];
        char dest_floor[8];
        int_to_floor(cars[0].current_floor, dest_floor, sizeof(dest_floor));
        snprintf(status_msg, sizeof(status_msg), "STATUS %s %s %s", 
                cars[0].status, dest_floor, dest_floor);
        send_msg(client_fd, status_msg);
        
        // Keep connection alive
        while (!should_exit) {
            char *status_msg = recv_msg(client_fd);
            if (!status_msg) break;
            
            if (strncmp(status_msg, "STATUS ", 7) == 0) {
                char status[16], current[8], dest[8];
                sscanf(status_msg + 7, "%s %s %s", status, current, dest);
                
                pthread_mutex_lock(&cars_mutex);
                Car *car = find_car(client_fd);
                if (car) {
                    strcpy(car->status, status);
                    car->current_floor = floor_to_int(current);
                    
                    // Check if car is ready for next floor
                    if (strcmp(status, "Closed") == 0 && car->queue_size > 0) {
                        char floor_msg[16];
                        int_to_floor(car->queue[0], floor_msg, sizeof(floor_msg));
                        char cmd[32];
                        snprintf(cmd, sizeof(cmd), "FLOOR %s", floor_msg);
                        send_msg(car->socket_fd, cmd);
                        
                        // Remove from queue
                        for (int i = 0; i < car->queue_size - 1; i++) {
                            car->queue[i] = car->queue[i + 1];
                        }
                        car->queue_size--;
                    }
                }
                pthread_mutex_unlock(&cars_mutex);
            }
            free(status_msg);
        }
        
        // Remove car from list
        pthread_mutex_lock(&cars_mutex);
        Car *car = find_car(client_fd);
        if (car) {
            car->in_use = 0;
        }
        pthread_mutex_unlock(&cars_mutex);
        
    } else if (strncmp(msg, "CALL ", 5) == 0) {
        // Call request
        char source[8], dest[8];
        sscanf(msg + 5, "%s %s", source, dest);
        
        int source_floor = floor_to_int(source);
        int dest_floor = floor_to_int(dest);
        
        pthread_mutex_lock(&cars_mutex);
        Car *car = find_available_car(source_floor, dest_floor);
        if (car) {
            add_to_queue(car, source_floor);
            add_to_queue(car, dest_floor);
            
            // Send floor command immediately
            char floor_msg[16];
            int_to_floor(source_floor, floor_msg, sizeof(floor_msg));
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "FLOOR %s", floor_msg);
            send_msg(car->socket_fd, cmd);
            
            char response[64];
            snprintf(response, sizeof(response), "CAR %s", car->car_name);
            send_msg(client_fd, response);
        } else {
            send_msg(client_fd, "UNAVAILABLE");
        }
        pthread_mutex_unlock(&cars_mutex);
    }
    
    free(msg);
    close(client_fd);
    return NULL;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket == -1) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(3000);
    
    if (bind(listening_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listening_socket);
        return 1;
    }
    
    if (listen(listening_socket, 16) == -1) {
        perror("listen");
        close(listening_socket);
        return 1;
    }
    
    printf("Controller running on port 3000\n");
    
    while (!should_exit) {
        int client_fd = accept(listening_socket, NULL, NULL);
        if (client_fd == -1) continue;
        
        pthread_t tid;
        int *arg = malloc(sizeof(int));
        *arg = client_fd;
        pthread_create(&tid, NULL, handle_client, arg);
        pthread_detach(tid);
    }
    
    close(listening_socket);
    return 0;
}