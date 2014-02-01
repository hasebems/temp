//
//  raspi_main.c
//
//  Created by Masahiko Hasebe on 2013/05/18.
//  Copyright (c) 2013 by Masahiko Hasebe(hasebems). All rights reserved.
//
#include	"raspi.h"
#ifdef RASPI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <sys/time.h>
#include <math.h>

#include <alsa/asoundlib.h>
#include <pthread.h>

#include	"raspi_cwrap.h"
#include	"raspi_hw.h"

//-------------------------------------------------------------------------
//			Variables
//-------------------------------------------------------------------------
static const char *device = "plughw:0,0";                     /* playback device */
static snd_pcm_format_t format = SND_PCM_FORMAT_S16;    /* sample format */
static unsigned int samplingRate = 44100;                       /* stream rate */
static unsigned int channels = 1;                       /* count of channels */
static unsigned int buffer_time = 60000;                /* ring buffer length in us */
static unsigned int period_time = 15000;                /* period time in us */
static double freq = 440;                               /* sinusoidal wave frequency in Hz */
static int verbose = 0;                                 /* verbose flag */
static int resample = 1;                                /* enable alsa-lib resampling */
static int period_event = 0;                            /* produce poll event after each period */

static snd_pcm_sframes_t buffer_size;
static snd_pcm_sframes_t period_size;
static snd_output_t *output = NULL;

