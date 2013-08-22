//
//  raspi_hw.c
//  ToneGenerator
//
//  Created by Masahiko Hasebe on 2013/08/13.
//  Copyright (c) 2013 Masahiko Hasebe. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

//-------------------------------------------------------------------------
//			Variables
//-------------------------------------------------------------------------
static int i2cDscript;       // file discripter



//-------------------------------------------------------------------------
//			Constants
//-------------------------------------------------------------------------
static unsigned char GPIO_EXPANDER_ADDRESS = 0x3e;
static unsigned char PRESSURE_SENSOR_ADDRESS = 0x5d;



//-------------------------------------------------------------------------
//			I2c Device Access Functions
//-------------------------------------------------------------------------
void initI2c( void )
{
    const char	*fileName = "/dev/i2c-1"; // I2C Drive File name
	
	//	Pressure Sensor
    printf("***** start i2c *****\n");
	
    // Open I2C port with Read/Write Attribute
    if ((i2cDscript = open(fileName, O_RDWR)) < 0){
        printf("Faild to open i2c port\n");
        exit(1);
    }
}
//-------------------------------------------------------------------------
void quitI2c( void )
{
     printf("***** quit i2c *****\n");
	
    // Open I2C port with Read/Write Attribute
    if ( close(i2cDscript) < 0){
        printf("Faild to close i2c port\n");
        exit(1);
    }
}
//-------------------------------------------------------------------------
void writeI2c( unsigned char adrs, unsigned char data )
{
	unsigned char buf[2];
	
	buf[0] = adrs;									// Commands for performing a ranging
	buf[1] = data;
	
	if ((write(i2cDscript, buf, 2)) != 2) {			// Write commands to the i2c port
		printf("Error writing to i2c slave\n");
		exit(1);
	}
}
//-------------------------------------------------------------------------
unsigned char readI2c( unsigned char adrs )
{
	unsigned char buf[2];
	buf[0] = adrs;									// This is the register we wish to read from
	
	if (write(i2cDscript, buf, 1) != 1) {			// Send the register to read from
		printf("Error writing to i2c slave(read)\n");
		exit(1);
	}
	
	if (read(i2cDscript, buf, 1) != 1) {					// Read back data into buf[]
		printf("Unable to read from slave\n");
		exit(1);
	}
	
	return buf[0];
}

//-------------------------------------------------------------------------
//			SX1509 (GPIO Expansion Device)
//-------------------------------------------------------------------------
//	for GPIO Expansion
#define		GPIO_EXPNDR_PULL_UP_B		0x06
#define		GPIO_EXPNDR_PULL_UP_A		0x07
#define		GPIO_EXPNDR_DIR_B			0x0e
#define		GPIO_EXPNDR_DIR_A			0x0f
#define		GPIO_EXPNDR_DATA_B			0x10
#define		GPIO_EXPNDR_DATA_A			0x11
//-------------------------------------------------------------------------
void accessSX1509( void )
{
	int		address = GPIO_EXPANDER_ADDRESS;  // I2C

	// Set Address
	if (ioctl(i2cDscript, I2C_SLAVE, address) < 0){
		printf("Unable to get bus access to talk to slave\n");
		exit(1);
	}
}
//-------------------------------------------------------------------------
void initSX1509( void )
{
	//	Start Access
	accessSX1509();
	
	//	Init Parameter
	writeI2c( GPIO_EXPNDR_PULL_UP_B, 0xFF );
	writeI2c( GPIO_EXPNDR_PULL_UP_A, 0xFF );
	writeI2c( GPIO_EXPNDR_DIR_B, 0xFF );
	writeI2c( GPIO_EXPNDR_DIR_A, 0xFF );
}
//-------------------------------------------------------------------------
unsigned short getSwData( void )
{
	unsigned short dt;
			
	//	Start Access
	accessLPS331AP();
	
	//	GPIO
	dt = readI2c( GPIO_EXPNDR_DATA_B ) << 8;
	dt |= readI2c( GPIO_EXPNDR_DATA_A );

	return dt;
}

//-------------------------------------------------------------------------
//			LPS331AP (Pressure Sencer : I2c Device)
//-------------------------------------------------------------------------
//	for Pressure Sencer
#define		PRES_SNCR_RESOLUTION		0x10
#define		PRES_SNCR_PWRON				0x20
#define		PRES_SNCR_START				0x21
#define		PRES_SNCR_ONE_SHOT			0x01
#define		PRES_SNCR_RCV_DT_FLG		0x27
#define		PRES_SNCR_RCV_TMPR			0x01
#define		PRES_SNCR_RCV_PRES			0x02
#define		PRES_SNCR_DT_H				0x28
#define		PRES_SNCR_DT_M				0x29
#define		PRES_SNCR_DT_L				0x2a
//-------------------------------------------------------------------------
void accessLPS331AP( void )
{
	int		address = PRESSURE_SENSOR_ADDRESS;  // I2C
	
	// Set Address
	if (ioctl(i2cDscript, I2C_SLAVE, address) < 0){
		printf("Unable to get bus access to talk to slave\n");
		exit(1);
	}
}
//-------------------------------------------------------------------------
void initLPS331AP( void )
{
	//	Start Access
	accessLPS331AP();
	
	//	Init Parameter
	writeI2c( PRES_SNCR_PWRON, 0x80 );	//	Power On
	writeI2c( PRES_SNCR_RESOLUTION, 0x7A );	//	Resolution
}
//-------------------------------------------------------------------------
int getPressure( void )
{
	unsigned char rdDt, dt[3];
	float	fdata = 10000;	//	Default Value
	int		idt;
	
	//	Start Access
	accessLPS331AP();
	
	//	Pressure Sencer
	writeI2c( PRES_SNCR_START, PRES_SNCR_ONE_SHOT );	//	Start One shot
	rdDt = readI2c( PRES_SNCR_RCV_DT_FLG );
	if ( rdDt & PRES_SNCR_RCV_PRES ){
		dt[0] = readI2c( PRES_SNCR_DT_H );
		dt[1] = readI2c( PRES_SNCR_DT_M );
		dt[2] = readI2c( PRES_SNCR_DT_L );
		fdata = (dt[2]<<16)|(dt[1]<<8)|dt[0];
		fdata = fdata*10/4096;
	}
	
	return (int)roundf(fdata);	//	10 times of Pressure(hPa)
	
	//	Temperature
#if 0
	if ( rdDt & PRES_SNCR_RCV_TMPR ){
		dt[0] = readI2c( 0x2b );
		dt[1] = readI2c( 0x2c );
		data = 0x10000 - (dt[1]<<8)|dt[0];
		data = (42.5 - data/480)*10;
	}
#endif
}
