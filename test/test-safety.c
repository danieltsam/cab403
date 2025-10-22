#include "shared.h"

// Tester for safety system

#define DELAY 50000 // 50ms

void displaycond(car_shared_mem *);
int safety(void);
void cleanup(pid_t p);

int main()
{
  printf("# If the output produced by your program does not appear on Gradescope,\n");
  printf("# add fflush(stdout); after your printfs in the safety component\n");
  printf("# (Alternatively, use write() instead as stdio is discouraged in MISRA C)\n\n");
  shm_unlink("/carTest"); // Remove shm object if it exists

  msg("Unable to access car Test.");
  system("./safety Test"); // Attempt to launch safety system with shm missing

  int fd = shm_open("/carTest", O_CREAT | O_RDWR, 0666);
  ftruncate(fd, sizeof(car_shared_mem));
  car_shared_mem *shm = mmap(0, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  init_shm(shm);

  pid_t p = safety();

  usleep(DELAY);

  // Test the safety system heartbeat
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 0, 0, 0, 0}");
  pthread_mutex_lock(&shm->mutex);
  shm->safety_system = 2;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  displaycond(shm);

  reset_shm(shm);

  // Simulate a door obstruction
  msg("Current state: {1, 1, Opening, 0, 0, 1, 1, 0, 0, 0, 0}");
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->status, "Closing");
  shm->door_obstruction = 1;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  displaycond(shm);

  reset_shm(shm);

  // Simulate an emergency stop
  msg("The emergency stop button has been pressed!");
  pthread_mutex_lock(&shm->mutex);
  shm->emergency_stop = 1;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 0, 0, 0, 1}");
  displaycond(shm);

  reset_shm(shm);

  // Simulate overload
  msg("The overload sensor has been tripped!");
  pthread_mutex_lock(&shm->mutex);
  shm->overload = 1;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 1, 0, 0, 1}");
  displaycond(shm);

  // Simulate another emergency stop without resetting emergency_mode - no message should be printed
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 1, 1, 0, 1}");
  pthread_mutex_lock(&shm->mutex);
  shm->emergency_stop = 1;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  displaycond(shm);

  reset_shm(shm);

  // Test for data consistency errors
  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->status, "Asdfghj");
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Asdfghj, 0, 0, 1, 0, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->current_floor, "ABC");
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {ABC, 1, Closed, 0, 0, 1, 0, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->destination_floor, "DEF");
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, DEF, Closed, 0, 0, 1, 0, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  shm->close_button = 3;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 3, 1, 0, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  shm->emergency_mode = 17;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  shm->emergency_stop = 241;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 0, 241, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  shm->overload = 82;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 82, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  shm->individual_service_mode = 16;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 0, 0, 0, 16, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  shm->open_button = 5;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 5, 0, 1, 0, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  shm->door_obstruction = 255;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 255, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->status, "Open");
  shm->door_obstruction = 1;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Open, 0, 0, 1, 1, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->status, "Between");
  shm->door_obstruction = 1;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Between, 0, 0, 1, 1, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  msg("Data consistency error!");
  pthread_mutex_lock(&shm->mutex);
  strcpy(shm->status, "Closed");
  shm->door_obstruction = 1;
  pthread_mutex_unlock(&shm->mutex);
  pthread_cond_broadcast(&shm->cond);
  usleep(DELAY);
  msg("Current state: {1, 1, Closed, 0, 0, 1, 1, 0, 0, 0, 1}");
  displaycond(shm);
  reset_shm(shm);

  cleanup(p);
  printf("\nTests completed.\n");
  shm_unlink("/carTest"); // Remove shm object if it exists
}

void displaycond(car_shared_mem *s)
{
  pthread_mutex_lock(&s->mutex);
  printf("Current state: {%s, %s, %s, %d, %d, %d, %d, %d, %d, %d, %d}\n",
    s->current_floor,
    s->destination_floor,
    s->status,
    s->open_button,
    s->close_button,
    s->safety_system,
    s->door_obstruction,
    s->overload,
    s->emergency_stop,
    s->individual_service_mode,
    s->emergency_mode
  );
  pthread_mutex_unlock(&s->mutex);
}

int safety(void)
{
  pid_t pid = fork();
  if (pid == 0) {
    execlp("./safety", "./safety", "Test", NULL);
  }
  return pid;
}

void cleanup(pid_t p)
{
  kill(p, SIGINT);
}
