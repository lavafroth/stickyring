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

#include "modifier.c"

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

const int MODIFIERS[] = {KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL,
                         KEY_RIGHTCTRL, KEY_LEFTMETA,   KEY_RIGHTMETA,
                         KEY_LEFTALT,   KEY_RIGHTALT};
const int N_MODFIERS = sizeof(MODIFIERS) / sizeof(MODIFIERS[0]);

int modifier_index(uint16_t code) {
  for (int i = 0; i < N_MODFIERS; ++i)
    if (MODIFIERS[i] == code)
      return i;
  return -1;
}

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

uint64_t event_time_ms(const struct input_event *ev) {
  struct timeval time = ev->time;
  return ((uint64_t)time.tv_sec * 1000) + ((uint64_t)time.tv_usec / 1000);
}

int emit(uint16_t type, uint16_t code, int32_t value) {
  struct input_event event = { { 0, 0 }, type, code, value };
  return write(ui, &event, sizeof(event));
}

const int NEXT_STATE[3] = {LATCHED, FREE, FREE};

void handle_event(struct input_event *event, modifier *keys) {
  if (event->type != EV_KEY) {
    return;
  }

  int i = modifier_index(event->code);
  if (i == -1) {
    if (write(ui, event, sizeof(struct input_event)) < 0) {
      perror("failed to passthrough event to virtual device");
    };
    for (int j = 0; j < N_MODFIERS; ++j) {
      if (keys[j].flag == LATCHED) {
        keys[j].flag = FREE;
        emit(EV_KEY, MODIFIERS[j], 0);
        display(keys, j);
      }
    }
    emit(EV_SYN, SYN_REPORT, 0);
    return;
  }

  if (event->value == 0) { // 1 = pressed
    return;
  };

  modifier *key = keys + i;
  uint64_t current_release_ms = event_time_ms(event);
  uint64_t elapsed_ms = current_release_ms - key->last;

  int flag = LOCKED;
  if (elapsed_ms > 200) {
    flag = NEXT_STATE[key->flag];
  }

  key->flag = flag;
  key->last = current_release_ms;
  display(keys, i);

  emit(EV_KEY, event->code, flag != FREE);
  emit(EV_SYN, SYN_REPORT, 0);
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
      is_keyboard += strstr(line, "keyboard") != NULL;
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

STAGE int main__post_uring_init(const char *device_path, struct io_uring ring,
                                struct io_uring_cqe *cqe);
STAGE int main__post_keyboard_open(device_context keyboard,
                                   struct io_uring ring,
                                   struct io_uring_cqe *cqe);
STAGE int main__post_keyboard_grab(device_context keyboard,
                                   struct io_uring ring,
                                   struct io_uring_cqe *cqe);
STAGE int main__post_uinput_open(device_context keyboard, struct io_uring ring,
                                 struct io_uring_cqe *cqe);
STAGE int main__post_virtual_device_create(modifier *keys,
                                           device_context keyboard,
                                           struct io_uring ring,
                                           struct io_uring_cqe *cqe);

int main() {
  int ret = 0;

  char device_path[PATH_MAX];

  if (find_keyboard_event_path(device_path) < 0) {
    fprintf(stderr, "unable to find a path to the keyboard device\n");
    return 1;
  }

  struct io_uring ring;
  struct io_uring_cqe *cqe;

  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
    perror("io_uring initialization failed");
    return 1;
  }

  ret = main__post_uring_init(device_path, ring, cqe);
  io_uring_queue_exit(&ring);

  return ret;
}

int main__post_uring_init(const char *device_path, struct io_uring ring,
                          struct io_uring_cqe *cqe) {
  int ret = 0;

  device_context keyboard = {.path = device_path, .fd = UNINITIALIZED_FD};
  keyboard.fd = open(keyboard.path, O_RDONLY);
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

  ui = open("/dev/uinput", O_NONBLOCK | O_WRONLY);
  if (ui < 0) {
    perror("failed to open uinput");
    return 1;
  }

  ret = main__post_uinput_open(keyboard, ring, cqe);
  close(ui);

  return ret;
}

int main__post_uinput_open(device_context keyboard, struct io_uring ring,
                           struct io_uring_cqe *cqe) {
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
  ret = main__post_virtual_device_create(keys, keyboard, ring, cqe);
  sleep(1);
  ioctl(ui, UI_DEV_DESTROY);

  return ret;
}

int main__post_virtual_device_create(modifier *keys, device_context keyboard,
                                     struct io_uring ring,
                                     struct io_uring_cqe *cqe) {
  queue_device_read(&ring, &keyboard);

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
      break;
    }

    if (cqe->res == sizeof(expired)) {
      printf("timer notification: expired");
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
    handle_event(event, keys);

    io_uring_cqe_seen(&ring, cqe);
    if (context && context->fd != UNINITIALIZED_FD) {
      queue_device_read(&ring, context);
      io_uring_submit(&ring);
    }
  }

  return 0;
}
