#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

// Simple floor validation
int is_valid_floor(const char *floor) {
    if (!floor || strlen(floor) == 0 || strlen(floor) > 3) return 0;
    
    if (floor[0] == 'B') {
        if (strlen(floor) < 2) return 0;
        for (int i = 1; floor[i] != '\0'; i++) {
            if (floor[i] < '0' || floor[i] > '9') return 0;
        }
        int num = atoi(floor + 1);
        return (num >= 1 && num <= 99);
    } else {
        for (int i = 0; floor[i] != '\0'; i++) {
            if (floor[i] < '0' || floor[i] > '9') return 0;
        }
        int num = atoi(floor);
        return (num >= 1 && num <= 999);
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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Invalid format\n");
        return 1;
    }
    
    const char* source_floor = argv[1];
    const char* destination_floor = argv[2];
    
    if (strcmp(source_floor, destination_floor) == 0) {
        printf("You are already on that floor!\n");
        return 1;
    }
    
    if (!is_valid_floor(source_floor) || !is_valid_floor(destination_floor)) {
        printf("Invalid floor(s) specified.\n");
        return 1;
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Unable to connect to elevator system.\n");
        return 1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    inet_aton("127.0.0.1", &addr.sin_addr);
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        printf("Unable to connect to elevator system.\n");
        close(sockfd);
        return 1;
    }
    
    char call_message[100];
    snprintf(call_message, sizeof(call_message), "CALL %s %s", source_floor, destination_floor);
    send_msg(sockfd, call_message);
    
    char *response = recv_msg(sockfd);
    if (!response) {
        printf("Unable to connect to elevator system.\n");
        close(sockfd);
        return 1;
    }
    
    if (strncmp(response, "CAR ", 4) == 0) {
        printf("Car %s is arriving.\n", response + 4);
    } else if (strcmp(response, "UNAVAILABLE") == 0) {
        printf("Sorry, no car is available to take this request.\n");
    } else {
        printf("Unable to connect to elevator system.\n");
    }
    
    free(response);
    close(sockfd);
    return 0;
}
