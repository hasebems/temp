//
//  MIDIWork.h
//  SoundGenerator_forMF
//
//  Created by 長谷部 雅彦 on 2015/03/24.
//  Copyright (c) 2015年 長谷部 雅彦. All rights reserved.
//

#ifndef SoundGenerator_forMF_MIDIWork_h
#define SoundGenerator_forMF_MIDIWork_h

#import "MIDIDriver.h"

typedef void (^ReceiveMidi)(UInt8,UInt8,UInt8);

@interface MidiWork : NSObject
- (id) init;
- (bool)midiDrvIsAvailable;
- (void)registCallback : (ReceiveMidi)rmd;
@end

#endif
