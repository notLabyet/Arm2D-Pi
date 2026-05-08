#include "bm8563_task.h"


extern char iic0_write_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len);
extern char iic0_read_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len);

bm8563_t tbm8563;


int32_t bm8563_write_bytes(void *handle, uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t size){

	int32_t status;
	if(iic0_write_bytes(address,reg, buffer,size)){
		status = BM8563_OK;
	}else {
		status = BM8563_ERROR_NOTTY;
	}
	return status;
}

int32_t bm8563_read_bytes(void *handle, uint8_t address, uint8_t reg,uint8_t *buffer, uint16_t size){
	int32_t status;
	if(iic0_read_bytes(address,reg, buffer,size)){
		status = BM8563_OK;
	}else {
		status = BM8563_ERROR_NOTTY;
	}
	return status;
}

char bm8563_hander_init()
{
	struct tm time ;
	time.tm_year = 26;
	time.tm_mon = 3;
	time.tm_mday = 12;
	time.tm_wday = 4-1;
	time.tm_hour = 9;
	time.tm_min = 34;
	time.tm_sec = 00;
	
	tbm8563.read = bm8563_read_bytes;
	tbm8563.write = bm8563_write_bytes;
	bm8563_init(&tbm8563);
	bm8563_write(&tbm8563, &time);
	return 0;
}