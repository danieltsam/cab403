// This program requires a library called 'ncurses' to run
// Install it with 'sudo apt install ncurses-dev' (on Ubuntu)
// then type 'make display-cars'

#include <ncurses.h>
#include <math.h>
#include <dirent.h>
#include <sys/time.h>
#include "shared.h"

// Default refresh rate: 50 frames/sec
#define FRAME_RATE 50
#define REFRESH_DELAY (1000000 / FRAME_RATE)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

struct carinfo {
    char name[128];
    int64_t delay;
    struct timeval status_tv;
    char state;
    car_shared_mem mem;
    struct carinfo *next;
};

static struct carinfo *cars = NULL;
static int highest = 1, lowest = 1;
int64_t us_diff(const struct timeval *, const struct timeval *);
void scan_cars(void);

int fti(const char *f)
{
    if (f[0] == 'B') return 1-atoi(f+1);
    else return atoi(f);
}
void itf(char *out, int f)
{
    if (f <= 0) sprintf(out, "B%d", 1-f);
    else sprintf(out, "%d", f);
}

int main(int argc, char **argv)
{
    if (argc >= 3) {
        lowest = fti(argv[1]);
        highest = fti(argv[2]);
    }
    if (highest < lowest) {
        fprintf(stderr, "Lowest floor must be lower than highest floor\n");
        exit(1);
    }
    
	initscr();
    nodelay(stdscr, true);

    for (;;) {
        erase();
        move(0, 20);
        scan_cars();
        if (getch() == 27) { // Esc
            break;
        }
        int w, h;
        getmaxyx(stdscr, h, w);

        int height = highest - lowest + 1;
        
        // Write floor numbers
        for (int i = 0; i < height; i++) {
            int y1 = (h * i / height);
            int y2 = (h * (i + 1) / height - 1);
            move((y1 + y2) / 2, 0);
            char buf[4];
            itf(buf, highest - i);
            printw("%s", buf);
        }

        // Count how many cars are active
        int numcars = 0;
        struct carinfo *c = cars;
        while (c != NULL) {
            if (c->state == 'c') numcars++;
            c = c->next;
        }

        // Draw each car
        c = cars;
        int carpos = 0;

        struct timeval current_tv;
        gettimeofday(&current_tv, NULL);

        while (c != NULL) {
            if (c->state != 'c') continue; // Only render active cars

            // Determine X bounds of the car
            int x1 = ((w - 4) * carpos / numcars) + 4;
            int x2 = ((w - 4) * (carpos + 1) / numcars) + 3;
            int colwidth = x2 - x1 + 1;
            // Determine Y bounds of the car
            int current_floor = fti(c->mem.current_floor);
            
            float floor = height - 1 - (current_floor - lowest);
            // Look at timestamp of last status change - guess progress
            int64_t us_passed = us_diff(&c->status_tv, &current_tv);
            float progress = fminf(1.0f * us_passed / c->delay, 1.0f);

            if (strcmp(c->mem.status, "Between")==0) {
                int destination_floor = fti(c->mem.destination_floor);
                int dir = (destination_floor - current_floor) / abs(destination_floor - current_floor);
                floor -= progress * dir;
            }
            
            int y1 = (int) roundf(h * floor / height);
            int y2 = (int) roundf(h * (floor + 1) / height - 1);

            for (int x = x1; x <= x2; x++) {
                move(y1, x);
                printw("=");
                move(y2, x);
                printw("=");
            }
            for (int y = y1 + 1; y < y2; y++) {
                move(y, x1);
                printw("||");
                move(y, x2-1);
                printw("||");
            }
            // Draw the insides of the car, showing the doors open/closed
            int door_closed_w;
            if (strcmp(c->mem.status, "Open")==0) {
                door_closed_w = 0;
            } else if (strcmp(c->mem.status, "Opening")==0) {
                door_closed_w = (int) roundf( (colwidth - 2) / 2 * (1.0f - progress) );
            } else if (strcmp(c->mem.status, "Closing")==0) {
                door_closed_w = (int) roundf( (colwidth - 2) / 2 * progress );
            } else {
                door_closed_w = (colwidth - 2) / 2;
            }

            // Display service mode / emergency mode
            move(y2, x1);
            if (c->mem.individual_service_mode) printw("(S)");
            if (c->mem.emergency_mode) printw("(E)");

            for (int y = y1 + 1; y < y2; y++) {
                move(y, x1 + 2);
                for (int i = 2; i < door_closed_w; i++) {
                    printw(".");
                }
                move(y, x2 - door_closed_w);
                for (int i = 0; i < door_closed_w - 1; i++) {
                    printw(".");
                }
                move(y, x1 + door_closed_w);
                printw("|");
                move(y, x2 - door_closed_w);
                printw("|");
            }

            // Write car name
            move(y1, (colwidth - strlen(c->name + 3) - 4)/2 + x1);
            printw("( %s )", c->name + 3);
            // Write car status
            move(y2, (colwidth - strlen(c->mem.status))/2 + x1);
            printw("%s", c->mem.status);
            
            carpos++;
            c = c->next;
        }
        
        move(0, 0);
        
        refresh();
        usleep(REFRESH_DELAY);
    }
	endwin();

	return 0;
}

