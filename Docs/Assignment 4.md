# Group Name/Number
LuckyLlama, Group 6

# Purpose
Extending the interface that was developed for Assignment 3 that was about using PIO to receive I2S data from an INMP441 microphone. The extension of this is to implement an FIR filter and learn about signal processing by working directly with building a filter. 

# Introduction
Implementing an FIR filter began with researching available options in programming languages. The approach that was often seen was a dual buffer approach for inputting the data and working on one set and the other gets filled in with incoming data. The other approach was a circular buffer approach, but this one on approach seemed harder to implement so we stayed away from this and came at it with the dual buffer. 

The core part of the FIR filter is finding an appropriate set of coefficients to apply in the sigma. But because we do not know what these will be, we instead will be implementing a band pass filter (to get frequencies in the band that are around 500hz; yoinked it from this git repo https://github.com/nathanLoretan/FIR_Filter/blob/master/FIR_filter/fir_filter.cpp). We want to see if a window function is necessary first and if we can get consistent results then we will stick with it. 

# Assignment specific details
To implement this we found that having two filters back to back gave a cleaner result with more consistent and believable values. With just one filter, there would sometimes be false positives where playing a 1000hz tone would give 500hz results similar to playing a 500hz tone. This was likely due to natural noise in the wiring and how the pico got the analog data. Implementing this filter meant that data would have whatever noise came in from the hardware and potentially never be removed from the signal. This noise can be significant enough that the signal as a whole is just garbage and the software might be none the wiser to it. But the advantage is that tuning the filter to different tones becomes easier to specify, change, and pin point. On hardware this would involve either changing the circuit design or having a set range of frequencies to implement a filter on. 




