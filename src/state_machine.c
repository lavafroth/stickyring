#include "state_machine.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h> // IWYU pragma: keep

static const uint16_t MODIFIERS[] = {KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL,
                                KEY_RIGHTCTRL, KEY_LEFTMETA,   KEY_RIGHTMETA,
                                KEY_LEFTALT,   KEY_RIGHTALT};
const int N_MODIFIERS = sizeof(MODIFIERS) / sizeof(MODIFIERS[0]);

const int NEXT_STATE[3] = {LATCHED, FREE, FREE};

#define TOUCH_RELEASED = 0;
#define TOUCH_HELD = 1;
#define COORDINATE_EMPTY = -1;

void display(Modifier *keys, int i) {
  Modifier key = keys[i];
  char *state_repr = "free";
  if (key.flag == LATCHED)
    state_repr = "latched";
  if (key.flag == LOCKED)
    state_repr = "locked";
  printf("%s for %d at %lu ms\n", state_repr, i, key.last);
}

int modifier_index(uint16_t code) {
  for (int i = 0; i < N_MODIFIERS; ++i)
    if (MODIFIERS[i] == code)
      return i;
  return -1;
}

ssize_t write_ignoring_interrupt(int fd, InputEvent *event) {
  ssize_t result = -EINTR;
  while (result == -EINTR) {
    result = write(fd, event, sizeof(InputEvent));
  }
  return result;
}

ssize_t emit_event(int fd, uint16_t type, uint16_t code, int32_t value) {
  struct input_event event = {{0, 0}, type, code, value};
  return write_ignoring_interrupt(fd, &event);
}

// Free all latched keys on the virtual device.
// Useful for clearing latched states when either a non modifier key is pressed
// or a tap, tap-drag, tap-drag-drag finishes and touchpad support is
// enabled.
void state_machine__free_latched(StateMachine *machine) {
  for (int i = 0; i < N_MODIFIERS; i++) {

    bool unmasked = machine->mask & (1 << i);
    Modifier *key = machine->keys + i;

    if (unmasked && key->flag == LATCHED) {
      key->flag = FREE;
    }
  }
}

bool state_machine__any_latched(StateMachine *machine) {
  for (Modifier *key = machine->keys; key < machine->keys + N_MODIFIERS; ++key) {
    if (key->flag == LATCHED)
      return true;
  }
  return false;
}

void state_machine__flush(StateMachine *machine) {
  if (!(machine->tainted)) {
    return;
  }
  // puts("dispatching");
  machine->tainted = false;

  // consume buffer event, writing it to virtual keyboard
  if (machine->buffer_event) {

    ssize_t res = write_ignoring_interrupt(machine->virtual_fd, machine->buffer_event);
    if (res < 0) {
      perror("failed to passthrough event to virtual device");
    };
    machine->buffer_event = NULL;
  }

  bool capsl = false;
  for (int j = 0; j < N_MODIFIERS; ++j) {
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

void state_machine__interact_modifier(StateMachine *machine, InputEvent *event,
                                      int i) {
  Modifier *key = machine->keys + i;
  uint64_t current_release_ms = event_time_ms(event);
  uint64_t elapsed_ms = current_release_ms - key->last;

  ModifierState flag = LOCKED;

  if (elapsed_ms > machine->timeout)
    flag = NEXT_STATE[key->flag];

  if (key->flag != flag) {
    machine->tainted = true;
    key->flag = flag;
  }

  key->last = current_release_ms;
}

void state_machine__interact(StateMachine *machine, InputEvent *event) {
  int i = modifier_index(event->code);

  if (i >= 0) {
    bool masked = !(machine->mask & (1 << i));
    if (masked) {
      emit_event(machine->virtual_fd, EV_KEY, event->code, event->value);
      (machine->keys + i)->flag = event->value;
      emit_event(machine->virtual_fd, EV_SYN, SYN_REPORT, 0);
      return;
    }
  }

  if (i < 0) {
    state_machine__free_latched(machine);
    machine->buffer_event = event;
    machine->tainted = true;
    return;
  }

  // 0 release
  // 1 press
  // 2 hardware spam
  if (event->value == 1)
    state_machine__interact_modifier(machine, event, i);
}
