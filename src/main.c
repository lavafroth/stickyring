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
#include <unistd.h>

#define STATE_MACHINE_IMPLEMENTATION
#include "state_machine.h"

#define QUEUE_DEPTH 16
#define PROC_INPUT_DEVICES "/proc/bus/input/devices"

unsigned int clk = CLOCK_MONOTONIC;

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

void queue_timeout(IoUring *ring, uint64_t active_timer_id, const struct __kernel_timespec *ts) {
  SQE *sqe = io_uring_get_sqe(ring);
  if (!sqe) {
    return;
  }

  io_uring_prep_timeout(sqe, ts, 0, 0);
  io_uring_sqe_set_data64(sqe, active_timer_id);
}

typedef enum {
  TIMER_CANCEL,
  TIMER_TRIGGER,
  TIMER_CONTINUE,
} TimerAction;

typedef struct {
  uint64_t timeout;
  uint8_t modifiers;
  bool touchpad;
  uint64_t touchpad_timeout;
  int64_t touchpad_grace_radius;
} Config;

int yesnt(const char *line, const char *pattern, unsigned int len) {
  if (strncmp(line, pattern, len) != 0) {
    return 0;
  }
  if (strncmp(line + len, "yes", 3) == 0 ||
      strncmp(line + len, "true", 4) == 0) {
    return 1;
  }
  if (strncmp(line + len, "no", 2) == 0 ||
      strncmp(line + len, "false", 5) == 0) {
    return 0;
  }
  return -1;
}

Config config;
int read_config(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) {
    fprintf(stderr, "failed to open config file %s", path);
    return EXIT_FAILURE;
  }

  char line[256];

  bool in_touchpad_section = false;
  uint64_t timeout = 300;
  uint8_t modifiers = 0b00000000;
  bool touchpad = false;
  uint64_t touchpad_timeout = 500;
  int64_t touchpad_grace_radius = 300;

  while (true) {
    bool empty_line = fgets(line, sizeof(line), fp) == NULL;
    if (empty_line) {
      break;
    }

    if (line[0] == ';')
      continue;

    if (strncmp(line, "[touchpad]", 10) == 0) {
      touchpad = true;
      in_touchpad_section = true;
      continue;
    }

    if (in_touchpad_section && strncmp(line, "timeout=", 8) == 0) {
      sscanf(line + 8, "%" SCNu64, &touchpad_timeout);
      continue;
    }
    if (in_touchpad_section && strncmp(line, "grace=", 6) == 0) {
      sscanf(line + 8, "%" SCNu64, &touchpad_grace_radius);
      continue;
    }

    if (in_touchpad_section && line[0] == '\n') {
      in_touchpad_section = false;
      continue;
    }

    if (strncmp(line, "timeout=", 8) == 0) {
      sscanf(line + 8, "%" SCNu64, &timeout);
      continue;
    }

    char *modifier_names[] = {
        "leftshift=", "rightshift=", "leftctrl=", "rightctrl=",
        "leftmeta=",  "rightmeta=",  "leftalt=",  "rightalt="};
    int lens[] = {10, 11, 9, 10, 9, 10, 8, 9};
    for (int i = 0; i < 8; i++) {
      int mod = yesnt(line, modifier_names[i], lens[i]);
      if (mod < 0)
        fprintf(stderr, "failed to parse config line %s", line);
      if (mod) {
        modifiers |= (1 << i);
        break;
      }
    }
  }

  config.timeout = timeout;
  config.modifiers = modifiers;
  config.touchpad = touchpad;
  config.touchpad_timeout = touchpad_timeout;
  config.touchpad_grace_radius = touchpad_grace_radius;

  fclose(fp);
  return EXIT_SUCCESS;
}

int64_t tap_euclidean_distance_square(StateMachine *machine) {
  int64_t dx = (int64_t)(machine->touch.ended.x - machine->touch.began.x);
  int64_t dy = (int64_t)(machine->touch.ended.y - machine->touch.began.y);
  return dx * dx + dy * dy;
}

