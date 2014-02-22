//
//  raspi_magicflute.c
//  ToneGenerator
//
//  Created by 長谷部 雅彦 on 2014/02/22.
//  Copyright (c) 2014年 長谷部 雅彦. All rights reserved.
//

#include	"raspi.h"
#ifdef RASPI

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdbool.h>

#include 	"raspi_magicflute.h"

#include	"raspi_cwrap.h"
#include	"raspi_hw.h"

static int cnt = 0;
static int standardPrs = 0;	//	standard pressure value
static int stockPrs = 0;
static unsigned char lastNote = 0;
static unsigned char lastExp = 0;
static int pressure = 0;
static unsigned short lastSwData = 0;

//-------------------------------------------------------------------------
#if 0
#define	MAX_EXP_WIDTH		40
const unsigned char tExpValue[MAX_EXP_WIDTH] = {
	0,0,0,0,0,24,38,48,56,62,
	68,72,76,80,83,86,89,92,94,96,
	98,100,102,104,106,107,109,110,112,113,
	115,116,117,118,119,120,121,123,124,125
};

#else
#define	MAX_EXP_WIDTH		250
const unsigned char tExpValue[MAX_EXP_WIDTH] = {
	0,	0,	0,	0,	0,	0,	16,	25,	32,	37,
	41,	45,	48,	51,	53,	55,	57,	59,	61,	62,
	64,	65,	67,	68,	69,	70,	71,	72,	73,	74,
	75,	76,	77,	78,	78,	79,	80,	80,	81,	82,
	82,	83,	84,	84,	85,	85,	86,	87,	87,	88,
	88,	89,	89,	90,	90,	91,	91,	91,	92,	92,
	93,	93,	93,	94,	94,	95,	95,	95,	96,	96,
	96,	97,	97,	97,	98,	98,	98,	99,	99,	99,
	100,	100,	100,	101,	101,	101,	101,	102,	102,	102,
	103,	103,	103,	103,	104,	104,	104,	104,	105,	105,
	105,	105,	106,	106,	106,	106,	106,	107,	107,	107,
	107,	108,	108,	108,	108,	108,	109,	109,	109,	109,
	109,	110,	110,	110,	110,	110,	111,	111,	111,	111,
	111,	112,	112,	112,	112,	112,	112,	113,	113,	113,
	113,	113,	113,	114,	114,	114,	114,	114,	114,	115,
	115,	115,	115,	115,	115,	115,	116,	116,	116,	116,
	
	116,	116,	117,	117,	117,	117,	117,	117,	117,	118,
	118,	118,	118,	118,	118,	118,	118,	119,	119,	119,
	119,	119,	119,	119,	120,	120,	120,	120,	120,	120,
	120,	120,	121,	121,	121,	121,	121,	121,	121,	121,
	121,	122,	122,	122,	122,	122,	122,	122,	122,	123,
	123,	123,	123,	123,	123,	123,	123,	123,	124,	124,
	124,	124,	124,	124,	124,	124,	124,	124,	125,	125,
	125,	125,	125,	125,	125,	125,	125,	125,	126,	126,
	126,	126,	126,	126,	126,	126,	126,	126,	127,	127
};
#endif

const unsigned char tSwTable[64] = {
	
	//	0x48, 0x40, 0x41, 0x3e, 0x43, 0x47, 0x45, 0x3c,
	//	0x54, 0x4c, 0x4d, 0x4a, 0x4f, 0x53, 0x51, 0x48,
	//	0x48, 0x40, 0x41, 0x3e, 0x43, 0x47, 0x45, 0x3c,
	//	0x54, 0x4c, 0x4d, 0x4a, 0x4f, 0x53, 0x51, 0x48,
	//	0x47, 0x3f, 0x40, 0x3d, 0x42, 0x46, 0x44, 0x3b,
	//	0x53, 0x4b, 0x4c, 0x49, 0x4e, 0x52, 0x50, 0x47,
	//	0x49, 0x41, 0x42, 0x3f, 0x44, 0x48, 0x46, 0x3d,
	//	0x55, 0x4d, 0x4e, 0x4b, 0x50, 0x54, 0x52, 0x49
	
	0x48, 0x43, 0x41, 0x45, 0x40, 0x47, 0x3e, 0x3c,
	0x47, 0x42, 0x40, 0x44, 0x3f, 0x46, 0x3d, 0x3b,
	0x48, 0x43, 0x41, 0x45, 0x40, 0x47, 0x3e, 0x3c,
	0x49, 0x44, 0x42, 0x46, 0x41, 0x48, 0x3f, 0x3d,
	0x54, 0x4f, 0x4d, 0x51, 0x4c, 0x53, 0x4a, 0x48,
	0x53, 0x4e, 0x4c, 0x50, 0x4b, 0x52, 0x49, 0x47,
	0x54, 0x4f, 0x4d, 0x51, 0x4c, 0x53, 0x4a, 0x48,
	0x55, 0x50, 0x4e, 0x52, 0x4d, 0x54, 0x4b, 0x49
};


