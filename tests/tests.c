#include <assert.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../src/state_machine.h"

void test_free_all_latched() {
  Modifier keys[N_MODIFIERS];
  memset(keys, 0, sizeof(keys));
  StateMachine machine = {.keys = keys,
                          .capsl = false,
                          .device_fd = 0,
                          .virtual_fd = 0, // dummy file descriptors, events never dispatched
                          .active_timer_id = 0,
                          .mask = 0b11111111,
                          .tainted = false,
                          .buffer_event = NULL};
  machine.keys[0].flag = LATCHED;
  machine.keys[2].flag = LOCKED;
  machine.keys[7].flag = FREE;

  state_machine__free_latched(&machine);

  assert(machine.keys[0].flag == FREE);
  assert(machine.keys[2].flag == LOCKED);
  assert(machine.keys[7].flag == FREE);
}


void test_free_all_latched_masked() {
  Modifier keys[N_MODIFIERS];
  memset(keys, 0, sizeof(keys));
  StateMachine machine = {
                          .keys = keys,
                          .capsl = false,
                          .device_fd = 0,
                          .virtual_fd = 0,
                          .active_timer_id = 0,
                          .mask = 0b11111101,
                          .tainted = false,
                          .buffer_event = NULL,
                          .timeout = 300,
                        };
  machine.keys[0].flag = LATCHED;
  machine.keys[1].flag = LATCHED;
  machine.keys[2].flag = LOCKED;
  machine.keys[7].flag = FREE;

  state_machine__free_latched(&machine);

  assert(machine.keys[0].flag == FREE);
  assert(machine.keys[1].flag == LATCHED);
  assert(machine.keys[2].flag == LOCKED);
  assert(machine.keys[7].flag == FREE);
}


void press(InputEvent *ev, uint16_t code, int32_t value, time_t sec) {
  ev->value = value;
  ev->code = code;
  ev->time.tv_sec = sec;
}


// test if repeated latches even with interspersed
// free's do not accidentally lock a key.
// Example: `control backspace` in two quick successions
// should NOT lock the key.
void test_latch_chain_no_lock() {
  Modifier keys[N_MODIFIERS];
  memset(keys, 0, sizeof(keys));
  StateMachine machine = {.keys = keys,
                          .capsl = false,
                          .device_fd = 0,
                          .virtual_fd = 0,
                          .active_timer_id = 0,
                          .mask = 0b11111101,
                          .tainted = false,
                          .buffer_event = NULL,
                          .timeout = 3000 // 3s
                      };

  InputEvent ev = { .time.tv_sec = 0, .type = EV_KEY };
  press(&ev, KEY_LEFTCTRL, 1, 50);
  state_machine__interact(&machine, &ev);
  press(&ev, KEY_LEFTCTRL, 0, 51);
  state_machine__interact(&machine, &ev);
  press(&ev, KEY_BACKSPACE, 1, 52);
  state_machine__interact(&machine, &ev);
  press(&ev, KEY_BACKSPACE, 0, 53);
  state_machine__interact(&machine, &ev);
  press(&ev, KEY_LEFTCTRL, 1, 54);
  state_machine__interact(&machine, &ev);
  press(&ev, KEY_LEFTCTRL, 0, 55);
  state_machine__interact(&machine, &ev);
  press(&ev, KEY_BACKSPACE, 1, 56);
  state_machine__interact(&machine, &ev);
  press(&ev, KEY_BACKSPACE, 0, 57);
  state_machine__interact(&machine, &ev);

  assert(keys[2].flag == FREE);
}

int main() {
  test_free_all_latched();
  test_free_all_latched_masked();
  test_latch_chain_no_lock();
  puts("all tests passed");
  return 0;
}
