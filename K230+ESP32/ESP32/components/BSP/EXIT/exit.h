#ifndef __EXIT_H_
#define __EXIT_H_

#include "led.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "exit_press.h"

#define BOOT_GPIO_PIN GPIO_NUM_0
void exit_init(void);
void IRAM_ATTR exit_isr_handler(void* arg);

#endif
