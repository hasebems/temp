//
//  raspi_hw.h
//  ToneGenerator
//
//  Created by 長谷部 雅彦 on 2013/08/13.
//  Copyright (c) 2013年 長谷部 雅彦. All rights reserved.
//

#ifndef ToneGenerator_raspi_hw_h
#define ToneGenerator_raspi_hw_h

void initI2c( void );
void quitI2c( void );
void writeI2c( unsigned char adrs, unsigned char data );
unsigned char readI2c( unsigned char adrs );
void initLPS331AP( void );


#endif
