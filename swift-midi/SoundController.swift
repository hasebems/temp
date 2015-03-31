//
//  SoundController.swift
//  SoundGenerator_forMF
//
//  Created by 長谷部 雅彦 on 2015/03/08.
//  Copyright (c) 2015年 長谷部 雅彦. All rights reserved.
//

import Foundation

class SoundController : NSObject
{
	let ssEngine = AudioOutput()
	unowned var vCntr:ViewController

	init( vc:ViewController ){
		vCntr = vc
		super.init()
		var mw = MidiWork()

		mw.registCallback { (stsByte: UInt8, dt1Byte: UInt8, dt2Byte: UInt8) -> Void in
			self.disp("Receive MIDI Message!")
			self.ssEngine.receiveMidi( stsByte, msg2:dt1Byte, msg3:dt2Byte )
		}

		var av = mw.midiDrvIsAvailable()
		if ( av == false ){
			disp("MIDI Driver is not Available")
		}
	}
	
	func disp(text:NSString) {
		vCntr.displayExternalInfo(text)
	}
	
	func noteOn( noteNum: UInt8 ){
		ssEngine.receiveMidi(0x90, msg2: noteNum, msg3: 0x7f)
	}
	func noteOff( noteNum: UInt8 ){
		ssEngine.receiveMidi(0x90, msg2: noteNum, msg3: 0x00)
	}
}