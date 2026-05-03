#include <stdint.h>

#include "FreeRTOS.h"

/*
 * FreeRTOS 动态堆放入 CCMRAM，避免继续挤占 DMA 可访问的普通 SRAM。
 * 注意：CCMRAM 不能被 STM32F4 DMA 访问，禁止通过 pvPortMalloc/new 分配
 * UART/SPI/ADC/I2C/USB 等 DMA 直接读写的缓冲区。
 */
uint8_t ucHeap[configTOTAL_HEAP_SIZE]
    __attribute__((section(".freertos_heap"), aligned(portBYTE_ALIGNMENT)));
