#include <bits/time.h>
#include <fcntl.h>
#include <liburing.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define STATE_MACHINE_IMPLEMENTATION
#include "state_machine.h"

#define QUEUE_DEPTH 16
#define PROC_INPUT_DEVICES "/proc/bus/input/devices"

// mimicking defer statements with stages for cleanup
#define STAGE static inline __attribute__((always_inline))

unsigned int clk = CLOCK_MONOTONIC;
const int UNINITIALIZED_FD = -1;
static int ui = UNINITIALIZED_FD;
const int INPUT_EVENT_SIZE = sizeof(struct input_event);

typedef struct {
  int fd;
  const char *path;
  struct input_event event;
} device_context;

void queue_device_read(struct io_uring *ring, device_context *context) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (!sqe) {
    return;
  }

  io_uring_prep_read(sqe, context->fd, &(context->event), INPUT_EVENT_SIZE, 0);
  io_uring_sqe_set_data(sqe, context);
}

void queue_time_read(struct io_uring *ring, int fd, uint64_t *time) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (!sqe) {
    return;
  }

  io_uring_prep_read(sqe, fd, time, sizeof(uint64_t), 0);
  io_uring_sqe_set_data(sqe, time);
}

void handle_event(state_machine *machine, struct input_event *event) {
  if (event->type == EV_KEY) {
    state_machine__interact(machine, event);
  }
  // if (event->type == EV_ABS) {
  // state_machine__interact(machine, event);
  // }
  state_machine__flush(machine);
}

STAGE int find_keyboard_event_path__post_fopen(FILE *fp, char *out_path);

int find_keyboard_event_path(char *out_path) {
  FILE *fp = fopen(PROC_INPUT_DEVICES, "r");
  if (!fp) {
    perror("Failed to open " PROC_INPUT_DEVICES);
    return -1;
  }

  int ret = find_keyboard_event_path__post_fopen(fp, out_path);
  fclose(fp);
  return ret;
}

int find_keyboard_event_path__post_fopen(FILE *fp, char *out_path) {
  char line[256];
  int is_keyboard = 0;

  while (true) {
    bool empty_line = fgets(line, sizeof(line), fp) == NULL;

    char first_char = line[0];
    bool end_of_section =
        empty_line || first_char == '\n' || first_char == '\r';

    if (end_of_section) {
      is_keyboard = 0;
      continue;
    }

    if (empty_line) {
      break;
    }

    if (strncmp(line, "N: Name=", 8) == 0) {
      is_keyboard += strcasestr(line, "keyboard") != NULL;
    }

    bool handler_prefix = strncmp(line, "H: Handlers=", 12) == 0;
    if (handler_prefix) {

      is_keyboard += strstr(line, "kbd") != NULL;
      char *event_identifier = strstr(line, "event");
      char *event_end = strstr(event_identifier, " ");

      // if space is found, terminate string there,
      // else event name is at the end already null terminated
      if (event_end != NULL) {
        *event_end = 0;
      }

      if (event_identifier && is_keyboard == 2) {
        snprintf(out_path, PATH_MAX, "/dev/input/%s", event_identifier);
        return 0;
      }
    }
  }
  return 0;
}

STAGE int find_touchpad_event_path__post_fopen(FILE *fp, char *out_path) {
  char line[256];
  int touchpad = 0;

  while (true) {
    bool empty_line = fgets(line, sizeof(line), fp) == NULL;

    char first_char = line[0];
    bool end_of_section =
        empty_line || first_char == '\n' || first_char == '\r';

    if (end_of_section) {
      touchpad = 0;
      continue;
    }

    if (empty_line) {
      break;
    }

    if (strncmp(line, "N: Name=", 8) == 0) {
      touchpad += strcasestr(line, "touchpad") != NULL;
    }

    bool handler_prefix = strncmp(line, "H: Handlers=", 12) == 0;
    if (handler_prefix) {

      touchpad += strstr(line, "mouse") != NULL;
      char *event_identifier = strstr(line, "event");
      char *event_end = strstr(event_identifier, " ");

      // if space is found, terminate string there,
      // else event name is at the end already null terminated
      if (event_end != NULL) {
        *event_end = 0;
      }

      if (event_identifier && touchpad == 2) {
        snprintf(out_path, PATH_MAX, "/dev/input/%s", event_identifier);
        return 0;
      }
    }
  }
  return 0;
}

