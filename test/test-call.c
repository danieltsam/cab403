#include "shared.h"

// Tester for call point

#define DELAY 50000 // 50ms

void *server(void *);

int main()
{
  msg("Unable to connect to elevator system.");
  system("./call B1 3"); // Testing valid floors with elevator system unavailable
  pthread_t tid;
  pthread_create(&tid, NULL, server, NULL);
  pthread_detach(tid);
  usleep(DELAY);
  msg("RECV: CALL B21 337 : Car Test is arriving.");
  system("./call B21 337"); // Testing two valid floors
  usleep(DELAY);
  msg("You are already on that floor!");
  system("./call 152 152"); // Testing same floor
  usleep(DELAY);
  msg("RECV: CALL 416 B68 : Sorry, no car is available to take this request.");
  system("./call 416 B68"); // Testing two valid floors
  usleep(DELAY);
  msg("Invalid floor(s) specified.");
  system("./call L4 8"); // Testing wrong format floor
  usleep(DELAY);
  msg("Invalid floor(s) specified.");
  system("./call B100 B98"); // Testing out of range floor
  usleep(DELAY);
  msg("Invalid floor(s) specified.");
  system("./call 800 1000"); // Testing out of range floor

  printf("\nTests completed.\n");
}

void *server(void *_)
{
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(3000);
  a.sin_addr.s_addr = htonl(INADDR_ANY);

  int s = socket(AF_INET, SOCK_STREAM, 0);
  int opt_enable = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable));
  if (bind(s, (const struct sockaddr *)&a, sizeof(a)) == -1) {
    perror("bind()");
    exit(1);
  }

  listen(s, 10);
  int fd;
  char *msg;

  fd = accept(s, NULL, NULL);
  msg = receive_msg(fd);
  printf("RECV: %s : ", msg);
  fflush(stdout);
  free(msg);
  send_message(fd, "CAR Test");
  close(fd);

  fd = accept(s, NULL, NULL);
  msg = receive_msg(fd);
  printf("RECV: %s : ", msg);
  fflush(stdout);
  free(msg);
  send_message(fd, "UNAVAILABLE");
  close(fd);

  fd = accept(s, NULL, NULL);
  msg = receive_msg(fd);
  printf("(This shouldn't happen) RECV: %s : ", msg);
  fflush(stdout);
  free(msg);
  close(fd);
  return NULL;
}

