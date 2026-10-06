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

int event_emit(int fd, uint16_t type, uint16_t code, int32_t value) {
  struct input_event event = { { 0, 0 }, type, code, value };
  return write(fd, &event, sizeof(event));
}

void free_all_latched(int fd, int real_fd, modifier *keys) {
  bool capsl = false;
  for (int j = 0; j < N_MODFIERS; ++j) {
    if (keys[j].flag == LATCHED) {
      keys[j].flag = FREE;
      if (keys[j].flag) capsl = true;
      event_emit(fd, EV_KEY, MODIFIERS[j], 0);
    }
  }
  event_emit(real_fd, EV_LED, LED_CAPSL, capsl);
  event_emit(fd, EV_SYN, SYN_REPORT, 0);
}

bool led_state(modifier *keys) {
  for (int i = 0; i < N_MODFIERS; ++i)
    if (keys[i].flag) return true;
  return false;
}


#endif
