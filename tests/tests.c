#include <assert.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define STATE_MACHINE_IMPLEMENTATION
#include "../src/state_machine.h"

void test_free_all_latched() {
  puts("entering test_free_all_latched");
  Modifier keys[N_MODFIERS];
  StateMachine machine = {.keys = keys,
                          .capsl = false,
                          0,
                          0, // dummy file descriptors, events never dispatched
                          .tainted = false,
                          .buffer_event = NULL};
  memset(keys, 0, sizeof(keys));
  keys[0].flag = LATCHED;
  keys[2].flag = LOCKED;
  keys[7].flag = FREE;

  state_machine__free_latched(&machine);

  assert(keys[0].flag == FREE);
  assert(keys[2].flag == LOCKED);
  assert(keys[7].flag == FREE);
  puts("test passed");
}

int main() {
  test_free_all_latched();
  return 0;
}
