RB:
-> Headphone output not controllable, just plays 100% master afaik (vol & cue/master knobs, cue assign buttons per deck)
-> Touchscreen not working? Or registering touches at wrong positions
-> No way to swich between sources?
  -> Multiple USBS / Usb hub?
  -> Will rekordbox LINK work?
-> Button feedback lights
-> CPU usage to draw screen, is it possible to optimize with GUI Hardware accel?

-> Remap my broken tempo slider control assignment
    Broken tempo slider range 
    
    meaurements (in the "100%" tempo range mode):
    Fully up: -100% (correct)
    Middle: -47.45% (incorrect, expected 0%)
    Fully down: -0.01% (incorrect, expected +100%)
    Full down, with pressure on the slider: Jitters between +0% ~ +6%, but we should probably clamp the entire positive range

-> Based on the other repo where we root the device: /Users/jimmy/src/djinn
  Can we patch the denon runtime itself to read rekordbox libraries directly from usb? i.e skip the conversion step
