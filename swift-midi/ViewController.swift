//
//  ViewController.swift
//  SoundGenerator_forMF
//
//  Created by 長谷部 雅彦 on 2015/03/02.
//  Copyright (c) 2015年 長谷部 雅彦. All rights reserved.
//

import UIKit

class ViewController: UIViewController {

	var aout:SoundController? = nil
	
	override func viewDidLoad() {
		super.viewDidLoad()
		// Do any additional setup after loading the view, typically from a nib.
		aout = SoundController(vc: self)
	}

	override func didReceiveMemoryWarning() {
		super.didReceiveMemoryWarning()
		// Dispose of any resources that can be recreated.
	}

	func displayExternalInfo(msg:NSString) {
		textArea.text = msg
	}
	
	@IBOutlet weak var textArea: UILabel!

	@IBAction func touchButton(sender: UIButton) {
		switch sender.tag {
			case 0:
				textArea.text = "KeyOn 3Ch"
				if let ao = aout {
					ao.noteOn(0x3c)
				}
			case 1:
				textArea.text = "KeyOn 3Dh"
				if let ao = aout {
					ao.noteOn(0x3d)
				}
			case 2:
				textArea.text = ""
				if let ao = aout {
					ao.noteOff(0x3c)
					ao.noteOff(0x3d)
				}
			default:
				break
		}
	}

}

