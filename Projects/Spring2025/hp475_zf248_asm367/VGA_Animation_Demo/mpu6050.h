/**
 * Hunter Adams (vha3@cornell.edu)
 * 
 *
 */
// mpu6050.h — at the top
#undef I2C_CHAN
#undef SDA_PIN
#undef SCL_PIN

#define ADDRESS 0x68
#define I2C_CHAN i2c1
#define SDA_PIN  2
#define SCL_PIN  3
#define I2C_BAUD_RATE 400000

// Fixed point data type
typedef signed int fix15 ;
#define multfix151(a,b) ((fix15)(((( signed long long)(a))*(( signed long long)(b)))>>16)) 
#define float2fix151(a) ((fix15)((a)*65536.0f)) // 2^16
#define fix2float151(a) ((float)(a)/65536.0f) 
#define int2fix151(a) ((a)<<16)
#define fix2int151(a) ((a)>>16)
#define divfix1(a,b) ((fix15)(((( signed long long)(a) << 16 / (b)))))
// Parameter values
#define oneeightyoverpi 3754936
#define zeropt001 65
#define zeropt999 65470
#define zeropt01 655
#define zeropt99 64880
#define zeropt1 6553
#define zeropt9 58982

// VGA primitives - usable in main
void mpu6050_reset(uint8_t addr) ;
void mpu6050_read_raw(uint8_t addr, fix15 accel[3], fix15 gyro[3]);