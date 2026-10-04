#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>
#include "stub_emit.c"
#include "../src/modifier.c"
#include "../src/state_machine.c"

void test_free_all_latched() {
  puts("entering test_free_all_latched");
  modifier keys[N_MODFIERS];
  memset(keys, 0, sizeof(keys));
  keys[0].flag = LATCHED;
  keys[2].flag = LOCKED;
  keys[7].flag = FREE;

  free_all_latched(keys);

  assert(keys[0].flag == FREE);
  assert(keys[2].flag == LOCKED);
  assert(keys[7].flag == FREE);
  puts("test passed");
}

int main() {
  test_free_all_latched();
}
