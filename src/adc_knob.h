#ifndef ADC_KNOB_H_
#define ADC_KNOB_H_

#include <stdint.h>

#define KNOB_EVENT_NONE     0
#define KNOB_EVENT_CW       1   /* Clockwise / Step Up */
#define KNOB_EVENT_CCW      2   /* Counter-Clockwise / Step Down */

int adc_knob_init(void);
int adc_knob_process(void);

#endif /* ADC_KNOB_H_ */