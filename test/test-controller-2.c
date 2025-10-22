#include "shared.h"

// Tester for controller (multiple cars)

/*
    Alpha  Beta   Gamma
  5               -----
  4 -----          | |
  3  | |           | |
  2  | |          -----
  1 -----  -----
 B1         | |
 B2         | |
 B3        -----

*/

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

  // Register a car that can take floors B3 to 1
  int beta = connect_to_controller();
  send_message(beta, "CAR Beta B3 1");
  send_message(beta, "STATUS Closed B3 B3");

  // Register a car that can take floors 2 to 5
  int gamma = connect_to_controller();
  send_message(gamma, "CAR Gamma 2 5");
  send_message(gamma, "STATUS Closed 2 2");

  usleep(DELAY);

  // Call an elevator to go from 1 to 3. Only Alpha can do this
  test_call("CALL 1 3", "CAR Alpha");
  test_recv(alpha, "RECV: FLOOR 1");

  // Call an elevator to go from 1 to B2. Only Beta can do this
  test_call("CALL 1 B2", "CAR Beta");
  test_recv(beta, "RECV: FLOOR 1");

  // Call an elevator to go from 3 to 5. Only Gamma can do this
  test_call("CALL 3 5", "CAR Gamma");
  test_recv(gamma, "RECV: FLOOR 3");

  // Call an elevator to go from 1 to 5. No car can do this
  test_call("CALL 1 5", "UNAVAILABLE");

  // Call an elevator to go from B3 to 3. No car can do this
  test_call("CALL B3 3", "UNAVAILABLE");

  cleanup(p);

  close(alpha);
  close(beta);
  close(gamma);
  
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
