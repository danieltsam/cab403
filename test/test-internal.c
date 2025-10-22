#include "shared.h"

// Tester for internal controls

#define DELAY 50000 // 50ms

void *condwatch(void *);
void test_operation(car_shared_mem *, const char *, const char *, const char *);
void test_operation_floor(car_shared_mem *, const char *, const char *, const char *, const char *);

int main()
{
  shm_unlink("/carTest"); // Remove shm object if it exists

  msg("Unable to access car Test.");
  system("./internal Test open"); // Valid operation but shm unavailable

  int fd = shm_open("/carTest", O_CREAT | O_RDWR, 0666);
  ftruncate(fd, sizeof(car_shared_mem));
  car_shared_mem *shm = mmap(0, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  init_shm(shm);

  pthread_t tid;
  pthread_create(&tid, NULL, condwatch, shm);
  pthread_detach(tid);

  usleep(DELAY);

  // Testing open button
  test_operation(shm, "Closed", "open", "Current state: {1, 1, Closed, 1, 0, 0, 0, 0, 0, 0}");
  // Testing close button
  test_operation(shm, "Closed", "close", "Current state: {1, 1, Closed, 1, 1, 0, 0, 0, 0, 0}");
  // Testing emergency stop
  test_operation(shm, "Closed", "stop", "Current state: {1, 1, Closed, 1, 1, 0, 0, 1, 0, 0}");
  // Testing up and down (while not in service mode)
  test_operation(shm, "Closed", "up", "Operation only allowed in service mode.");
  test_operation(shm, "Closed", "down", "Operation only allowed in service mode.");
  // Testing enabling service mode
  test_operation(shm, "Closed", "service_on", "Current state: {1, 1, Closed, 1, 1, 0, 0, 1, 1, 0}");
  // Testing up and down (while in service mode)
  test_operation(shm, "Closed", "up", "Current state: {1, 2, Closed, 1, 1, 0, 0, 1, 1, 0}");
  test_operation(shm, "Closed", "down", "Current state: {1, B1, Closed, 1, 1, 0, 0, 1, 1, 0}");
  // Testing up and down (while doors are open or partly open)
  test_operation(shm, "Open", "up", "Operation not allowed while doors are open.");
  test_operation(shm, "Open", "down", "Operation not allowed while doors are open.");
  test_operation(shm, "Opening", "up", "Operation not allowed while doors are open.");
  test_operation(shm, "Opening", "down", "Operation not allowed while doors are open.");
  test_operation(shm, "Closing", "up", "Operation not allowed while doors are open.");
  test_operation(shm, "Closing", "down", "Operation not allowed while doors are open.");
  // Testing up and down (while between floors)
  test_operation(shm, "Between", "up", "Operation not allowed while elevator is moving.");
  test_operation(shm, "Between", "down", "Operation not allowed while elevator is moving.");
  // Testing up and down (with various floors)
  test_operation_floor(shm, "Closed", "up", "Current state: {B32, B31, Closed, 1, 1, 0, 0, 1, 1, 0}", "B32");
  test_operation_floor(shm, "Closed", "down", "Current state: {617, 616, Closed, 1, 1, 0, 0, 1, 1, 0}", "617");
  test_operation_floor(shm, "Closed", "up", "Current state: {48, 49, Closed, 1, 1, 0, 0, 1, 1, 0}", "48");
  test_operation_floor(shm, "Closed", "down", "Current state: {B98, B99, Closed, 1, 1, 0, 0, 1, 1, 0}", "B98");
  test_operation_floor(shm, "Closed", "up", "Current state: {998, 999, Closed, 1, 1, 0, 0, 1, 1, 0}", "998");
  // Testing disabling service mode
  test_operation(shm, "Closed", "service_off", "Current state: {1, 1, Closed, 1, 1, 0, 0, 1, 0, 0}");
  // Testing invalid operation
  test_operation(shm, "Closed", "jdfgklsj", "Invalid operation.");
  // Testing enabling service mode disables emergency mode
  pthread_mutex_lock(&shm->mutex);
  shm->emergency_mode = 1;
  pthread_mutex_unlock(&shm->mutex);
  test_operation(shm, "Closed", "service_on", "Current state: {1, 1, Closed, 1, 1, 0, 0, 1, 1, 0}");

  printf("\nTests completed.\n");
  shm_unlink("/carTest"); // Remove shm object
}

void test_operation(car_shared_mem *s, const char *st, const char *op, const char *m)
{
  msg(m);
  // Initialise status
  pthread_mutex_lock(&s->mutex);
  strcpy(s->status, st);
  pthread_mutex_unlock(&s->mutex);
  char cmd_buf[256];
  sprintf(cmd_buf, "./internal Test %s", op);
  system(cmd_buf);

  usleep(DELAY);

  // Reset current and destination (in case they changed)
  pthread_mutex_lock(&s->mutex);
  strcpy(s->current_floor, "1");
  strcpy(s->destination_floor, "1");
  pthread_mutex_unlock(&s->mutex);
}

void test_operation_floor(car_shared_mem *s, const char *st, const char *op, const char *m, const char *f)
{
  pthread_mutex_lock(&s->mutex);
  strcpy(s->current_floor, f);
  strcpy(s->destination_floor, f);
  pthread_mutex_unlock(&s->mutex);
  test_operation(s, st, op, m);
}


void *condwatch(void *p)
{
  car_shared_mem *s = p;
  pthread_mutex_lock(&s->mutex);
  for (;;) {
    pthread_cond_wait(&s->cond, &s->mutex);
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
  }
}
