typedef enum {
  FREE = 0,
  LATCHED = 1,
  LOCKED = 2,
} modifier_state;

typedef struct {
  uint64_t last;
  modifier_state flag;
} modifier;

void display(modifier *keys, int i) {
  modifier key = keys[i];
  char *state_repr = "free";
  if (key.flag == LATCHED) state_repr = "latched";
  if (key.flag == LOCKED) state_repr = "locked";
  printf("%s for %d at %lu ms\n", state_repr, i, key.last);
}
