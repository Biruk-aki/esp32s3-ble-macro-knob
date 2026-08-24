#ifndef STATUS_LED_H_
#define STATUS_LED_H_

#include "button_mode.h"

int status_led_init(void);
void status_led_set_mode(controller_mode_t mode);

#endif /* STATUS_LED_H_ */