modulation Ideas:


Idea: 
modulation morphing: when changing a mouldation pattern is wouldn't just snap into the next pattern, it would change (with a max step per clock/trigger set), to slowly move into the next pattern, the current morph can be locked mid morph, that new modulation can be saved, and and then another modulation to morph can be applied

"interpolate" option/toggle for modulation: if this is selected then the values for modulation are outputted at a set frequency (maybe every 1/4 of a clock or faster?) the values that are outputting attempt to interpolate the two different set modulation values, the options for interpolate would be linear (in which case each increase per unit of time is the same), or smooth (in which case the value would slowly change  on the first units of time, the change would increase in the middle of the transition, and then the change would slow towards the end), creating a smooth wave between set modulations. The time be configuratble to "on hit" or "on clock" of course (thus if its "on hit" and the pattern changes, the modulation values would need to change too, but if morph was selected then it would slowly change to the new modulation that was effected by the pattern change )


Live play:
Knob reactions: when playing live, there should be knobs that can be used to adjust parameters, if you go to change a knob, it start from where the current modulation is at, when finished adjusting you can press the knob to hold its current position, or it will slowly drift back into the modulation that it is playing, should also enable recording knob changes live and saving it as a modulation pattern