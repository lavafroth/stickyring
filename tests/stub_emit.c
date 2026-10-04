int emit(uint16_t type, uint16_t code, int32_t value) {
  if (type == EV_SYN) {
    puts("synchronize event");
    return 0;
  }

  char *key_state = "pressed";
  if (value) {
    key_state = "released";
  }

  printf("setting key code %03x to %s\n", code, key_state);
  return 0;
}
