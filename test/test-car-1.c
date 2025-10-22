#include "shared.h"

// Tester for car (Shared memory, delay and door operation, no controller)

#define DELAY 50000 // 50ms
#define MILLISECOND 1000 // 1ms

void displaycond(car_shared_mem *);
pid_t car(const char *, const char *, const char *, const char *);
void cleanup(pid_t);

int shm_fd;
static car_shared_mem *shm;

int main()
{
  shm_unlink("/carTest"); // Remove shm object if it exists
  pid_t p;

  p = car("Test", "B4", "4", "10");
  usleep(DELAY);
  msg("Current state: {B4, B4, Closed, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);

  pthread_mutex_lock(&shm->mutex);
  shm->open_button = 1;
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);
  // 0ms - opening. 10ms - open. 20ms - closing. 30ms - closed
  usleep(15 * MILLISECOND);
  msg("Current state: {B4, B4, Open, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  usleep(25 * MILLISECOND);
  msg("Current state: {B4, B4, Closed, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);

  cleanup(p);

  // Slower car, to test door opening times

  p = car("Test", "12", "16", "100");
  usleep(DELAY);
  msg("Current state: {12, 12, Closed, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  pthread_mutex_lock(&shm->mutex);
  shm->open_button = 1;
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);

  usleep(50 * MILLISECOND);
  msg("Current state: {12, 12, Opening, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  usleep(100 * MILLISECOND);
  msg("Current state: {12, 12, Open, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  usleep(100 * MILLISECOND);
  msg("Current state: {12, 12, Closing, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  usleep(100 * MILLISECOND);
  msg("Current state: {12, 12, Closed, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);

  cleanup(p);

  // Test close button

  p = car("Test", "B64", "945", "100");
  usleep(DELAY);
  msg("Current state: {B64, B64, Closed, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  pthread_mutex_lock(&shm->mutex);
  shm->open_button = 1;
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);

  usleep(110 * MILLISECOND);
  msg("Current state: {B64, B64, Open, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);

  pthread_mutex_lock(&shm->mutex);
  shm->close_button = 1;
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);
  usleep(40 * MILLISECOND);
  msg("Current state: {B64, B64, Closing, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  usleep(100 * MILLISECOND);
  msg("Current state: {B64, B64, Closed, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
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

void displaycond(car_shared_mem *s)
{
  pthread_mutex_lock(&s->mutex);
  printf("Current state: {%s, %s, %s, %d, %d, %d, %d, %d, %d, %d}\n",
    s->current_floor,
    s->destination_floor,
    s->status,
    s->open_button,
    s->close_button,
    s->door_obstruction,
    s->overload,
    s->emergency_stop,
    s->individual_service_mode,
    s->emergency_mode
  );
  pthread_mutex_unlock(&s->mutex);
}

pid_t car(const char *name, const char *lowest_floor, const char *highest_floor, const char *delay)
{
  pid_t pid = fork();
  if (pid == 0) {
    execlp("./car", "./car", name, lowest_floor, highest_floor, delay, NULL);
  }
  usleep(DELAY);
  shm_fd = shm_open("/carTest", O_RDWR, 0666);
  shm = mmap(0, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

  return pid;
}