//-------------------------------------------------------------------------
//		Generate Wave : MSGF
//-------------------------------------------------------------------------
static void generate_wave(const snd_pcm_channel_area_t *areas,
                          snd_pcm_uframes_t offset,
                          int maxCount, double *_phase)
{
	unsigned char *samples[channels];
	int steps[channels];
	unsigned int chn;
	int count = 0;
	int format_bits = snd_pcm_format_width(format);
	unsigned int maxval = (1 << (format_bits - 1)) - 1;
	int bps = format_bits / 8;  /* bytes per sample */
	int phys_bps = snd_pcm_format_physical_width(format) / 8;
	int big_endian = snd_pcm_format_big_endian(format) == 1;
	int to_unsigned = snd_pcm_format_unsigned(format) == 1;
	int is_float = (format == SND_PCM_FORMAT_FLOAT_LE ||
					format == SND_PCM_FORMAT_FLOAT_BE);
	
	/* verify and prepare the contents of areas */
	for (chn = 0; chn < channels; chn++) {
		if ((areas[chn].first % 8) != 0) {
			printf("areas[%i].first == %i, aborting...\n", chn, areas[chn].first);
			exit(EXIT_FAILURE);
		}
		samples[chn] = /*(signed short *)*/(((unsigned char *)areas[chn].addr) + (areas[chn].first / 8));
		if ((areas[chn].step % 16) != 0) {
			printf("areas[%i].step == %i, aborting...\n", chn, areas[chn].step);
			exit(EXIT_FAILURE);
		}
		steps[chn] = areas[chn].step / 8;
		samples[chn] += offset * steps[chn];
	}
	
	//	get wave data
	int16_t* buf = (int16_t*)malloc(sizeof(int16_t) * maxCount);
	raspiaudio_Process( buf, maxCount );
	
	/* fill the channel areas */
	while (count < maxCount) {
		int i;
		int16_t res = buf[count++];
		
		if (to_unsigned)
			res ^= 1U << (format_bits - 1);
		
		for (chn = 0; chn < channels; chn++) {
			/* Generate data in native endian format */
			if (big_endian) {
				for (i = 0; i < bps; i++)
					*(samples[chn] + phys_bps - 1 - i) = (res >> i * 8) & 0xff;
			} else {
				for (i = 0; i < bps; i++)
					*(samples[chn] + i) = (res >>  i * 8) & 0xff;
			}
			samples[chn] += steps[chn];
		}
	}
	free(buf);
}
//-------------------------------------------------------------------------
//			ALSA Hardware Parameters
//-------------------------------------------------------------------------
static int set_hwparams(snd_pcm_t *handle,
                        snd_pcm_hw_params_t *params)
{
	unsigned int rrate;
	snd_pcm_uframes_t size;
	int err, dir;
	
	/* choose all parameters */
	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0) {
		printf("Broken configuration for playback: no configurations available: %s\n", snd_strerror(err));
		return err;
	}
	
	/* set hardware resampling */
	err = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
	if (err < 0) {
		printf("Resampling setup failed for playback: %s\n", snd_strerror(err));
		return err;
	}
	
	/* set the interleaved read/write format */
	err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_MMAP_INTERLEAVED );
	if (err < 0) {
		printf("Access type not available for playback: %s\n", snd_strerror(err));
		return err;
	}
	
	/* set the sample format */
	err = snd_pcm_hw_params_set_format(handle, params, format);
	if (err < 0) {
		printf("Sample format not available for playback: %s\n", snd_strerror(err));
		return err;
	}
	
	/* set the count of channels */
	err = snd_pcm_hw_params_set_channels(handle, params, channels);
	if (err < 0) {
		printf("Channels count (%i) not available for playbacks: %s\n", channels, snd_strerror(err));
		return err;
	}
	
	/* set the stream rate */
	rrate = samplingRate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
	if (err < 0) {
		printf("Rate %iHz not available for playback: %s\n", samplingRate, snd_strerror(err));
		return err;
	}
	if (rrate != samplingRate) {
		printf("Rate doesn't match (requested %iHz, get %iHz)\n", samplingRate, err);
		return -EINVAL;
	}
	
	/* set the buffer time */
	err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, &dir);
	if (err < 0) {
		printf("Unable to set buffer time %i for playback: %s\n", buffer_time, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_get_buffer_size(params, &size);
	if (err < 0) {
		printf("Unable to get buffer size for playback: %s\n", snd_strerror(err));
		return err;
	}
	buffer_size = size;
	
	/* set the period time */
	err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
	if (err < 0) {
		printf("Unable to set period time %i for playback: %s\n", period_time, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_get_period_size(params, &size, &dir);
	if (err < 0) {
		printf("Unable to get period size for playback: %s\n", snd_strerror(err));
		return err;
	}
	period_size = size;
	
	/* write the parameters to device */
	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		printf("Unable to set hw params for playback: %s\n", snd_strerror(err));
		return err;
	}
	return 0;
}


//-------------------------------------------------------------------------
//			ALSA Software Parameters
//-------------------------------------------------------------------------
static int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams)
{
	int err;
	
	/* get the current swparams */
	err = snd_pcm_sw_params_current(handle, swparams);
	if (err < 0) {
		printf("Unable to determine current swparams for playback: %s\n", snd_strerror(err));
		return err;
	}
	
	/* start the transfer when the buffer is almost full: */
	/* (buffer_size / avail_min) * avail_min */
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, (buffer_size / period_size) * period_size);
	if (err < 0) {
		printf("Unable to set start threshold mode for playback: %s\n", snd_strerror(err));
		return err;
	}
	
	/* allow the transfer when at least period_size samples can be processed */
	/* or disable this mechanism when period event is enabled (aka interrupt like style processing) */
	err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_event ? buffer_size : period_size);
	if (err < 0) {
		printf("Unable to set avail min for playback: %s\n", snd_strerror(err));
		return err;
	}
	
	/* enable period events when requested */
	if (period_event) {
		err = snd_pcm_sw_params_set_period_event(handle, swparams, 1);
		if (err < 0) {
			printf("Unable to set period event: %s\n", snd_strerror(err));
			return err;
		}
	}
	
	/* write the parameters to the playback device */
	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0) {
		printf("Unable to set sw params for playback: %s\n", snd_strerror(err));
		return err;
	}
	return 0;
}
//-------------------------------------------------------------------------
//			Underrun and suspend recovery
//-------------------------------------------------------------------------
static int xrun_recovery(snd_pcm_t *handle, int err)
{
	if (verbose)
		printf("stream recovery\n");
	if (err == -EPIPE) {    /* under-run */
		err = snd_pcm_prepare(handle);
		if (err < 0)
			printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(handle)) == -EAGAIN)
			sleep(1);       /* wait until the suspend flag is released */
		if (err < 0) {
			err = snd_pcm_prepare(handle);
			if (err < 0)
				printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
		}
		return 0;
	}
	return err;
}
//-------------------------------------------------------------------------
//		Audio Theread  /  Transfer method - direct write only
//-------------------------------------------------------------------------
struct THREAD_INFO {
	snd_pcm_t*			alsaHandle;
	pthread_mutex_t*	mutexHandle;
};
//-------------------------------------------------------------------------
#define		BEGIN_TRUNCATE			80	//	percent
//-------------------------------------------------------------------------
static void writeAudioToDriver( THREAD_INFO* inf, snd_pcm_t* handle, double* phase, int* first )
{
	const snd_pcm_channel_area_t *my_areas;
	snd_pcm_uframes_t	offset, frames, size;
	snd_pcm_sframes_t	commitres;
	int err;

	//	Time Measurement
	struct	timeval ts;
	struct	timeval te;
	long	startTime, endTime, execTime, latency, limit;
	gettimeofday(&ts, NULL);
	
	size = period_size;
	while (size > 0) {
		frames = size;
		err = snd_pcm_mmap_begin(handle, &my_areas, &offset, &frames);
		if (err < 0) {
			if ((err = xrun_recovery(handle, err)) < 0) {
				printf("MMAP begin avail error: %s\n", snd_strerror(err));
				exit(EXIT_FAILURE);
			}
			*first = 1;
		}
	
		//	Call MSGF
		pthread_mutex_lock( inf->mutexHandle );
		generate_wave(my_areas, offset, frames, phase);
		pthread_mutex_unlock( inf->mutexHandle );

		commitres = snd_pcm_mmap_commit(handle, offset, frames);
		if (commitres < 0 || (snd_pcm_uframes_t)commitres != frames) {
			if ((err = xrun_recovery(handle, commitres >= 0 ? -EPIPE : commitres)) < 0) {
				printf("MMAP commit error: %s\n", snd_strerror(err));
				exit(EXIT_FAILURE);
			}
			*first = 1;
		}
		size -= frames;
	}

	//	Time Measurement
	gettimeofday(&te, NULL);
	startTime = ts.tv_sec * 1000 + ts.tv_usec/1000;
	endTime = te.tv_sec * 1000 + te.tv_usec/1000;
	execTime = endTime - startTime;
	latency = period_size*1000/samplingRate;
	limit = period_size*BEGIN_TRUNCATE*10/samplingRate;

	//	Reduce Resource
	if ( limit < execTime ){
		pthread_mutex_lock( inf->mutexHandle );
		raspiaudio_ReduceResource();
		pthread_mutex_unlock( inf->mutexHandle );

		printf("processing time = %d/%d[msec]\n", execTime, latency);
	}
}
//-------------------------------------------------------------------------
static void* audioThread( void* thInfo )
{
	THREAD_INFO* inf = (THREAD_INFO*)thInfo;
	snd_pcm_t* handle = inf->alsaHandle;
	double phase = 0;
	snd_pcm_sframes_t avail;
	snd_pcm_state_t state;
	int err, first = 1;
	
	while (1) {
		state = snd_pcm_state(handle);
		if (state == SND_PCM_STATE_XRUN) {
			err = xrun_recovery(handle, -EPIPE);
			if (err < 0) {
				printf("XRUN recovery failed: %s\n", snd_strerror(err));
				goto END_OF_THREAD;
				//return err;
			}
			first = 1;
		} else if (state == SND_PCM_STATE_SUSPENDED) {
			err = xrun_recovery(handle, -ESTRPIPE);
			if (err < 0) {
				printf("SUSPEND recovery failed: %s\n", snd_strerror(err));
				goto END_OF_THREAD;
				//return err;
			}
		}
		
		avail = snd_pcm_avail_update(handle);
		if (avail < 0) {
			err = xrun_recovery(handle, avail);
			if (err < 0) {
				printf("avail update failed: %s\n", snd_strerror(err));
				goto END_OF_THREAD;
				//return err;
			}
			first = 1;
			continue;
		}

		if (avail < period_size) {
			if (first) {
				first = 0;
				err = snd_pcm_start(handle);
				if (err < 0) {
					printf("Start error: %s\n", snd_strerror(err));
					exit(EXIT_FAILURE);
				}
			} else {
				err = snd_pcm_wait(handle, -1);
				if (err < 0) {
					if ((err = xrun_recovery(handle, err)) < 0) {
						printf("snd_pcm_wait error: %s\n", snd_strerror(err));
						exit(EXIT_FAILURE);
					}
					first = 1;
				}
			}
			continue;
		}

		writeAudioToDriver( inf, handle, &phase, &first );
	}
	
END_OF_THREAD:
	return (void *)NULL;
}
//-------------------------------------------------------------------------
//		Original Thread		/	Input Message
//-------------------------------------------------------------------------
static int cnt = 0;
static int standardPrs = 0;	//	standard pressure value
static int stockPrs = 0;
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
//	0,0,0,0,17,26,33,38,43,		46,50,52,55,57,59,61,
//	63,65,66,68,69,70,72,73,	74,75,76,77,78,79,80,80,
//	81,82,83,84,84,85,86,86,	87,88,88,89,89,90,90,91,
//	91,92,92,93,93,94,94,95,	95,96,96,97,97,97,98,98,
//	99,99,99,100,100,100,101,101,	101,102,102,102,103,103,103,104,
//	104,104,105,105,105,106,106,106,	106,107,107,107,107,108,108,108,
//	109,109,109,109,110,110,110,110,	110,111,111,111,111,112,112,112,
//	112,112,113,113,113,113,114,114,	114,114,114,115,115,115,115,115,
//	116,116,116,116,116,116,117,117,	117,117,117,118,118,118,118,118,
//	118,119,119,119,119,119,119,120,	120,120,120,120,120,120,121,121,
//	121,121,121,121,122,122,122,122,	122,122,122,123,123,123,123,123,
//	123,123,124,124,124,124,124,124,	124,124,125,125,125,125,125,125,
//	125,125,126,126,126,126,126,127

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
static unsigned char lastNote = 0;
static unsigned char lastExp = 0;
static int pressure = 0;
static unsigned short lastSwData = 0;
//-------------------------------------------------------------------------
static void inputFromSwAndExp( pthread_mutex_t* mutex )
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
				msg[0] = 0x90; msg[1] = note; msg[2] = 0x7f;
				lastNote = note;
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
static void inputFromGPIO( pthread_mutex_t* mutex )
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
static void inputFromKeyboard( pthread_mutex_t* mutex )
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
static int soundGenerateLoop(snd_pcm_t *handle )
{
	int	rtn;
	THREAD_INFO thInfo;
	pthread_mutex_t	mutex;
	pthread_t		threadId;
	
	//	Initialize Variables
	thInfo.alsaHandle = handle;
	thInfo.mutexHandle = &mutex;
	
	//	Create Audio Thread
	pthread_mutex_init(&mutex,NULL);
	rtn = pthread_create( &threadId, NULL, audioThread, (void *)&thInfo );
	if (rtn != 0) {
		fprintf(stderr, "pthread_create() failed for %d.", rtn);
		exit(EXIT_FAILURE);
	}
	
	//	Get MIDI Command
	//inputFromKeyboard( &mutex );
	//inputFromGPIO( &mutex );
	inputFromSwAndExp( &mutex );
	
	//	End of Thread
	pthread_join( threadId, NULL );
}
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
//			HELP
//-------------------------------------------------------------------------
static void help(void)
{
	int k;
	printf(
		   "Usage: pcm [OPTION]... [FILE]...\n"
		   "-h,--help      help\n"
		   "-D,--device    playback device\n"
		   "-r,--rate      stream rate in Hz\n"
		   "-c,--channels  count of channels in stream\n"
		   "-f,--frequency sine wave frequency in Hz\n"
		   "-b,--buffer    ring buffer size in us\n"
		   "-p,--period    period size in us\n"
		   "-o,--format    sample format\n"
		   "-v,--verbose   show the PCM setup parameters\n"
		   "-n,--noresample  do not resample\n"
		   "-e,--pevent    enable poll event after each period\n"
		   "\n");
	printf("Recognized sample formats are:");
	for (k = 0; k < SND_PCM_FORMAT_LAST; ++k) {
		const char *s = (const char *)snd_pcm_format_name((snd_pcm_format_t)k);
		if (s)
			printf(" %s", s);
	}
	printf("\n");
}
//-------------------------------------------------------------------------
//			Option Command
//-------------------------------------------------------------------------
static int optionCommand(int morehelp, int argc, char *argv[])
{
	struct option long_option[] =
	{
		{"help", 0, NULL, 'h'},
		{"device", 1, NULL, 'D'},
		{"samplingRate", 1, NULL, 'r'},
		{"channels", 1, NULL, 'c'},
		{"frequency", 1, NULL, 'f'},
		{"buffer", 1, NULL, 'b'},
		{"period", 1, NULL, 'p'},
		{"format", 1, NULL, 'o'},
		{"verbose", 1, NULL, 'v'},
		{"noresample", 1, NULL, 'n'},
		{"pevent", 1, NULL, 'e'},
		{NULL, 0, NULL, 0},
	};

	while (1) {
		int c;
		if ((c = getopt_long(argc, argv, "hD:r:c:f:b:p:m:o:vne", long_option, NULL)) < 0)
			break;
		switch (c) {
			case 'h':
				morehelp++;
				break;
			case 'D':
				device = strdup(optarg);
				break;
			case 'r':
				samplingRate = atoi(optarg);
				samplingRate = samplingRate < 4000 ? 4000 : samplingRate;
				samplingRate = samplingRate > 196000 ? 196000 : samplingRate;
				break;
			case 'c':
				channels = atoi(optarg);
				channels = channels < 1 ? 1 : channels;
				channels = channels > 1024 ? 1024 : channels;
				break;
			case 'f':
				freq = atoi(optarg);
				freq = freq < 50 ? 50 : freq;
				freq = freq > 5000 ? 5000 : freq;
				break;
			case 'b':
				buffer_time = atoi(optarg);
				buffer_time = buffer_time < 1000 ? 1000 : buffer_time;
				buffer_time = buffer_time > 1000000 ? 1000000 : buffer_time;
				break;
			case 'p':
				period_time = atoi(optarg);
				period_time = period_time < 1000 ? 1000 : period_time;
				period_time = period_time > 1000000 ? 1000000 : period_time;
				break;
			case 'o':
				int _format;
				for (_format = 0; _format < SND_PCM_FORMAT_LAST; _format++) {
					const char *format_name = (const char *)snd_pcm_format_name((snd_pcm_format_t)_format);
					if (format_name)
						if (!strcasecmp(format_name, optarg))
							break;
				}
				if (format == SND_PCM_FORMAT_LAST)
					format = SND_PCM_FORMAT_S16;
				if (!snd_pcm_format_linear(format) &&
					!(format == SND_PCM_FORMAT_FLOAT_LE ||
					  format == SND_PCM_FORMAT_FLOAT_BE)) {
						printf("Invalid (non-linear/float) format %s\n",
							   optarg);
						return 1;
					}
				break;
			case 'v':
				verbose = 1;
				break;
			case 'n':
				resample = 0;
				break;
			case 'e':
				period_event = 1;
				break;
			default: break;
		}
	}
	return morehelp;
}
//-------------------------------------------------------------------------
//			MAIN
//-------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	snd_pcm_t *handle;
	int err, morehelp;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
	signed short *samples;
	unsigned int chn;
	snd_pcm_channel_area_t *areas;
	snd_pcm_hw_params_alloca(&hwparams);
	snd_pcm_sw_params_alloca(&swparams);

	//--------------------------------------------------------
	//	Check Initialize Parameter
	morehelp = 0;
	morehelp = optionCommand( morehelp, argc, argv );
	
	if (morehelp) {
		help();
		return 0;
	}
	
	err = snd_output_stdio_attach(&output, stdout, 0);
	if (err < 0) {
		printf("Output failed: %s\n", snd_strerror(err));
		return 0;
	}
	
	printf("Playback device is %s\n", device);
	printf("Stream parameters are %iHz, %s, %i channels\n", samplingRate, snd_pcm_format_name(format), channels);
	printf("Sine wave rate is %.4fHz\n", freq);
	

	//--------------------------------------------------------
	//	ALSA Settings
	if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		return 0;
	}

	if ((err = set_hwparams(handle, hwparams)) < 0) {
		printf("Setting of hwparams failed: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}
	if ((err = set_swparams(handle, swparams)) < 0) {
		printf("Setting of swparams failed: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}
	
	printf("Buffer Size is %d\n", buffer_size);
	printf("Period Size is %d\n", period_size);

	//	reserve memory
	if (verbose > 0)
		snd_pcm_dump(handle, output);
	samples = (signed short *)malloc((period_size * channels * snd_pcm_format_physical_width(format)) / 8);
	if (samples == NULL) {
		printf("No enough memory\n");
		exit(EXIT_FAILURE);
	}
	
	//	create information by snd_pcm_channel_area_t
	areas = (snd_pcm_channel_area_t *)calloc(channels, sizeof(snd_pcm_channel_area_t));
	if (areas == NULL) {
		printf("No enough memory\n");
		exit(EXIT_FAILURE);
	}
	for (chn = 0; chn < channels; chn++) {
		areas[chn].addr = samples;
		areas[chn].first = chn * snd_pcm_format_physical_width(format);
		areas[chn].step = channels * snd_pcm_format_physical_width(format);
	}

	//--------------------------------------------------------
	//	Call Init MSGF
	raspiaudio_Init();	

	//--------------------------------------------------------
	//	Initialize GPIO
	initGPIO();
	
	//--------------------------------------------------------
	//	Initialize I2C device
	initI2c();
	initLPS331AP();
//	initSX1509();
	initMPR121();

	//--------------------------------------------------------
	//	Main Loop
	err = soundGenerateLoop(handle);
	if (err < 0)
		printf("Transfer failed: %s\n", snd_strerror(err));
	free(areas);
	free(samples);
	snd_pcm_close(handle);

	//--------------------------------------------------------
	//	Quit Hardware
	quitI2c();

	//--------------------------------------------------------
	//	Call End of MSGF
	raspiaudio_End();
	
	return 0;
}
#endif
