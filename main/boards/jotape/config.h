#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

#define AUDIO_INPUT_SAMPLE_RATE 24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_8
#define AUDIO_I2S_GPIO_WS GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_9
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_11
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_12

#define AUDIO_CODEC_PA_PIN GPIO_NUM_7
#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_42
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_41
#define AUDIO_CODEC_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR ES7210_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO GPIO_NUM_NC
#define PWR_BUTTON_GPIO GPIO_NUM_5
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_4
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_0

#define DISPLAY_SPI_MODE 3
#define DISPLAY_CS_PIN GPIO_NUM_21
#define DISPLAY_MOSI_PIN GPIO_NUM_39
#define DISPLAY_MISO_PIN GPIO_NUM_NC
#define DISPLAY_CLK_PIN GPIO_NUM_38
#define DISPLAY_DC_PIN GPIO_NUM_45
#define DISPLAY_RST_PIN GPIO_NUM_40

#define TOOUCH_INT_PIN GPIO_NUM_48
#define TOOUCH_RST_PIN GPIO_NUM_47

#define BATTERY_EN_PIN GPIO_NUM_2
#define BATTERY_ADC_PIN GPIO_NUM_1
#define BATTERY_CHARGING_PIN GPIO_NUM_3

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_INVERT_COLOR true

#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_46
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

/* ---- JOTAPE screen orientation ----
 * Rotation used at power-on and while the board lies flat (buttons on the
 * side = face upright). Values are lv_display_rotation_t (0..3 = 0/90/180/270).
 */
#define JOTAPE_DEFAULT_ROTATION 1

/* Rotation to apply when gravity points along each accelerometer axis.
 * Adjust if the face ends up sideways/upside down for some orientation. */
#define JOTAPE_ROTATION_GRAVITY_POS_X 1
#define JOTAPE_ROTATION_GRAVITY_NEG_X 3
#define JOTAPE_ROTATION_GRAVITY_POS_Y 0
#define JOTAPE_ROTATION_GRAVITY_NEG_Y 2

/* The ST7789 has 240x320 RAM for this 240x240 glass: when the panel mirrors
 * its rows the visible area shifts by 80 pixels. Gap (x, y) per rotation. */
#define JOTAPE_GAP_ROTATION_0 0, 0
#define JOTAPE_GAP_ROTATION_90 80, 0
#define JOTAPE_GAP_ROTATION_180 0, 80
#define JOTAPE_GAP_ROTATION_270 0, 0

#endif // _BOARD_CONFIG_H_
