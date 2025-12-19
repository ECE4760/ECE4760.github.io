#include "utils.h"

#define I2C_RECOVERY_CLOCKS 9

static int i2c_reinit_attempts = 0; // Counter for reinitialization attempts
const int max_i2c_reinit_attempts = 4;

// Wrapper function to manage recovery attempts and reinitialization
void handleI2CError(i2c_inst_t* &i2c_port) {
    if (++i2c_reinit_attempts >= max_i2c_reinit_attempts ) {
        initI2C(i2c_port, true); // Force recovery on the nth attempt
        i2c_reinit_attempts = 0; // Reset attempts after recovery
    } else {
        initI2C(i2c_port, false); // Regular reinitialization without forcing recovery
    }
}

void i2cBusRecovery(uint sda_pin, uint scl_pin) {
    gpio_init(sda_pin);
    gpio_init(scl_pin);

    gpio_set_dir(sda_pin, GPIO_IN);
    gpio_set_dir(scl_pin, GPIO_OUT);

    // Attempt to clock out any stuck slave devices
    for (int i = 0; i < I2C_RECOVERY_CLOCKS; ++i) {
        gpio_put(scl_pin, 0);
        busy_wait_us(5); // Clock low period
        gpio_put(scl_pin, 1);
        busy_wait_us(5); // Clock high period

        // Check if SDA line has been released by slave
        if (gpio_get(sda_pin)) {
            break; // SDA line is high; assume bus is free
        }
    }

    // Generate a stop condition in case a slave is still in the middle of a transaction
    gpio_set_dir(sda_pin, GPIO_OUT);
    gpio_put(scl_pin, 0);
    busy_wait_us(5);
    gpio_put(sda_pin, 0);
    busy_wait_us(5);
    gpio_put(scl_pin, 1);
    busy_wait_us(5);
    gpio_put(sda_pin, 1);
    busy_wait_us(5);
}

void initI2C(i2c_inst_t* &i2c_port, bool force_recovery) {
    i2c_port = i2c_default; // i2c0

    // Attempt I2C bus recovery before de-initializing pins
    if (force_recovery) {
        i2cBusRecovery(CONFIG::I2C_SDA_PIN, CONFIG::I2C_SCL_PIN);
    }
    // de-initialise I2C pins in case
    gpio_disable_pulls(CONFIG::I2C_SDA_PIN);
    gpio_set_function(CONFIG::I2C_SDA_PIN, GPIO_FUNC_NULL);
    gpio_disable_pulls(CONFIG::I2C_SCL_PIN);
    gpio_set_function(CONFIG::I2C_SCL_PIN, GPIO_FUNC_NULL);

    // initialise I2C with baudrate
    i2c_init(i2c_port, CONFIG::I2C_BAUD_RATE);

    gpio_set_function(CONFIG::I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(CONFIG::I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(CONFIG::I2C_SDA_PIN);
    gpio_pull_up(CONFIG::I2C_SCL_PIN);
}

bool reserved_addr(uint8_t addr) {
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

void scan_i2c_bus() {
    printf("\nI2C Bus Scan\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int addr = 0; addr < (1 << 7); ++addr) {
        if (addr % 16 == 0) {
            printf("%02x ", addr);
        }

        // Perform a 1-byte dummy read from the probe address. If a slave
        // acknowledges this address, the function returns the number of bytes
        // transferred. If the address byte is ignored, the function returns
        // -1.

        // Skip over any reserved addresses.
        int ret;
        uint8_t rxdata;
        if (reserved_addr(addr))
            ret = PICO_ERROR_GENERIC;
        else
            ret = i2c_read_blocking(i2c_default, addr, &rxdata, 1, false);

        printf(ret < 0 ? "." : "@");
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
}

// Non-blocking delay function
bool non_blocking_delay_us(uint32_t delay_us) {
    static absolute_time_t start_time;
    static bool delay_started = false;

    if (!delay_started) {
        start_time = get_absolute_time();
        delay_started = true;
    }

    if (absolute_time_diff_us(start_time, get_absolute_time()) >= delay_us) {
        delay_started = false; // Reset for the next delay call
        return true; // Delay completed
    }

    return false; // Delay still ongoing
}