TimerAction handle_event(StateMachine *machine, InputEvent *event) {
  uint16_t code = event->code;
  int value = event->value;
  uint16_t type = event->type;

  bool is_key = type == EV_KEY;
  bool click_not_touch = is_key && (code == BTN_LEFT ||       //
                                    code == BTN_RIGHT ||      //
                                    code == BTN_TOOL_FINGER); //
  bool click_is_touch = is_key && code == BTN_TOUCH;
  bool key_not_click = is_key && !(click_is_touch || click_not_touch);
  bool touch_pressed = click_is_touch && value;
  bool second_tap_in_time =
      touch_pressed &&
      event_time_ms(event) < machine->touch.last_ms + config.touchpad_timeout;
  bool touch_released = click_is_touch && !value;
  bool is_coordinate = type == EV_ABS;
  bool x_axis = is_coordinate && code == ABS_X;
  bool y_axis = is_coordinate && code == ABS_Y;
  long threshold = config.touchpad_grace_radius;
  bool released_within_threshold_circle =
      touch_released &&
      tap_euclidean_distance_square(machine) < threshold * threshold;
  bool released_beyond_threshold_circle =
      touch_released && !released_within_threshold_circle;

  if (click_not_touch)
    return TIMER_CONTINUE;

  if (touch_pressed)
    machine->touch.stage = INITIATED;

  if (second_tap_in_time)
    return TIMER_CANCEL;

  if (touch_released)
    machine->touch.last_ms = event_time_ms(event);

  if (released_within_threshold_circle && state_machine__any_latched(machine))
    return TIMER_TRIGGER;

  if (released_beyond_threshold_circle) {
    state_machine__free_latched(machine);
    machine->tainted = true;
    state_machine__flush(machine);
    // puts("drag lifted, keys unlatched");
  }

  if (key_not_click)
    state_machine__interact(machine, event);

  // the kernel sends x and y values in separate events:
  // first note x value, then y
  if (x_axis && machine->touch.stage == INITIATED) {
    machine->touch.began.x = value;
    machine->touch.stage = NOTED_BEGIN_X;
  }

  if (y_axis && machine->touch.stage == NOTED_BEGIN_X) {
    machine->touch.began.y = value;
    machine->touch.stage = NOTED_BEGIN_XY;
  }

  if (x_axis)
    machine->touch.ended.x = value;
  if (y_axis)
    machine->touch.ended.y = value;

  state_machine__flush(machine);
  return TIMER_CONTINUE;
}

int find_device_path(char *out_path, bool want_touchpad) {
  FILE *fp = fopen(PROC_INPUT_DEVICES, "r");
  if (!fp) {
    perror("Failed to open " PROC_INPUT_DEVICES);
    return EXIT_FAILURE;
  }

  char *name_pattern = "keyboard";
  char *id_pattern = "kbd";

  if (want_touchpad) {
    name_pattern = "touchpad";
    id_pattern = "mouse";
  }

  char line[256];
  int evidence = 0;

  while (true) {
    bool empty_line = fgets(line, sizeof(line), fp) == NULL;

    char first_char = line[0];
    bool end_of_section =
        empty_line || first_char == '\n' || first_char == '\r';

    if (end_of_section) {
      evidence = 0;
      continue;
    }

    if (empty_line) {
      break;
    }

    if (strncmp(line, "N: Name=", 8) == 0) {
      evidence += strcasestr(line, name_pattern) != NULL;
    }

    bool handler_prefix = strncmp(line, "H: Handlers=", 12) == 0;
    if (handler_prefix) {

      evidence += strstr(line, id_pattern) != NULL;
      char *event_identifier = strstr(line, "event");
      char *event_end = strstr(event_identifier, " ");

      // if space is found, terminate string there,
      // else event name is at the end already null terminated
      if (event_end != NULL) {
        *event_end = 0;
      }

      if (event_identifier && evidence == 2) {
        snprintf(out_path, PATH_MAX, "/dev/input/%s", event_identifier);
        goto defer_close_file;
      }
    }
  }

defer_close_file:
  fclose(fp);
  return EXIT_SUCCESS;
}

