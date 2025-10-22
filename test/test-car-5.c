#include "shared.h"

// Tester for car (Testing SIGINT shm cleanup)

#define DELAY 50000 // 50ms
#define MILLISECOND 1000 // 1ms

pid_t car(const char *, const char *, const char *, const char *);
void displaycond(car_shared_mem *);
void cleanup(pid_t);
void *server(void *);
void server_init();
void test_recv(int, const char *);

int server_fd;
int shm_fd;
static car_shared_mem *shm;

int main()
{
  shm_unlink("/carTest"); // Remove shm object if it exists

  pid_t p;

  // Testing with car launched before server
  p = car("Test", "1", "4", "10");

  // Wait 50ms
  usleep(DELAY);

  // Send SIGINT (simulating ctrl+c)
  kill(p, SIGINT);

  // Wait again to give it time to close
  usleep(DELAY);

  msg("shm_open(): No such file or directory");
  int shm_fd = shm_open("/carTest", O_RDWR, 0666);
  if (shm_fd == -1) perror("shm_open()");

  cleanup(p);
  printf("\nTests completed.\n");
}


void cleanup(pid_t p)
{
  munmap(shm, sizeof(car_shared_mem));
  close(shm_fd);
  kill(p, SIGINT);
  usleep(DELAY);
  shm_unlink("/carTest");
}

pid_t car(const char *name, const char *lowest_floor, const char *highest_floor, const char *delay)
{
  pid_t pid = fork();
  if (pid == 0) {
    execlp("./car", "./car", name, lowest_floor, highest_floor, delay, NULL);
  }

  return pid;
}
