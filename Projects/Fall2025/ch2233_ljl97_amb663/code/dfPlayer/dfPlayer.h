#ifndef DFPLAYER_H
#define DFPLAYER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

constexpr uint8_t SERIAL_CMD_SIZE = 10;

namespace dfPlayer
{
    enum cmd
    {
        NEXT = 0x01,
        PREV = 0x02,
        SPECIFY_TRACKING = 0x03,
        INC_VOL = 0x04,
        DEC_VOL = 0x05,
        SPECIFY_VOL = 0x06,
        SPECIFY_EQ = 0x07,
        PECIFY_PLAYBACK_MODE = 0x08,
        SPECIFY_PLAYBACK_SRC = 0x09,
        ENTER_STANDBY = 0x0a,
        NORMAL_WORKING = 0x0b,
        RESET_MODULE = 0x0c,
        PLAYBACK = 0x0d,
        PAUSE = 0x0e,
        SPECIFY_FOLDER_PLAYBACK = 0x0f,
        VOL_ADJ_SET = 0x10,
        REPEAT_PLAY = 0x11
    };

    enum serialCommFormat
    {
        CMD = 0x03,
        PARA1 = 0x05,
        PARA2 = 0x06,
        CHKSUM_HIGH = 0x07,
        CHKSUM_LOW = 0x08
    };
}

template <typename T>
class DfPlayer
{
public:
    void next();
    void reset();
    void specifyVolume(uint8_t a_vol);
    void setRepeatPlay(bool a_repeat);

    int16_t calcChecksum(uint8_t *a_serialCmdBuffer);
    void sendCmd(uint8_t a_cmd, uint16_t a_params);
    inline void uartSend(uint8_t *a_cmd);

 

private:
    const uint8_t serialBufferWithHeadersNoAck[SERIAL_CMD_SIZE] = {0x7e, 0xff, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xef};
    uint8_t tempSerialBuffer[SERIAL_CMD_SIZE];
    uint8_t currentFolder = 1;
};

template <typename T>
void DfPlayer<T>::next()
{
    sendCmd(dfPlayer::cmd::NEXT, 0x0000);
}

template <typename T>
void DfPlayer<T>::reset()
{
    sendCmd(dfPlayer::cmd::RESET_MODULE, 0x0000);
}

template <typename T>
void DfPlayer<T>::specifyVolume(uint8_t a_vol)
{
    // Clip volume to maximum of 31 only!
    uint16_t tempParam = a_vol & 0x001f;
    sendCmd(dfPlayer::cmd::SPECIFY_VOL, tempParam);
}

template <typename T>
void DfPlayer<T>::setRepeatPlay(bool a_repeat)
{
    uint16_t tempParam = a_repeat;
    sendCmd(dfPlayer::cmd::REPEAT_PLAY, tempParam);
}

template <typename T>
int16_t DfPlayer<T>::calcChecksum(uint8_t *a_serialCmdBuffer)
{
    // Checksum is calculated from byte 1 to byte 6:
    int16_t result = 0;
    for (uint8_t i = 1; i < 7; i++)
    {
        result += a_serialCmdBuffer[i];
    }
    result = -result;
    return result;
}

template <typename T>
void DfPlayer<T>::sendCmd(uint8_t a_cmd, uint16_t a_params)
{
    memcpy(tempSerialBuffer, serialBufferWithHeadersNoAck, SERIAL_CMD_SIZE);

    tempSerialBuffer[dfPlayer::serialCommFormat::CMD] = a_cmd;
    tempSerialBuffer[dfPlayer::serialCommFormat::PARA1] = (a_params & 0xff00) >> 8;
    tempSerialBuffer[dfPlayer::serialCommFormat::PARA2] = (a_params & 0x00ff);

    int16_t checksum = calcChecksum(tempSerialBuffer);

    tempSerialBuffer[dfPlayer::serialCommFormat::CHKSUM_HIGH] = (checksum & 0xff00) >> 8;
    tempSerialBuffer[dfPlayer::serialCommFormat::CHKSUM_LOW] = (checksum & 0x00ff);

    uartSend(tempSerialBuffer);
}

template <typename T>
inline void DfPlayer<T>::uartSend(uint8_t *a_serialCmd)
{
    static_cast<T *>(this)->uartSend(a_serialCmd);
}


#endif