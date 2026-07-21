#include "drivers/copet_i2c.h"

#include "esp_check.h"

enum {
    I2C_PIN_SDA = 21,
    I2C_PIN_SCL = 22,
};

static const char *TAG = "copet_i2c";

static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t copet_i2c_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_PIN_SDA,
        .scl_io_num = I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_config, &s_i2c_bus);
}

i2c_master_bus_handle_t copet_i2c_bus(void)
{
    return s_i2c_bus;
}
