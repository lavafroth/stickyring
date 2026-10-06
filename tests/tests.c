#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>
#include <unistd.h>

#define STATE_MACHINE_IMPLEMENTATION
#include "../src/state_machine.h"

void test_free_all_latched() {
  puts("entering test_free_all_latched");
  int devnull = open("/dev/null", O_WRONLY);
  modifier keys[N_MODFIERS];
  memset(keys, 0, sizeof(keys));
  keys[0].flag = LATCHED;
  keys[2].flag = LOCKED;
  keys[7].flag = FREE;

  free_all_latched(devnull, keys);

  assert(keys[0].flag == FREE);
  assert(keys[2].flag == LOCKED);
  assert(keys[7].flag == FREE);
  close(devnull);
  puts("test passed");
}

int main() {
  test_free_all_latched();
}
