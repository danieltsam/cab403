#include "shared.h"

// Tester for controller (car put into service / emergency mode)

#define DELAY 50000 // 50ms
#define MILLISECOND 1000 // 1ms

pid_t controller(void);
int connect_to_controller(void);
void test_call(const char *, const char *);
void test_recv(int, const char *);
void cleanup(pid_t);

int main()
{
  pid_t p;
  p = controller();
  usleep(DELAY);

  // Register a car that can take floors 1 to 4
  int alpha = connect_to_controller();
  send_message(alpha, "CAR Alpha 1 4");
  send_message(alpha, "STATUS Closed 1 1");
  usleep(DELAY);
  test_call("CALL 1 3", "CAR Alpha");
  test_recv(alpha, "RECV: FLOOR 1");
  send_message(alpha, "STATUS Opening 1 1");
  test_recv(alpha, "RECV: FLOOR 3");
  send_message(alpha, "STATUS Open 1 3");
  send_message(alpha, "STATUS Closing 1 3");
  send_message(alpha, "STATUS Between 1 3");
  send_message(alpha, "STATUS Between 2 3");
  send_message(alpha, "STATUS Opening 3 3");
  send_message(alpha, "STATUS Open 3 3");
  send_message(alpha, "STATUS Closing 3 3");
  send_message(alpha, "STATUS Closed 3 3");

  // The technician is in the lift and sets it to individual service mode.
  send_message(alpha, "INDIVIDUAL SERVICE");
  shutdown(alpha, SHUT_RDWR);
  close(alpha);

  usleep(DELAY);

  // Call a lift from 3 to 4. Normally Alpha could handle this, but it is
  // in individual service mode so no car can handle it
  test_call("CALL 3 4", "UNAVAILABLE");

  // Alpha now reappears on a different floor - the technician moved it
  // Should be schedulable again as before
  alpha = connect_to_controller();
  send_message(alpha, "CAR Alpha 1 4");
  send_message(alpha, "STATUS Closed 2 2");
  usleep(DELAY);
  test_call("CALL 3 4", "CAR Alpha");
  test_recv(alpha, "RECV: FLOOR 3");

  send_message(alpha, "STATUS Between 2 3");
  send_message(alpha, "STATUS Opening 3 3");
  test_recv(alpha, "RECV: FLOOR 4");
  send_message(alpha, "STATUS Open 3 4");
  send_message(alpha, "STATUS Closing 3 4");
  send_message(alpha, "STATUS Between 3 4");
  send_message(alpha, "STATUS Opening 4 4");
  send_message(alpha, "STATUS Open 4 4");
  send_message(alpha, "STATUS Closing 4 4");

  // Put Alpha in emergency mode
  send_message(alpha, "EMERGENCY");
  close(alpha);

  usleep(DELAY);
  // Call a lift from 2 to 1. No car is available to take this
  test_call("CALL 2 1", "UNAVAILABLE");

  cleanup(p);

  printf("\nTests completed.\n");
}

void test_call(const char *sendmsg, const char *expectedreply)
{
  int fd = connect_to_controller();
  send_message(fd, sendmsg);
  char *reply = receive_msg(fd);
  msg(expectedreply);
  printf("%s\n", reply);
  free(reply);
  close(fd);
}

void test_recv(int fd, const char *t)
{
  char *m = receive_msg(fd);
  msg(t);
  printf("RECV: %s\n", m);
  free(m);
}

int connect_to_controller(void)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(3000);
  sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (const struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1)
  {
    perror("connect()");
    exit(1);
  }
  return fd;
}

void cleanup(pid_t p)
{
  // Terminate with SIGINT to allow server to clean up
  kill(p, SIGINT);
}

pid_t controller(void)
{
  pid_t pid = fork();
  if (pid == 0) {
    execlp("./controller", "./controller", NULL);
  }

  return pid;
}
