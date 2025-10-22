#include "shared.h"

// Tester for car (Individual service mode, no controller)

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

  p = car("Test", "1", "2", "10");
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 0, 0, 0, 0, 0}");
  displaycond(shm);
  usleep(DELAY);

  // Put the car into individual service mode
  pthread_mutex_lock(&shm->mutex);
  shm->individual_service_mode = 1;
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);

  msg("Current state: {1, 1, Closed, 0, 0, 0, 0, 0, 1, 0}");
  displaycond(shm);

  // Open the doors. They should stay open
  pthread_mutex_lock(&shm->mutex);
  shm->open_button = 1;
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);
  usleep(DELAY);
  msg("Current state: {1, 1, Open, 0, 0, 0, 0, 0, 1, 0}");
  displaycond(shm);

  // Close the doors
  pthread_mutex_lock(&shm->mutex);
  shm->close_button = 1;
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 0, 0, 0, 1, 0}");
  displaycond(shm);

  // Drive the elevator up one level
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->destination_floor, "2");
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);
  usleep(5 * MILLISECOND);
  msg("Current state: {1, 2, Between, 0, 0, 0, 0, 0, 1, 0}");
  displaycond(shm);
  usleep(10 * MILLISECOND);
  msg("Current state: {2, 2, Closed, 0, 0, 0, 0, 0, 1, 0}");
  displaycond(shm);

  // Attempt to drive the elevator up one level, but it will not work as we reached the top
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->destination_floor, "3");
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);
  usleep(5 * MILLISECOND);
  msg("Current state: {2, 2, Closed, 0, 0, 0, 0, 0, 1, 0}");
  displaycond(shm);

  // Attempt to drive the elevator down one level
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->destination_floor, "1");
  pthread_cond_broadcast(&shm->cond);
  pthread_mutex_unlock(&shm->mutex);
  usleep(5 * MILLISECOND);
  msg("Current state: {2, 1, Between, 0, 0, 0, 0, 0, 1, 0}");
  displaycond(shm);
  usleep(10 * MILLISECOND);
  msg("Current state: {1, 1, Closed, 0, 0, 0, 0, 0, 1, 0}");
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
