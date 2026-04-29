#include "app_main.h"

#include "cdc_uart.hpp"
#include "libxr.hpp"
#include "main.h"
#include "stm32_adc.hpp"
#include "stm32_can.hpp"
#include "stm32_canfd.hpp"
#include "stm32_dac.hpp"
#include "stm32_flash.hpp"
#include "stm32_gpio.hpp"
#include "stm32_i2c.hpp"
#include "stm32_power.hpp"
#include "stm32_pwm.hpp"
#include "stm32_spi.hpp"
#include "stm32_timebase.hpp"
#include "stm32_uart.hpp"
#include "stm32_usb_dev.hpp"
#include "stm32_watchdog.hpp"
#include "usb_otg.h"
#include "flash_map.hpp"
#include "app_framework.hpp"
#include "xrobot_main.hpp"

using namespace LibXR;

/* User Code Begin 1 */
namespace {

constexpr auto kUsbLanguagePack =
    LibXR::USB::DescriptorStrings::MakeLanguagePack(
        LibXR::USB::DescriptorStrings::Language::EN_US, "XRobot",
        "XRobot MasterMachine VCP", "XROBOT-MM-0001");

alignas(4) std::uint8_t usb_ep0_out_buffer[64];
alignas(4) std::uint8_t usb_cdc_out_buffer[64];
alignas(4) std::uint8_t usb_ep0_in_buffer[64];
alignas(4) std::uint8_t usb_cdc_in_buffer[64];
alignas(4) std::uint8_t usb_cdc_comm_in_buffer[16];

}  // namespace
/* User Code End 1 */
// NOLINTBEGIN
// clang-format off
/* External HAL Declarations */
extern ADC_HandleTypeDef hadc1;
extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;
extern DAC_HandleTypeDef hdac;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim10;
extern TIM_HandleTypeDef htim14;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;
extern TIM_HandleTypeDef htim8;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart6;

/* DMA Resources */
static uint16_t adc1_buf[32];
static uint8_t spi1_tx_buf[32];
static uint8_t spi1_rx_buf[32];
static uint8_t usart1_tx_buf[128];
static uint8_t usart1_rx_buf[128];
static uint8_t usart2_rx_buf[128];
static uint8_t usart3_rx_buf[128];
static uint8_t usart6_tx_buf[128];
static uint8_t usart6_rx_buf[128];
static uint8_t i2c2_buf[32];
static uint8_t i2c3_buf[32];

