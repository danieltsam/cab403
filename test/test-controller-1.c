#include "shared.h"

// Tester for controller (single car, very basic tests)

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


  // Attempt to call an elevator before any cars register
  test_call("CALL 1 2", "UNAVAILABLE");

  // Register a car that can take floors B1 to 4
  int alpha = connect_to_controller();
  // Send initialisation message and first status message
  send_message(alpha, "CAR Alpha B1 4");
  send_message(alpha, "STATUS Closed B1 B1");
  usleep(DELAY);

  // Call an elevator. Alpha should be dispatched and told to go to 1
  test_call("CALL 1 2", "CAR Alpha");
  test_recv(alpha, "RECV: FLOOR 1");

  // Send status updates telling the controller that the car is moving
  send_message(alpha, "STATUS Between B1 1");
  send_message(alpha, "STATUS Opening 1 1");
  // Now that Alpha has arrived at floor 1 it should be told to go to floor 2
  test_recv(alpha, "RECV: FLOOR 2");

  send_message(alpha, "STATUS Open 1 2");
  send_message(alpha, "STATUS Closing 1 2");
  send_message(alpha, "STATUS Between 1 2");
  send_message(alpha, "STATUS Opening 2 2");
  send_message(alpha, "STATUS Open 2 2");
  send_message(alpha, "STATUS Closing 2 2");
  send_message(alpha, "STATUS Closed 2 2");
  usleep(DELAY);
  
  // Call an elevator from a floor Alpha does not service
  test_call("CALL 5 1", "UNAVAILABLE");
  // Call an elevator to a floor Alpha does not service
  test_call("CALL B2 3", "UNAVAILABLE");
  
  // Call from floor 2 to floor 1. Alpha can service this one
  test_call("CALL 2 1", "CAR Alpha");
  test_recv(alpha, "RECV: FLOOR 2");
  send_message(alpha, "STATUS Opening 2 2");
  test_recv(alpha, "RECV: FLOOR 1");
  send_message(alpha, "STATUS Open 2 1");
  send_message(alpha, "STATUS Closing 2 1");
  send_message(alpha, "STATUS Between 2 1");
  send_message(alpha, "STATUS Opening 1 1");
  send_message(alpha, "STATUS Open 1 1");
  send_message(alpha, "STATUS Closing 1 1");
  send_message(alpha, "STATUS Closed 1 1");

  cleanup(p);

  close(alpha);

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
