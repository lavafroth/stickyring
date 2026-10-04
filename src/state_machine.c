const int MODIFIERS[] = {KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL,
                         KEY_RIGHTCTRL, KEY_LEFTMETA,   KEY_RIGHTMETA,
                         KEY_LEFTALT,   KEY_RIGHTALT};
const int N_MODFIERS = sizeof(MODIFIERS) / sizeof(MODIFIERS[0]);

const int NEXT_STATE[3] = {LATCHED, FREE, FREE};

int modifier_index(uint16_t code) {
  for (int i = 0; i < N_MODFIERS; ++i)
    if (MODIFIERS[i] == code)
      return i;
  return -1;
}

void free_all_latched(modifier *keys) {
    for (int j = 0; j < N_MODFIERS; ++j) {
      if (keys[j].flag == LATCHED) {
        keys[j].flag = FREE;
        emit(EV_KEY, MODIFIERS[j], 0);
      }
    }
    emit(EV_SYN, SYN_REPORT, 0);
}