int find_touchpad_event_path(char *out_path) {
  FILE *fp = fopen(PROC_INPUT_DEVICES, "r");
  if (!fp) {
    perror("Failed to open " PROC_INPUT_DEVICES);
    return -1;
  }

  int ret = find_touchpad_event_path__post_fopen(fp, out_path);
  fclose(fp);
  return ret;
}

STAGE int main__post_uring_init(struct io_uring ring, struct io_uring_cqe *cqe);
STAGE int main__post_keyboard_open(device_context keyboard,
                                   struct io_uring ring,
                                   struct io_uring_cqe *cqe);
STAGE int main__post_keyboard_grab(device_context keyboard,
                                   struct io_uring ring,
                                   struct io_uring_cqe *cqe);
STAGE int main__post_uinput_open(device_context keyboard,
                                 device_context touchpad, struct io_uring ring,
                                 struct io_uring_cqe *cqe);
STAGE int main__post_virtual_device_create(modifier *keys,
                                           device_context keyboard,
                                           device_context touchpad,
                                           struct io_uring ring,
                                           struct io_uring_cqe *cqe);
STAGE int main__post_touchpad_open(device_context keyboard,
                                   device_context touchpad,
                                   struct io_uring ring,
                                   struct io_uring_cqe *cqe);
int main() {
  int ret = 0;

  struct io_uring ring;
  struct io_uring_cqe *cqe;

  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
    perror("io_uring initialization failed");
    return 1;
  }

  ret = main__post_uring_init(ring, cqe);
  io_uring_queue_exit(&ring);

  return ret;
}

int main__post_uring_init(struct io_uring ring, struct io_uring_cqe *cqe) {
  int ret = 0;

  char device_path[PATH_MAX];

  if (find_keyboard_event_path(device_path) < 0) {
    fprintf(stderr, "unable to find a path to the keyboard device\n");
    return 1;
  }

  device_context keyboard = {.path = device_path,
                             .fd = open(device_path, O_RDWR)};
  if (keyboard.fd < 0) {
    fprintf(stderr,
            "failed to open handle to input device %s: currently skipped: "
            "ensure you are root\n",
            keyboard.path);
    return 1;
  }

  ret = main__post_keyboard_open(keyboard, ring, cqe);
  close(keyboard.fd);

  return ret;
}

int main__post_keyboard_open(device_context keyboard, struct io_uring ring,
                             struct io_uring_cqe *cqe) {
  int ret = 0;

  if (ioctl(keyboard.fd, EVIOCSCLOCKID, &clk) <
      0) { // force monotonic timestamps
    perror("failed to set monotonic clock");
    return 1;
  }

  sleep(1);

  if (ioctl(keyboard.fd, EVIOCGRAB, 1) < 0) {
    perror("failed to grab device exclusively");
    return 1;
  }

  ret = main__post_keyboard_grab(keyboard, ring, cqe);
  ioctl(keyboard.fd, EVIOCGRAB, 0);

  return ret;
}

int main__post_keyboard_grab(device_context keyboard, struct io_uring ring,
                             struct io_uring_cqe *cqe) {
  int ret = 0;

  char device_path[PATH_MAX];
  if (find_touchpad_event_path(device_path) < 0) {
    fprintf(stderr, "unable to find a path to the touchpad device\n");
    return 1;
  }

  printf("%s\n", device_path);
  device_context touchpad = {.path = device_path,
                             .fd = open(device_path, O_RDONLY)};
  if (touchpad.fd < 0) {
    fprintf(stderr,
            "failed to open handle to input device %s: currently skipped: "
            "ensure you are root\n",
            touchpad.path);
    return 1;
  }
  if (ioctl(touchpad.fd, EVIOCSCLOCKID, &clk) <
      0) { // force monotonic timestamps
    perror("failed to set monotonic clock");
    return 1;
  }

  ret = main__post_touchpad_open(keyboard, touchpad, ring, cqe);
  close(touchpad.fd);
  return ret;
}

