#ifndef BUTTON_MODE_H_
#define BUTTON_MODE_H_

typedef enum {
    MODE_MEDIA = 0,      /* Mode 1: Volume & Media Playback */
    MODE_NAVIGATION = 1, /* Mode 2: Track Scrubbing */
    MODE_SYSTEM = 2,     /* Mode 3: Display Brightness Control */
    MODE_COUNT
} controller_mode_t;

int button_mode_init(void);
void button_mode_process(void);
controller_mode_t button_mode_get_current(void);

#endif /* BUTTON_MODE_H_ */