int64_t us_diff(const struct timeval *before, const struct timeval *after)
{
    int64_t diff = ((int64_t)after->tv_sec - (int64_t)before->tv_sec) * (int64_t)1000000;
    diff += (int64_t)after->tv_usec - (int64_t)before->tv_usec;
    return diff;
}

struct carinfo *get_car_by_name(const char *name)
{
    struct carinfo *c = cars;
    while (c != NULL) {
        if (strcmp(name, c->name)==0) {
            return c;
        }
        c = c->next;
    }
    return NULL;
}

void cleanup(void)
{
    // Clean up cars that are no longer present
    struct carinfo *c = cars;
    struct carinfo *prev = NULL;
    while (c != NULL) {
        if (c->state == 'o') {
            if (prev) {
                prev->next = c->next;
            } else {
                cars = c->next;
            }
            struct carinfo *old = c;
            c = c->next;
            free(old);
        } else {
            prev = c;
            c = c->next;
        }
    }
}

void scan_cars(void)
{
    {
        // Set existing cars to 'o'. This allows us to keep
        // track of the ones that need to be removed.
        struct carinfo *c = cars;
        while (c != NULL) {
            if (c->state == 'c') c->state = 'o';
            c = c->next;
        }
    }

    // Get the current time
    struct timeval current_tv;
    gettimeofday(&current_tv, NULL);

    DIR *dir = opendir("/dev/shm");
    if (dir) {
        for (;;) {
            struct dirent *e = readdir(dir);
            if (!e) break;

            if (strncmp(e->d_name, "car", 3)==0) {
                struct carinfo *c = get_car_by_name(e->d_name);
                char shmname[257];
                sprintf(shmname, "/%s", e->d_name);
                int fd = shm_open(shmname, O_RDWR, 0);
                if (fd == -1) {
                    // Failed to load - skip and keep looping
                    continue;
                }
                car_shared_mem *shm = mmap(0, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (c == NULL) {
                    c = malloc(sizeof(struct carinfo));
                    strncpy(c->name, e->d_name, 127);

                    // Insert in alphabetical order
                    if (cars == NULL || strcmp(e->d_name, cars->name) == -1) {
                        c->next = cars;
                        cars = c;
                    } else {
                        struct carinfo *t = cars;
                        while (t != NULL) {
                            if (t->next == NULL || strcmp(e->d_name, t->next->name) == -1) {
                                c->next = t->next;
                                t->next = c;
                                break;
                            }
                            t = t->next;
                        }
                    }

                    c->status_tv = current_tv;
                    c->mem = *shm;
                    c->delay = 1000000; // Default (1000ms)
                }
                c->state = 'c';
                if (strcmp(c->mem.status, shm->status) != 0 || strcmp(c->mem.current_floor, shm->current_floor) != 0) {
                    if ((strcmp(c->mem.status, "Between")==0 && strcmp(shm->status, "Opening")==0) ||
                        (strcmp(c->mem.status, "Between")==0 && strcmp(shm->status, "Closed")==0) ||
                        (strcmp(c->mem.status, "Opening")==0 && strcmp(shm->status, "Open")==0) ||
                        (strcmp(c->mem.status, "Closing")==0 && strcmp(shm->status, "Closed")==0) ||
                        (strcmp(c->mem.current_floor, shm->current_floor)!=0)) {
                            c->delay = us_diff(&c->status_tv, &current_tv);
                    }
                    c->status_tv = current_tv;
                }
                c->mem = *shm;

                // Dynamically resize
                int curr_floor = fti(c->mem.current_floor);
                highest = MAX(highest, curr_floor);
                lowest = MIN(lowest, curr_floor);
                int dest_floor = fti(c->mem.destination_floor);
                highest = MAX(highest, dest_floor);
                lowest = MIN(lowest, dest_floor);

                munmap(shm, sizeof(*shm));
                close(fd);
            }
        }

        closedir(dir);
    }

    cleanup();
}
