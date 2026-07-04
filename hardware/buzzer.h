#ifndef BUZZER_H
#define BUZZER_H

#define BUZZER_PIN 17

#define BUZZER_SHORT_BEEP_DELAY 100
#define BUZZER_MEDIUM_BEEP_DELAY 500
#define BUZZER_LONG_BEEP_DELAY 2000

void buzzer_init();
void buzzer_on();
void buzzer_off();
void buzzer_beep_short();
void buzzer_beep_medium();
void buzzer_beep_long();

#endif