#include <bits/time.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/time.h>
#include <liburing.h>
#include <string.h>

#define QUEUE_DEPTH 16
#define PROC_INPUT_DEVICES "/proc/bus/input/devices"

unsigned int clk = CLOCK_MONOTONIC;
static int ui = -1;

typedef struct {
  int fd;
  const char* path;
  struct input_event event;
} device_context;

int key_to_state(uint16_t code) {
  switch (code) {
    // shift
    case 42: return 0;
    case 54: return 1;

    // control
    case 29: return 2;
    case 97: return 3;

    // alt
    case 56: return 4;
    case 100: return 5;

    // super
    case 125: return 6;
    case 126: return 7;
    default: return -1;
  }
}

const int FREE = 0;
const int LATCHED = 1;
const int LOCKED = 2;

typedef struct {
  uint64_t last;
  int flag;
} state;

const int INPUT_EVENT_SIZE = sizeof(struct input_event);
const int UNINITIALIZED_FD = -1;

void queue_device_read(struct io_uring* ring, device_context* context) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  if (!sqe) {
    return;
  }

  io_uring_prep_read(sqe, context->fd, &(context->event), INPUT_EVENT_SIZE, 0);
  io_uring_sqe_set_data(sqe, context);
}


uint64_t time_ms(struct timeval time) {
  return ((uint64_t)time.tv_sec * 1000) + ((uint64_t)time.tv_usec / 1000);
}

uint64_t event_time_ms(const struct input_event *ev) {
  return time_ms(ev->time);
}

void display(state *key, int i) {
  char *state_repr = "free";
  if (key->flag == LATCHED) {
    state_repr = "latched";
  }
  if (key->flag == LOCKED) {
    state_repr = "locked";
  }
  printf("%s for %d %lu ms\n", state_repr, i, key->last);

}

// #define DEFER(cleanup) for (int _done = 0; !_done; (cleanup), _done = 1)

int emit(uint16_t type, uint16_t code, int32_t value) {
  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.code = code;
  ev.type = type;
  ev.value = value;

  return write(ui, &ev, sizeof(ev));
}


const int NEXT_STATE[3] = {LATCHED, FREE, FREE};
const int KEYCODES[8] = {42,54,29,97,56,100,125,126};

void handle_event(struct input_event* event, state* keys) {
    if (event->type != EV_KEY) {
      return;
    }

    int i = key_to_state(event->code);
    if (i == -1) {
      if (write(ui, event, sizeof(struct input_event)) < 0) {
        perror("failed to passthrough event to virtual device");
      };
      for (int j = 0; j < 8; ++j) {
        if (keys[j].flag == LATCHED) {
          keys[j].flag = FREE;
          emit(EV_KEY, KEYCODES[j], 0);
          display(keys + j, j);
        }
      }
      emit(EV_SYN, SYN_REPORT, 0);
      return;
    }

    if (event->value == 0) { // 1 = pressed
      return;
    };


    state *key = keys + i;
    uint64_t current_release_ms = event_time_ms(event);
    uint64_t elapsed_ms = current_release_ms - key->last;

    int flag = LOCKED;
    if (elapsed_ms > 200) {
      flag = NEXT_STATE[key->flag];
    }

    key->flag = flag;
    key->last = current_release_ms;
    display(key, i);

    emit(EV_KEY, event->code, flag != FREE);
    emit(EV_SYN, SYN_REPORT, 0);
  
}

int find_keyboard_event_path(char *out_path) {
    FILE *fp = fopen(PROC_INPUT_DEVICES, "r");
    if (!fp) {
        perror("Failed to open " PROC_INPUT_DEVICES);
        return -1;
    }

    char line[256];
    bool is_keyboard = false;

    while (true) {
      bool empty_line = fgets(line, sizeof(line), fp) == NULL;

      char first_char = line[0];
      bool end_of_section = empty_line || first_char == '\n' || first_char == '\r';

      if (end_of_section) {
          is_keyboard = false;
          continue;
      }

      if (empty_line) {
        break;
      }

      bool handler_prefix = strncmp(line, "H: Handlers=", 12) == 0;
      if (handler_prefix) {

        is_keyboard |= strstr(line, "kbd") != NULL;
        char *event_identifier = strstr(line, "event");
        char *event_end = strstr(event_identifier, " ");

        // if space is found, terminate string there,
        // else event name is at the end already null terminated
        if (event_end != NULL) {
          *event_end = 0;
        }

        if (event_identifier && is_keyboard) {
            snprintf(out_path, PATH_MAX, "/dev/input/%s", event_identifier);
            fclose(fp);
            return 0;
        }

      }
    }

    fclose(fp);
    return -1;
}

int main() {
  char device_path[PATH_MAX];

  if (find_keyboard_event_path(device_path) < 0) {
    fprintf(stderr, "unable to find a path to the keyboard device\n");
    return 1;
  }

  device_context keyboard = { .path = device_path, .fd = UNINITIALIZED_FD };
  
  struct io_uring ring;
  struct io_uring_cqe *cqe;

  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
    perror("io_uring initialization failed");
    return 1;
  }

  keyboard.fd = open(keyboard.path, O_RDONLY);

  if (ioctl(keyboard.fd, EVIOCSCLOCKID, &clk) < 0) { // force monotonic timestamps
      perror("failed to set monotonic clock");
      close(keyboard.fd);
      return 1;
  }

  sleep(1);

  if (ioctl(keyboard.fd, EVIOCGRAB, 1) < 0) {
      perror("failed to grab device exclusively");
      close(keyboard.fd);
      return 1;
  }

  if (keyboard.fd < 0) {
    fprintf(stderr, "failed to open handle to input device %s: currently skipped: ensure you are root\n", keyboard.path);
    return 1;
  }
  queue_device_read(&ring, &keyboard);

  printf("ring submitted\n");
  io_uring_submit(&ring);

  uint64_t last_release_ms = 0;
  state keys[8];
  memset(keys, 0, sizeof(keys));
  
  // source: https://www.kernel.org/doc/html/v4.12/input/uinput.html
  struct uinput_setup usetup;
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x7047;
  usetup.id.product = 0x1337;
  strcpy(usetup.name, "sticky keys daemon");

  ui = open("/dev/uinput", O_NONBLOCK | O_WRONLY);
  if (ui < 0) {
    perror("failed to open uinput");
    return 1;
  }

  if(ioctl(ui, UI_SET_EVBIT, 1) < 0) {
    perror("failed to set EVBIT");
    return 1;
  }

  for (int i = 0; i < KEY_CNT; ++i) {
    if(ioctl(ui, UI_SET_KEYBIT, i) < 0) {
      perror("failed to set KEYBIT");
      return 1;
    }
  }

  if(ioctl(ui, UI_DEV_SETUP, &usetup) < 0) {
    perror("failed to set up virtual device");
    return 1;
  }
  if(ioctl(ui, UI_DEV_CREATE) < 0) {
    perror("failed to create virtual device");
    return 1;
  }

  sleep(1);

  while (true) {
    int ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
      break;
    }


    device_context* context = io_uring_cqe_get_data(cqe);

    if (cqe->res != INPUT_EVENT_SIZE) {
      fprintf(stderr, "incomplete input event data found for %s: skipping\n", context->path);
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

  sleep(1);

  ioctl(ui, UI_DEV_DESTROY);
  close(ui);

  io_uring_queue_exit(&ring);
  ioctl(keyboard.fd, EVIOCGRAB, 0);
  close(keyboard.fd);
  return 0;
}

