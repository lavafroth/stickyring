#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <linux/input.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
  FREE = 0,
  LATCHED = 1,
  LOCKED = 2,
} ModifierState;

typedef struct {
  uint64_t last;
  ModifierState flag;
} Modifier;

typedef struct input_event InputEvent;

typedef struct {
  int x;
  int y;
} Position;

typedef enum {
  INITIATED,
  NOTED_BEGIN_X,
  NOTED_BEGIN_XY,
} TouchStage;

typedef struct {
  TouchStage stage;
  Position began;
  Position ended;
  uint64_t last_ms;
} Touch;

typedef struct {
  Modifier *keys;
  bool capsl;
  int device_fd;
  int virtual_fd;
  bool tainted;
  InputEvent *buffer_event;
  Touch touch;
  uint64_t timeout;
  uint64_t active_timer_id;
  uint8_t mask;

} StateMachine;

extern const int N_MODFIERS;

void display(Modifier *keys, int i);
uint64_t event_time_ms(const struct input_event *ev);

// Free all latched keys on the virtual device.
// Useful for clearing latched states when either a non modifier key is pressed
// or a tap, tap-drag, tap-drag-drag finishes and touchpad support is
// enabled.
void state_machine__free_latched(StateMachine *machine);
bool state_machine__any_latched(StateMachine *machine);
void state_machine__flush(StateMachine *machine);
void state_machine__interact_modifier(StateMachine *machine, InputEvent *event, int i);
void state_machine__interact(StateMachine *machine, InputEvent *event);
#endif
