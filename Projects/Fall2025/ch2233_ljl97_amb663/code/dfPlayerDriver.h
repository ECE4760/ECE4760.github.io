#pragma once
#include "pico/stdlib.h"
#include "hardware/uart.h"

class DfPlayerDriver {
public:
    DfPlayerDriver(uart_inst_t* uart_port, uint tx_pin, uint rx_pin)
        : uart(uart_port), tx(tx_pin), rx(rx_pin)
    {
        uart_init(uart, 9600);
        gpio_set_function(tx, GPIO_FUNC_UART);
        gpio_set_function(rx, GPIO_FUNC_UART);
    }

    void play(uint16_t track) { sendCommand(0x03, track); }
    void next()               { sendCommand(0x01, 0); }
    void prev()               { sendCommand(0x02, 0); }
    void volume(uint8_t vol)  { if (vol > 30) vol = 30; sendCommand(0x06, vol); }
    void repeat(bool enable)  { sendCommand(0x11, enable ? 1 : 0); }
    void reset()              { sendCommand(0x0C, 0); }

    // NEW: play a file from a specific folder
    void playFolderTrack(uint8_t folder, uint8_t file);

private:
    uart_inst_t* uart;
    uint tx, rx;

    void sendCommand(uint8_t cmd, uint16_t param)
    {
        uint8_t packet[10] = {
            0x7E, 0xFF, 0x06, cmd, 0x00,
            uint8_t(param >> 8),
            uint8_t(param & 0xFF),
            0, 0, 0xEF
        };

        int16_t sum = 0;
        for (int i = 1; i < 7; i++) sum += packet[i];
        sum = -sum;
        packet[7] = (sum >> 8) & 0xFF;
        packet[8] = sum & 0xFF;

        uart_write_blocking(uart, packet, 10);
        sleep_ms(20);
    }
};


inline void DfPlayerDriver::playFolderTrack(uint8_t folder, uint8_t file)
{
    // DFPlayer limits: folders 1–99, files 1–255
    if (folder < 1)  folder = 1;
    if (folder > 99) folder = 99;
    if (file < 1)    file = 1;
    if (file > 255)  file = 255;

    // DFPlayer folder playback: param = (folder << 8) | file
    uint16_t param = (static_cast<uint16_t>(folder) << 8) | file;

    // 0x0F = "SPECIFY_FOLDER_PLAYBACK"
    sendCommand(0x0F, param);
}
