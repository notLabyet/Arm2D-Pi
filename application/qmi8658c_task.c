#include "qmi8658c_task.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "bsp_cfg.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"
#include <stdio.h>
//char iic0_read_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len)
//{
//	uint8_t ret = 0;
//	if( i2c_write_blocking(I2C_PORT,addr,&reg, 1,true) < 0){
//		ret = 0;
//	} else {
//		ret = 1;
//		if( i2c_read_blocking(I2C_PORT,addr,value, len,false) < 0){
//			ret = 0;
//		} else {
//			ret = 1;
//		}
//	}

//	return ret;
//}
//char iic0_write_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len)
//{
//	uint8_t ret = 0;
//	uint8_t buf[10];
//	buf[0] = reg;
//	buf[1] = *value;
//	memcpy(&buf[1],value,len);
//	if( i2c_write_blocking(I2C_PORT,addr,buf, len+1,false) < 0){
//		ret = 0;
//	} else {
//		ret = 1;
//	}
//	return ret;
//}
uint8_t qmi8658c_init()
{
	uint8_t ret = 0;
	uint32_t start_ms = to_ms_since_boot(get_absolute_time());
	i2c_init(I2C_PORT,400*1000);
	gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    bi_decl(bi_2pins_with_func(I2C_SDA,I2C_SCL,GPIO_FUNC_I2C));
	
	
	
	ret = QMI8658A_Init();;
	printf("QMI8658 init %s: %lu ms\r\n",
	       ret ? "OK" : "FAIL",
	       (unsigned long)(to_ms_since_boot(get_absolute_time()) - start_ms));
	return ret;
}


