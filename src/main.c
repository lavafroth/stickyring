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

typedef struct {
  int fd;
  const char *path;
  InputEvent event;
} DeviceContext;

typedef struct io_uring IoUring;
typedef struct io_uring_sqe SQE;
typedef struct io_uring_cqe CQE;
typedef struct uinput_setup USetup;

void queue_device_read(IoUring *ring, DeviceContext *context) {
  SQE *sqe = io_uring_get_sqe(ring);
  if (!sqe) {
    return;
  }

  io_uring_prep_read(sqe, context->fd, &(context->event), sizeof(InputEvent),
                     0);
  io_uring_sqe_set_data(sqe, context);
}

int queue_time_read(IoUring *ring, int fd, uint64_t *expiry_couter,
                    time_t duration_ms) {
  struct itimerspec dst;
  struct timespec now;
  if (clock_gettime(clk, &now) < 0) {
    perror("failed to get current time");
    return EXIT_FAILURE;
  }

  const static int ns_per_ms = 1000;
  dst.it_value.tv_sec = now.tv_sec;
  dst.it_value.tv_nsec = now.tv_nsec + duration_ms * ns_per_ms;
  dst.it_interval.tv_sec = 0;
  dst.it_interval.tv_nsec = 0;
  if (timerfd_settime(fd, TFD_TIMER_ABSTIME, &dst, NULL) < 0) {
    perror("failed ot set timer request");
    return EXIT_FAILURE;
  }
  SQE *sqe = io_uring_get_sqe(ring);
  if (sqe)
    io_uring_prep_read(sqe, fd, expiry_couter, sizeof(uint64_t), 0);
  return EXIT_SUCCESS;
}

void handle_event(StateMachine *machine, InputEvent *event) {
  if (event->type == EV_KEY) {
    if (event->code == BTN_LEFT || event->code == BTN_RIGHT ||
        event->code == BTN_TOUCH || event->code == BTN_TOOL_FINGER) {
      puts("respond touch");
    } else {
      state_machine__interact(machine, event);
    }
  }
  if (event->type == EV_ABS) {

    if (event->code == ABS_X || event->code == ABS_Y) {
      puts("respond motion");
    }
  }
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

STAGE int main__post_uring_init(IoUring ring, CQE *cqe);
STAGE int main__post_keyboard_open(DeviceContext keyboard, IoUring ring,
                                   CQE *cqe);
STAGE int main__post_keyboard_grab(DeviceContext keyboard, IoUring ring,
                                   CQE *cqe);
STAGE int main__post_uinput_open(DeviceContext keyboard, DeviceContext touchpad,
                                 IoUring ring, CQE *cqe);
STAGE int main__post_virtual_device_create(Modifier *keys,
                                           DeviceContext keyboard,
                                           DeviceContext touchpad, IoUring ring,
                                           CQE *cqe);
STAGE int main__post_touchpad_open(DeviceContext keyboard,
                                   DeviceContext touchpad, IoUring ring,
                                   CQE *cqe);
STAGE int main__post_timer_create(Modifier *keys, DeviceContext keyboard,
                                  int tfd, DeviceContext touchpad, IoUring ring,
                                  CQE *cqe);
int main() {
  int ret = 0;

  IoUring ring;
  CQE *cqe;

  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
    perror("io_uring initialization failed");
    return 1;
  }

  ret = main__post_uring_init(ring, cqe);
  io_uring_queue_exit(&ring);

  return ret;
}

int main__post_uring_init(IoUring ring, CQE *cqe) {
  int ret = 0;

  char device_path[PATH_MAX];

  if (find_keyboard_event_path(device_path) < 0) {
    fprintf(stderr, "unable to find a path to the keyboard device\n");
    return 1;
  }

  DeviceContext keyboard = {.path = device_path,
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

int main__post_keyboard_open(DeviceContext keyboard, IoUring ring, CQE *cqe) {
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

int main__post_keyboard_grab(DeviceContext keyboard, IoUring ring, CQE *cqe) {
  int ret = 0;

  char device_path[PATH_MAX];
  if (find_touchpad_event_path(device_path) < 0) {
    fprintf(stderr, "unable to find a path to the touchpad device\n");
    return 1;
  }

  printf("%s\n", device_path);
  DeviceContext touchpad = {.path = device_path,
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

int main__post_touchpad_open(DeviceContext keyboard, DeviceContext touchpad,
                             IoUring ring, CQE *cqe) {

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

int main__post_uinput_open(DeviceContext keyboard, DeviceContext touchpad,
                           IoUring ring, CQE *cqe) {
  int ret = 0;

  uint64_t last_release_ms = 0;
  Modifier keys[N_MODFIERS];
  memset(keys, 0, sizeof(keys));

  // source: https://www.kernel.org/doc/html/v4.12/input/uinput.html
  USetup usetup;
  memset(&usetup, 0, sizeof(USetup));
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

int main__post_virtual_device_create(Modifier *keys, DeviceContext keyboard,
                                     DeviceContext touchpad, IoUring ring,
                                     CQE *cqe) {

  int tfd = timerfd_create(clk, 0);
  if (tfd < 0) {
    perror("failed to create timerfd");
    return EXIT_FAILURE;
  }

  int ret = main__post_timer_create(keys, keyboard, tfd, touchpad, ring, cqe);

  close(tfd);
  return ret;
}

int create_timer_spec(struct itimerspec *dst, time_t duration_ms) {
  return EXIT_SUCCESS;
}

int main__post_timer_create(Modifier *keys, DeviceContext keyboard, int tfd,
                            DeviceContext touchpad, IoUring ring, CQE *cqe) {
  queue_device_read(&ring, &keyboard);
  queue_device_read(&ring, &touchpad);

  StateMachine machine = {
      .keys = keys,
      .capsl = false,
      .device_fd = keyboard.fd,
      .virtual_fd = ui,
      .buffer_event = NULL,
      .tainted = false,
  };

  puts("ring submitted");
  io_uring_submit(&ring);

  // TODO: timer for touchpad tap auto unlatch
  uint64_t expiry_indicator;
  queue_time_read(&ring, tfd, &expiry_indicator, 500);

  while (true) {
    int result = io_uring_wait_cqe(&ring, &cqe);

    // admissible errors
    if (result == -EWOULDBLOCK || result == -EAGAIN || result == -EINTR) {
      continue;
    }

    if (result < 0) {
      printf("kernel returned error on waiting for event: %d", result);
      return result;
    }

    if (cqe->res == sizeof(expiry_indicator)) {
      puts("timer notification: expired");
      io_uring_cqe_seen(&ring, cqe);
      continue;
    };

    DeviceContext *context = io_uring_cqe_get_data(cqe);

    if (cqe->res != sizeof(InputEvent)) {
      fprintf(stderr, "incomplete input event data found for %s: skipping\n",
              context->path);
      continue;
    }

    InputEvent *event = &(context->event);
    handle_event(&machine, event);

    if (context) {
      queue_device_read(&ring, context);
      io_uring_submit(&ring);
    }

    io_uring_cqe_seen(&ring, cqe);
  }

  return EXIT_SUCCESS;
}