//-------------------------------------------------------------------------
//		Touch & Pressure Sencer Input
//-------------------------------------------------------------------------
static int ExcludeAtmospheric( int value )
{
	int tmpVal;
	
	if ( cnt < 100 ){	//	not calculate at first 100 times
		cnt++;
		if ( cnt == 100 ){
			standardPrs = value;
			printf("Standard Pressure is %d\n",value);
		}
		return 0;
	}
	else {
		if (( cnt > 1000 ) && (( stockPrs-1 <= value ) && ( stockPrs+1 >= value ))){
			cnt++;
			if ( cnt > 1050 ){	//	when pressure keep same value by 50 times
				cnt = 1000;
				standardPrs = stockPrs;
				printf("Change Standard Pressure! %d\n",stockPrs);
			}
		}
		else if (( value >= standardPrs+2 ) || ( value <= standardPrs-2 )){
			stockPrs = value;
			cnt = 1001;
		}
		
		tmpVal = value - standardPrs;
		if (( tmpVal < 2 ) && ( tmpVal > -2 )) tmpVal = 0;
		return tmpVal;
	}
}

//-------------------------------------------------------------------------
//		event Loop
//-------------------------------------------------------------------------
void eventLoop( pthread_mutex_t* mutex )
{
	unsigned char msg[3], note, exp;
	int		idt;
	unsigned short swdata;
	
	//	Time Measurement
	struct	timeval tstr;
	long	startTime = 0;
	bool	event = false;
	
	//	Initialize
	exp  = 0;
	msg[0] = 0xb0; msg[1] = 0x0b; msg[2] = 0;
	pthread_mutex_lock( mutex );
	raspiaudio_Message( msg, 3 );
	pthread_mutex_unlock( mutex );
	
	while (1){
		int tempPrs = getPressure();
		if ( tempPrs != 0 ){
			idt = ExcludeAtmospheric( tempPrs );
			if ( pressure != idt ){
				//	protect trembling
				printf("Pressure:%d\n",idt);
				pressure = idt;
				if ( idt < 0 ) idt = 0;
				else if ( idt >= MAX_EXP_WIDTH ) idt = MAX_EXP_WIDTH-1;
				exp = tExpValue[idt];
			}
		}
		if ( exp != lastExp ){
			if ( exp > lastExp ) lastExp++;
			else lastExp--;
			
			//	Generate Expression Event
			msg[0] = 0xb0; msg[1] = 0x0b; msg[2] = lastExp;
			//	Call MSGF
			pthread_mutex_lock( mutex );
			raspiaudio_Message( msg, 3 );
			pthread_mutex_unlock( mutex );
		}
		
		
		//		swdata = getSwData();
		swdata = getTchSwData();
		if ( startTime == 0 ){
			if ( swdata != lastSwData ){
				gettimeofday(&tstr, NULL);
				startTime = tstr.tv_sec * 1000 + tstr.tv_usec/1000;
			}
		}
		else {
			gettimeofday(&tstr, NULL);
			long currentTime = tstr.tv_sec * 1000 + tstr.tv_usec/1000;
			if ( currentTime - startTime > 50 ){	//	over 50msec
				startTime = 0;
				event = true;
			}
		}
		
		if ( event ){
			event = false;
			printf("Switch Data:%04x\n",swdata);
			note = tSwTable[swdata & 0x3f];
			lastSwData = swdata;
			if ( note != 0 ){
				unsigned char color[3] = {0xff,0x00,0x00};
				
				msg[0] = 0x90; msg[1] = note; msg[2] = 0x7f;
				lastNote = note;
				changeColor(color);
			}
			else {
				msg[0] = 0x90; msg[1] = lastNote; msg[2] = 0x00;
			}
			//	Call MSGF
			pthread_mutex_lock( mutex );
			raspiaudio_Message( msg, 3 );
			pthread_mutex_unlock( mutex );
		}
	}
}
//-------------------------------------------------------------------------
#define		MAX_SW_NUM			3
//-------------------------------------------------------------------------
void eventLoopGPIO( pthread_mutex_t* mutex )
{
	unsigned char msg[3];
	int 	i;
	char	gpioPath[64];
	int		fd_in[MAX_SW_NUM], swNew[MAX_SW_NUM], swOld[MAX_SW_NUM] = {1,1,1};
	
	while (1){
		for (i=0; i<MAX_SW_NUM; i++){
			sprintf(gpioPath,"/sys/class/gpio/gpio%d/value",i+9);
			fd_in[i] = open(gpioPath,O_RDWR);
			if ( fd_in[i] < 0 ) exit(EXIT_FAILURE);
		}
		for (i=0; i<MAX_SW_NUM; i++){
			char value[2];
			read(fd_in[i], value, 2);
			if ( value[0] == '0' ) swNew[i] = 0;
			else swNew[i] = 1;
		}
		for (i=0; i<MAX_SW_NUM; i++){
			close(fd_in[i]);
		}
		
		for (i=0; i<MAX_SW_NUM; i++ ){
			if ( swNew[i] != swOld[i] ){
				if ( !swNew[i] ){
					msg[0] = 0x90; msg[1] = 0x3c + 2*i; msg[2] = 0x7f;
					printf("Now KeyOn of %d\n",i);
				}
				else {
					msg[0] = 0x90; msg[1] = 0x3c + 2*i; msg[2] = 0;
					printf("Now KeyOff of %d\n",i);
				}
				//	Call MSGF
				pthread_mutex_lock( mutex );
				raspiaudio_Message( msg, 3 );
				pthread_mutex_unlock( mutex );
				
				swOld[i] = swNew[i];
			}
		}
	}
};
//-------------------------------------------------------------------------
void eventLoopKbd( pthread_mutex_t* mutex )
{
	int	c=0, d=0, e=0, f=0, g=0, a=0, b=0, q=0;
	unsigned char msg[3];
	int key;
	
	while (( key = getchar()) != -1 ){
		bool anykey = false;
		msg[0] = 0x90;
		switch (key){
			case 'c': msg[1] = 0x3c; c?(c=0,msg[2]=0):(c=1,msg[2]=0x7f); anykey = true; break;
			case 'd': msg[1] = 0x3e; d?(d=0,msg[2]=0):(d=1,msg[2]=0x7f); anykey = true; break;
			case 'e': msg[1] = 0x40; e?(e=0,msg[2]=0):(e=1,msg[2]=0x7f); anykey = true; break;
			case 'f': msg[1] = 0x41; f?(f=0,msg[2]=0):(f=1,msg[2]=0x7f); anykey = true; break;
			case 'g': msg[1] = 0x43; g?(g=0,msg[2]=0):(g=1,msg[2]=0x7f); anykey = true; break;
			case 'a': msg[1] = 0x45; a?(a=0,msg[2]=0):(a=1,msg[2]=0x7f); anykey = true; break;
			case 'b': msg[1] = 0x47; b?(b=0,msg[2]=0):(b=1,msg[2]=0x7f); anykey = true; break;
			case 'q': msg[0] = 0xc0; q?(q=0,msg[1]=0):(q=1,msg[1]=0x7f); anykey = true; break;
			default: break;
		}
		if ( anykey == true ){
			//	Call MSGF
			pthread_mutex_lock( mutex );
			raspiaudio_Message( msg, 3 );
			pthread_mutex_unlock( mutex );
		}
	}
};