int main__post_touchpad_open(device_context keyboard, device_context touchpad,
                             struct io_uring ring, struct io_uring_cqe *cqe) {

  int ret = 0;

  ui = open("/dev/uinput", O_NONBLOCK | O_WRONLY);
  if (ui < 0) {
    perror("failed to open uinput");
    return 1;
  }

  ret = main__post_uinput_open(keyboard, touchpad, ring, cqe);
  close(ui);

  return ret;
}

int main__post_uinput_open(device_context keyboard, device_context touchpad,
                           struct io_uring ring, struct io_uring_cqe *cqe) {
  int ret = 0;

  uint64_t last_release_ms = 0;
  modifier keys[N_MODFIERS];
  memset(keys, 0, sizeof(keys));

  // source: https://www.kernel.org/doc/html/v4.12/input/uinput.html
  struct uinput_setup usetup;
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x7047;
  usetup.id.product = 0x1337;
  strcpy(usetup.name, "sticky keys daemon");

  if (ioctl(ui, UI_SET_EVBIT, 1) < 0) {
    perror("failed to set EVBIT");
    return 1;
  }

  for (int i = 0; i < KEY_CNT; ++i) {
    if (ioctl(ui, UI_SET_KEYBIT, i) < 0) {
      perror("failed to set KEYBIT");
      return 1;
    }
  }

  if (ioctl(ui, UI_DEV_SETUP, &usetup) < 0) {
    perror("failed to set up virtual device");
    return 1;
  }

  if (ioctl(ui, UI_DEV_CREATE) < 0) {
    perror("failed to create virtual device");
    return 1;
  }

  sleep(1);
  ret = main__post_virtual_device_create(keys, keyboard, touchpad, ring, cqe);
  sleep(1);
  ioctl(ui, UI_DEV_DESTROY);

  return ret;
}

int main__post_virtual_device_create(modifier *keys, device_context keyboard,
                                     device_context touchpad,
                                     struct io_uring ring,
                                     struct io_uring_cqe *cqe) {
  queue_device_read(&ring, &keyboard);
  queue_device_read(&ring, &touchpad);

  state_machine machine = {.keys = keys,
                           .capsl = false,
                           .device_fd = keyboard.fd,
                           .virtual_fd = ui,
                           .buffer_event = NULL};

  puts("ring submitted");
  io_uring_submit(&ring);

  int tfd = timerfd_create(clk, 0);
  if (tfd == -1) {
    perror("failed to create timerfd");
    return EXIT_FAILURE;
  }

  struct timespec now;
  if (clock_gettime(clk, &now) < 0) {
    perror("failed to get current time");
    close(tfd);
    return EXIT_FAILURE;
  }

  // TODO: timer for touchpad tap auto unlatch
  struct itimerspec new_value;
  new_value.it_value.tv_sec = now.tv_sec + 5;
  new_value.it_value.tv_nsec = now.tv_nsec;
  new_value.it_interval.tv_sec = 0;
  new_value.it_interval.tv_nsec = 0;

  if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &new_value, NULL) < 0) {
    perror("failed ot set timer request");
    close(tfd);
    return EXIT_FAILURE;
  }

  uint64_t expired;
  queue_time_read(&ring, tfd, &expired);

  while (true) {
    int got_cqe = io_uring_wait_cqe(&ring, &cqe);
    if (got_cqe < 0) {
      if (got_cqe == -EWOULDBLOCK || got_cqe == -EAGAIN || got_cqe == -EINTR) {
        continue;
      }
      printf("kernel returned error on waiting for event: %d", got_cqe);
      break;
    }

    if (cqe->res == sizeof(expired)) {
      puts("timer notification: expired");
      io_uring_cqe_seen(&ring, cqe);
      continue;
    };

    device_context *context = io_uring_cqe_get_data(cqe);

    if (cqe->res != INPUT_EVENT_SIZE) {
      fprintf(stderr, "incomplete input event data found for %s: skipping\n",
              context->path);
      continue;
    }

    struct input_event *event = &(context->event);
    handle_event(&machine, event);

    if (context) {
      queue_device_read(&ring, context);
      io_uring_submit(&ring);
    }

    io_uring_cqe_seen(&ring, cqe);
  }

  return 0;
}
