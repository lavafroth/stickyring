#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>

typedef enum {
  FREE = 0,
  LATCHED = 1,
  LOCKED = 2,
} modifier_state;

typedef struct {
  uint64_t last;
  modifier_state flag;
} modifier;

typedef struct {
  modifier *keys;
  bool capsl;
  int device_fd;
  int virtual_fd;
  struct input_event* buffer_event;
} state_machine;

#endif

#ifdef STATE_MACHINE_IMPLEMENTATION

#include <linux/input.h>

static const int MODIFIERS[] = {KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL,
                                KEY_RIGHTCTRL, KEY_LEFTMETA,   KEY_RIGHTMETA,
                                KEY_LEFTALT,   KEY_RIGHTALT};
const int N_MODFIERS = sizeof(MODIFIERS) / sizeof(MODIFIERS[0]);

const int NEXT_STATE[3] = {LATCHED, FREE, FREE};

void display(modifier *keys, int i) {
  modifier key = keys[i];
  char *state_repr = "free";
  if (key.flag == LATCHED)
    state_repr = "latched";
  if (key.flag == LOCKED)
    state_repr = "locked";
  printf("%s for %d at %lu ms\n", state_repr, i, key.last);
}

int modifier_index(uint16_t code) {
  for (int i = 0; i < N_MODFIERS; ++i)
    if (MODIFIERS[i] == code)
      return i;
  return -1;
}

int emit_event(int fd, uint16_t type, uint16_t code, int32_t value) {
  struct input_event event = {{0, 0}, type, code, value};
  return write(fd, &event, sizeof(event));
}

// Free all latched keys on the virtual device.
// Useful for clearing latched states when either a non modifier key is pressed
// or TODO: a tap, tap-drag, tap-drag-drag finishes and touchpad support is enabled.
void state_machine__free_latched(state_machine *machine) {
  for (int j = 0; j < N_MODFIERS; ++j)
    if (machine->keys[j].flag == LATCHED)
      machine->keys[j].flag = FREE;
}

void state_machine__flush(state_machine *machine) {
  // consume buffer event, writing it to virtual keyboard
  if (machine->buffer_event) {
    int res = write(machine->virtual_fd, machine->buffer_event, sizeof(struct input_event));
    if (res < 0) {
      perror("failed to passthrough event to virtual device");
    };
    machine->buffer_event = NULL;
  }

  bool capsl = false;
  for (int j = 0; j < N_MODFIERS; ++j) {
    bool latched_or_locked = machine->keys[j].flag != FREE;
    capsl |= latched_or_locked;
    emit_event(machine->virtual_fd, EV_KEY, MODIFIERS[j], latched_or_locked);
  }
  emit_event(machine->virtual_fd, EV_SYN, SYN_REPORT, 0);

  if (machine->capsl != capsl) {
    machine->capsl = capsl;
    emit_event(machine->device_fd, EV_LED, LED_CAPSL, capsl);
    emit_event(machine->device_fd, EV_SYN, SYN_REPORT, 0);
  }
}

uint64_t event_time_ms(const struct input_event *ev) {
  struct timeval time = ev->time;
  return ((uint64_t)time.tv_sec * 1000) + ((uint64_t)time.tv_usec / 1000);
}

void state_machine__interact_modifier(state_machine *machine, struct input_event *event, int i) {
  modifier *key = machine->keys + i;
  uint64_t current_release_ms = event_time_ms(event);
  uint64_t elapsed_ms = current_release_ms - key->last;

  int flag = LOCKED;
  if (elapsed_ms > 200) {
    flag = NEXT_STATE[key->flag];
  }

  key->flag = flag;
  key->last = current_release_ms;
}

void state_machine__interact(state_machine *machine, struct input_event *event) {
  int i = modifier_index(event->code);
  if (i == 5) exit(1);
  if (i < 0) {
    state_machine__free_latched(machine);
    machine->buffer_event = event;
    return;
  }
  if (event->value != 0) 
    state_machine__interact_modifier(machine, event, i);
}

#endif