//-------------------------------------------------------------------------
//			Initialize GPIO
//-------------------------------------------------------------------------
static void initGPIO( void )
{
	int	fd_exp, fd_dir, i;
	char gpiodrv[64];
	
	fd_exp = open("/sys/class/gpio/export", O_WRONLY );
	if ( fd_exp < 0 ){
		printf("Can't open GPIO\n");
		exit(EXIT_FAILURE);
	}
	write(fd_exp,"9",2);
	write(fd_exp,"10",2);
	write(fd_exp,"11",2);
	close(fd_exp);
	
	for ( i=9; i<12; i++ ){
		sprintf(gpiodrv,"/sys/class/gpio/gpio%d/direction",i);
		fd_dir = open(gpiodrv,O_RDWR);
		if ( fd_dir < 0 ){
			printf("Can't set direction\n");
			exit(EXIT_FAILURE);
		}
		write(fd_dir,"in",3);
		close(fd_dir);
	}
}

//-------------------------------------------------------------------------
//			Initialize
//-------------------------------------------------------------------------
void initHw( void )
{
	//	Initialize GPIO
	initGPIO();
	
	//--------------------------------------------------------
	//	Initialize I2C device
	initI2c();
	initLPS331AP();
	//	initSX1509();
	initMPR121();
	initBlinkM();
}
#endif