void solar_toggle_name(const char *name);
void solar_toggle_id(unsigned int id, int relay);
void solar_tasmota(tasmota_t *t);
void solar_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize);