int main(int argc, const char **argv) {

  IoUring ring;
  CQE *cqe;

  if (argc > 1)
    read_config(argv[1]);
  else {
    config.modifiers = 0b11111111;
    config.timeout = 300;
    config.touchpad = false;
  }

  Modifier keys[N_MODFIERS];
  memset(keys, 0, sizeof(keys));

  char keyboard_path[PATH_MAX];
  if (find_device_path(keyboard_path, false) < 0) {
    fprintf(stderr, "unable to find a path to the keyboard device\n");
    return EXIT_FAILURE;
  }

  DeviceContext keyboard = {.path = keyboard_path,
                            .fd = open(keyboard_path, O_RDWR)};

  if (keyboard.fd < 0) {
    fprintf(stderr,
            "failed to open handle to input device %s: currently skipped: "
            "ensure you are root\n",
            keyboard.path);
    return EXIT_FAILURE;
  }

  if (ioctl(keyboard.fd, EVIOCSCLOCKID, &clk) <
      0) { // force monotonic timestamps
    perror("failed to set monotonic clock");
    goto deferred_close_keyboard;
  }

  sleep(1);

  if (ioctl(keyboard.fd, EVIOCGRAB, 1) < 0) {
    perror("failed to grab device exclusively");
    goto deferred_close_keyboard;
  }

  DeviceContext touchpad = {.path = NULL, .fd = 0};

  if (config.touchpad) {
    char touchpad_path[PATH_MAX];
    if (find_device_path(touchpad_path, true) < 0) {
      fprintf(stderr, "unable to find a path to the touchpad device\n");
      goto deferred_ungrab_keyboard;
    }

    touchpad.path = touchpad_path;
    touchpad.fd = open(touchpad_path, O_RDONLY);

    if (touchpad.fd < 0) {
      fprintf(stderr,
              "failed to open handle to touchpad input device %s: currently "
              "skipped: "
              "ensure you are root\n",
              touchpad.path);
      goto deferred_ungrab_keyboard;
    }
    if (ioctl(touchpad.fd, EVIOCSCLOCKID, &clk) < 0) {
      perror("failed to set monotonic clock");
      goto deferred_touchpad_close;
    }
  }

  int ui = open("/dev/uinput", O_NONBLOCK | O_WRONLY);
  if (ui < 0) {
    perror("failed to open uinput");
    goto deferred_touchpad_close;
  }

  // source: https://www.kernel.org/doc/html/v4.12/input/uinput.html
  USetup usetup;
  memset(&usetup, 0, sizeof(USetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x7047;
  usetup.id.product = 0x1337;
  strcpy(usetup.name, "sticky keys daemon");

  if (ioctl(ui, UI_SET_EVBIT, 1) < 0) {
    perror("failed to set EVBIT");
    goto deferred_close_virtual_device;
  }

  for (int i = 0; i < KEY_CNT; ++i) {
    if (ioctl(ui, UI_SET_KEYBIT, i) < 0) {
      perror("failed to register which keys the virtual input will be able to "
             "press");
      goto deferred_close_virtual_device;
    }
  }

  if (ioctl(ui, UI_DEV_SETUP, &usetup) < 0) {
    perror("failed to set up virtual device");
    goto deferred_close_virtual_device;
  }

  if (ioctl(ui, UI_DEV_CREATE) < 0) {
    perror("failed to create virtual device");
    goto deferred_close_virtual_device;
  }

  sleep(1);

  struct __kernel_timespec timer = {
    .tv_nsec = ((long) config.touchpad_timeout) * 1000 * 1000,
    .tv_sec = 0,
  };

  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
    perror("io_uring initialization failed");
    goto deferred_destroy_virtual_device;
  }

  queue_device_read(&ring, &keyboard);

  if (config.touchpad)
    queue_device_read(&ring, &touchpad);

  StateMachine machine = {
      .keys = keys,
      .capsl = false,
      .device_fd = keyboard.fd,
      .virtual_fd = ui,
      .mask = config.modifiers,
      .buffer_event = NULL,
      .timeout = config.timeout,
      .active_timer_id = 0,
      .tainted = false,
  };

  puts("ring submitted");
  io_uring_submit(&ring);

  while (true) {
    int result = io_uring_wait_cqe(&ring, &cqe);

    // admissible errors
    if (result == -EWOULDBLOCK || result == -EAGAIN || result == -EINTR) {
      continue;
    }

    if (result < 0) {
      printf("kernel returned error on waiting for event: %d", result);
      goto deferred_queue_exit;
    }


    if (cqe->res == -ETIME) {
      if (machine.active_timer_id != cqe->user_data) goto deferred_cqe_seen;
      state_machine__free_latched(&machine);
      machine.tainted = true;
      state_machine__flush(&machine);
      goto deferred_cqe_seen;
    }

    DeviceContext *context = io_uring_cqe_get_data(cqe);

    if (cqe->res != sizeof(InputEvent)) {
      fprintf(stderr, "incomplete input event data found for %s: skipping\n",
              context->path);
      goto deferred_cqe_seen;
    }

    InputEvent *event = &(context->event);
    TimerAction res = handle_event(&machine, event);

    if (res == TIMER_CANCEL) machine.active_timer_id = (machine.active_timer_id + 1) % 256;
    if (res == TIMER_TRIGGER) {
      queue_timeout(&ring, machine.active_timer_id, &timer);
      io_uring_submit(&ring);
    }
    if (context) {
      queue_device_read(&ring, context);
      io_uring_submit(&ring);
    }

  deferred_cqe_seen:
    io_uring_cqe_seen(&ring, cqe);
  }

deferred_queue_exit:
  io_uring_queue_exit(&ring);

  sleep(1);

deferred_destroy_virtual_device:
  ioctl(ui, UI_DEV_DESTROY);

deferred_close_virtual_device:
  close(ui);

deferred_touchpad_close:
  // touchpad support can be disabled
  // hence need to check if device was ever opened
  if (touchpad.fd)
    close(touchpad.fd);

deferred_ungrab_keyboard:
  ioctl(keyboard.fd, EVIOCGRAB, 0);

deferred_close_keyboard:
  close(keyboard.fd);

  return EXIT_SUCCESS;
}
