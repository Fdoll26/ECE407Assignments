# Group Name/Number
LuckyLlama, Group 6

# Purpose
The purpose of this lab is to dive into utilizing PIO to interface with external devices / sensors. By implementing the PIO we uncover the inner workings of sensor interfacing and clock timings. By reading the datasheet we can the implement methods to read the data and convert it into working signals.

# Introduction
The INMP441 microphone has built in I2S functionality right out of the gate, allowing users to switch what channel the microphone is listening on with pulse code modulation. This has interface capabilities with the pico and there is numerous libraries for micropython or arduino that allow plug and play functionality with the pico. In this lab we will be utilizing this existing knowledge to implement a PIO version of their implementations. Showcasing knowledge of how the I2S communication protocol works and how we can refine and utilize it further in project 3 and other personal projects in the future.

# Assignment specific details
This assignment had many difficulties when it came to the PIO, there was implementations online that got it part of the way through but not fully. There would sometimes be issues with the number of bytes being stored or how the implementation was compared to the way it was setup. The pin setup was as follows: SD -> 10, SCK -> 11, WS -> 12, LR -> 13. The implementations that were found had the SD and WS right next to one another, the reason I did not do this was because I had wires connected neatly beside one another from the pico pins to the board. Then implementing the c file, I took from how I implemented PIO for the ring LED and copied it into the program and built it around reading 32 byte boundary in but only using 24 bytes as that is what the microphone produces, where the rest of the bytes are garbage (just noise). Then taking the buffer data and processing it using another persons implementation with micropython where they used the absolute sum and sum of squares to get the root mean square and calculate the decibel level from it and print it out finally.