extern "C" void app_main(void) {
  // clang-format on
  // NOLINTEND
  /* User Code Begin 2 */
  
  /* User Code End 2 */
  // clang-format off
  // NOLINTBEGIN
  STM32TimerTimebase timebase(&htim14);
  PlatformInit(2, 1024);
  STM32PowerManager power_manager;

  /* GPIO Configuration */
  STM32GPIO KEY(KEY_GPIO_Port, KEY_Pin);
  STM32GPIO CS1_ACCEL(CS1_ACCEL_GPIO_Port, CS1_ACCEL_Pin);
  STM32GPIO CS1_GYRO(CS1_GYRO_GPIO_Port, CS1_GYRO_Pin);
  STM32GPIO INT_ACC(INT_ACC_GPIO_Port, INT_ACC_Pin, EXTI4_IRQn);
  STM32GPIO INT_GYRO(INT_GYRO_GPIO_Port, INT_GYRO_Pin, EXTI9_5_IRQn);
  STM32GPIO INT_MAG(INT_MAG_GPIO_Port, INT_MAG_Pin, EXTI3_IRQn);
  STM32GPIO MAG_RST(MAG_RST_GPIO_Port, MAG_RST_Pin);

  STM32ADC adc1(&hadc1, adc1_buf, {ADC_CHANNEL_TEMPSENSOR, ADC_CHANNEL_VREFINT}, 3.3);
  auto adc1_adc_channel_tempsensor = adc1.GetChannel(0);
  UNUSED(adc1_adc_channel_tempsensor);
  auto adc1_adc_channel_vrefint = adc1.GetChannel(1);
  UNUSED(adc1_adc_channel_vrefint);

  STM32PWM pwm_tim1_ch1(&htim1, TIM_CHANNEL_1, false);
  STM32PWM pwm_tim1_ch2(&htim1, TIM_CHANNEL_2, false);
  STM32PWM pwm_tim1_ch3(&htim1, TIM_CHANNEL_3, false);
  STM32PWM pwm_tim1_ch4(&htim1, TIM_CHANNEL_4, false);

  STM32PWM pwm_tim10_ch1(&htim10, TIM_CHANNEL_1, false);

  STM32PWM pwm_tim4_ch3(&htim4, TIM_CHANNEL_3, false);

  STM32PWM pwm_tim5_ch1(&htim5, TIM_CHANNEL_1, false);
  STM32PWM pwm_tim5_ch2(&htim5, TIM_CHANNEL_2, false);
  STM32PWM pwm_tim5_ch3(&htim5, TIM_CHANNEL_3, false);

  STM32DAC dac_out2(&hdac, DAC_CHANNEL_2, 0.0, 3.3);

  STM32SPI spi1(&hspi1, spi1_rx_buf, spi1_tx_buf, 3);

  STM32SPI spi2(&hspi2, {nullptr, 0}, {nullptr, 0}, 3);

  STM32UART usart1(&huart1,
              usart1_rx_buf, usart1_tx_buf, 5);

  STM32UART usart2(&huart2,
              usart2_rx_buf, {nullptr, 0}, 5);

  STM32UART usart3(&huart3,
              usart3_rx_buf, {nullptr, 0}, 5);

  STM32UART usart6(&huart6,
              usart6_rx_buf, usart6_tx_buf, 5);

  STM32I2C i2c2(&hi2c2, i2c2_buf, 3);

  STM32I2C i2c3(&hi2c3, i2c3_buf, 3);

  STM32CAN can1(&hcan1, 5);

  STM32CAN can2(&hcan2, 5);

  LibXR::USB::CDCUart vcp_master_machine(256, 256, 5);
  STM32USBDeviceOtgFS usb_otg_fs(
      &hpcd_USB_OTG_FS, 128,
      {
          LibXR::RawData{usb_ep0_out_buffer, sizeof(usb_ep0_out_buffer)},
          LibXR::RawData{usb_cdc_out_buffer, sizeof(usb_cdc_out_buffer)},
      },
      {
          STM32USBDeviceOtgFS::EPInConfig{
              LibXR::RawData{usb_ep0_in_buffer, sizeof(usb_ep0_in_buffer)}, 64},
          STM32USBDeviceOtgFS::EPInConfig{
              LibXR::RawData{usb_cdc_in_buffer, sizeof(usb_cdc_in_buffer)}, 64},
          STM32USBDeviceOtgFS::EPInConfig{
              LibXR::RawData{usb_cdc_comm_in_buffer,
                             sizeof(usb_cdc_comm_in_buffer)},
              16},
      },
      LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, 0x0483, 0x5740,
      0x0200, {&kUsbLanguagePack}, {{&vcp_master_machine}});
  usb_otg_fs.Init(false);
  usb_otg_fs.Start(false);

  /* Terminal Configuration */


  LibXR::HardwareContainer peripherals{
    LibXR::Entry<LibXR::PowerManager>({power_manager, {"power_manager"}}),
    LibXR::Entry<LibXR::GPIO>({KEY, {"KEY"}}),
    LibXR::Entry<LibXR::GPIO>({CS1_ACCEL, {"CS1_ACCEL"}}),
    LibXR::Entry<LibXR::GPIO>({CS1_GYRO, {"CS1_GYRO"}}),
    LibXR::Entry<LibXR::GPIO>({INT_ACC, {"INT_ACC"}}),
    LibXR::Entry<LibXR::GPIO>({INT_GYRO, {"INT_GYRO"}}),
    LibXR::Entry<LibXR::GPIO>({INT_MAG, {"INT_MAG"}}),
    LibXR::Entry<LibXR::GPIO>({MAG_RST, {"MAG_RST"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim1_ch1, {"pwm_tim1_ch1"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim1_ch2, {"pwm_tim1_ch2"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim1_ch3, {"pwm_tim1_ch3"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim1_ch4, {"pwm_tim1_ch4"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim10_ch1, {"pwm_tim10_ch1"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim4_ch3, {"pwm_tim4_ch3"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim5_ch1, {"pwm_tim5_ch1"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim5_ch2, {"pwm_tim5_ch2"}}),
    LibXR::Entry<LibXR::PWM>({pwm_tim5_ch3, {"pwm_tim5_ch3"}}),
    LibXR::Entry<LibXR::ADC>({adc1_adc_channel_tempsensor, {"adc1_adc_channel_tempsensor"}}),
    LibXR::Entry<LibXR::ADC>({adc1_adc_channel_vrefint, {"adc1_adc_channel_vrefint"}}),
    LibXR::Entry<LibXR::DAC>({dac_out2, {"dac_out2"}}),
    LibXR::Entry<LibXR::SPI>({spi1, {"spi1"}}),
    LibXR::Entry<LibXR::SPI>({spi2, {"spi2"}}),
    LibXR::Entry<LibXR::UART>({usart1, {"usart1"}}),
    LibXR::Entry<LibXR::UART>({usart2, {"usart2"}}),
    LibXR::Entry<LibXR::UART>({usart3, {"usart3"}}),
    LibXR::Entry<LibXR::UART>({usart6, {"usart6"}}),
    LibXR::Entry<LibXR::I2C>({i2c2, {"i2c2"}}),
    LibXR::Entry<LibXR::I2C>({i2c3, {"i2c3"}}),
    LibXR::Entry<LibXR::CAN>({can1, {"can1"}}),
    LibXR::Entry<LibXR::CAN>({can2, {"can2"}})
  };

  // clang-format on
  // NOLINTEND
  /* User Code Begin 3 */
  STM32Flash flash(FLASH_SECTORS, FLASH_SECTOR_NUMBER);
  DatabaseRawSequential database(flash);
  RamFS ramfs;

  peripherals.Register(Entry<GPIO>{CS1_ACCEL, {"bmi088_accl_cs"}});
  peripherals.Register(Entry<GPIO>{CS1_GYRO, {"bmi088_gyro_cs"}});
  peripherals.Register(Entry<GPIO>{INT_GYRO, {"bmi088_gyro_int"}});
  peripherals.Register(Entry<PWM>{pwm_tim10_ch1, {"pwm_bmi088_heat"}});
  peripherals.Register(Entry<SPI>{spi1, {"spi_bmi088", "SPI1"}});
  peripherals.Register(Entry<UART>{usart1, {"uart_master_machine", "USART1"}});
  peripherals.Register(Entry<UART>{vcp_master_machine, {"vcp_master_machine"}});
  peripherals.Register(Entry<UART>{usart2, {"uart_vt13", "USART2"}});
  peripherals.Register(Entry<UART>{usart3, {"USART3"}});
  peripherals.Register(Entry<UART>{usart6, {"uart_referee", "USART6"}});
  peripherals.Register(Entry<CAN>{can1, {"CAN1"}});
  peripherals.Register(Entry<CAN>{can2, {"can_bridge", "CAN2"}});
  peripherals.Register(Entry<Flash>{flash, {"flash"}});
  peripherals.Register(Entry<Database>{database, {"database"}});
  peripherals.Register(Entry<RamFS>{ramfs, {"ramfs"}});

  XRobotMain(peripherals);
  /* User Code End 3 */
}
