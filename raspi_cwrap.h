//
//  raspi_cwrap.h
//  ToneGenerator
//
//  Created by 長谷部 雅彦 on 2013/04/07.
//  Copyright (c) 2013年 長谷部 雅彦. All rights reserved.
//

#ifndef raspi_cwrap_h
#define raspi_cwrap_h

extern void raspiaudio_Init( void );
extern void raspaudio_End( void );
extern int	raspiaudio_Process( int16_t* buf, int bufsize );
extern int	raspiaudio_Message( unsigned char* msg, int msgsize );

#endif
