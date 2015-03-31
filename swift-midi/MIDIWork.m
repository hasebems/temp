//
//  MIDIWork.m
//  SoundGenerator_forMF
//
//  Created by 長谷部 雅彦 on 2015/03/24.
//  Copyright (c) 2015年 長谷部 雅彦. All rights reserved.
//

#import <Foundation/Foundation.h>

#import "MIDIWork.h"

@interface MidiWork () {
	bool	drvIsAvailable;
}
@property (nonatomic, strong) MIDIDriver *midiDriver;
@property (nonatomic, weak) ReceiveMidi rmdFunc;
@end

@implementation MidiWork

- (id) init
{
	self = [super init];
	if (self != nil) {
		_midiDriver = [[MIDIDriver alloc] init];

		if (_midiDriver.isAvailable == NO) {
			drvIsAvailable = false;
			return self;
		}
		drvIsAvailable = true;

		// Setup the callback for receiving MIDI message.
		__weak ReceiveMidi rmdFunc = _rmdFunc;
		_midiDriver.onMessageReceived = ^(ItemCount index, NSData *receivedData, uint64_t timestamp) {
			const void* rcvDt = [receivedData bytes];
			if ( rmdFunc != nil ){
				rmdFunc( *((unsigned char*)rcvDt), *((unsigned char*)rcvDt + 1), *((unsigned char*)rcvDt + 2));
			}
		};
		
		_midiDriver.onDestinationPortAdded = ^(ItemCount index) {
		};

		_midiDriver.onSourcePortAdded = ^(ItemCount index) {
		};
		
		_midiDriver.onDestinationPortRemoved = ^(ItemCount index) {
		};
		
		_midiDriver.onSourcePortRemoved = ^(ItemCount index) {
		};
		
	
	}
	return self;
}

- (bool)midiDrvIsAvailable
{
	return drvIsAvailable;
}

- (void)registCallback:(ReceiveMidi)rmd
{
	_rmdFunc = rmd;
}
@end