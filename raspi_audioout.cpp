//
//  raspi_audioout.cpp
//  ToneGenerator
//
//  Created by 長谷部 雅彦 on 2013/04/07.
//  Copyright (c) 2013年 長谷部 雅彦. All rights reserved.
//

#include "raspi_audioout.h"

#include "msgf_audio_buffer.h"
#include "msgf_if.h"

static AudioOutput au;

//--------------------------------------------------------
//		Initialize
//--------------------------------------------------------
extern "C" void raspiaudio_Init( void )
{
	au.SetTg( new msgf::Msgf() );
}

//--------------------------------------------------------
//		End
//--------------------------------------------------------
extern "C" void raspaudio_End( void )
{
	void*	tg = au.GetTg();
	delete reinterpret_cast<msgf::Msgf*>(tg);
}

//--------------------------------------------------------
//		Audio Process
//--------------------------------------------------------
extern "C" int	raspiaudio_Process( int16_t* buf, int bufsize )
{
	msgf::TgAudioBuffer	abuf;						//	MSGF IF
	msgf::Msgf*	tg = reinterpret_cast<msgf::Msgf*>(au.GetTg());
	abuf.obtainAudioBuffer(bufsize);				//	MSGF IF
	tg->process( abuf );							//	MSGF IF
	
	for ( int j = 0; j < bufsize; j++ ) {
		int16_t dt = static_cast<int16_t>(abuf.getAudioBuffer(j) * 15000);
		buf[j] = dt;
    }
	
	abuf.releaseAudioBuffer();						//	MSGF IF

	return 1;
}

//--------------------------------------------------------
//		Receive Message
//--------------------------------------------------------
extern "C" int	raspiaudio_Message( unsigned char* message, int msgsize )
{
	unsigned char	msg[3];
	msgf::Msgf*	tg = reinterpret_cast<msgf::Msgf*>(au.GetTg());

	msg[0] = *message;
	msg[1] = *(message+1);
	msg[2] = *(message+2);
	tg->sendMessage( 3, msg );						//	MSGF IF

	return 1;
}

