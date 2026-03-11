# Group Name/Number
LuckyLlama, Group 6

# Purpose
The purpose of this lab is to dig into I2C and SPI and try to implement both onto a simulated chip. By implementing I2C and SPI into the same chip, we can then try to replicate the MPU6000 chip we have talked about in class. Utilizing Wokwis custom SPI and I2C API's, this assignment replicates the functionality of the MPU6000 chip.

# Introduction
The MPU6000 chip features a temperature sensor, accelerometer, and SPI and I2C interfacing. To implement a simplified version in Wokwi without registers, we need to mock accelerometer data and pass through an analog temperature sensor to ensure that there is data. Then implement SPI and I2C using the Wokwi API's, this allows us to instantiate the communication interface on the Pico and talk to this chip as if there was a full SPI and I2C protocol along with registers. 
There is the option to either wire up the SPI and I2C to the same ports and as we begin communication we write in what communication we want to be doing. The other simpler way is to have separate input lines and be listening to both and outputting the data when requested.
The scope that we are replicating the MPU6000 is with the acceleration registers, PWR_MGMT_1 to put the device to sleep and to wake it up, and WHO_AM_I for the device ID.

# Assignment specific details
This assignment was done initally as an SPI chip and a I2C chip, utilizing minimal components from the FreeRTOS example. For our implementation, we foregone the FreeRTOS due to the amount of files it created, and instead opted for a sleep loop that calls on the chip. The reason for initially two chips is that without FreeRTOS, some interactions may not be the same. To ensure that we could switch from SPI to I2C and back, having two chips was a good way to decentralize the code and remove error prone sections. 
The SPI chip was taken from the example and modified to work with randomized accelerometer data and an increase in the buffer size to accommodate it. 
The I2C chip had its fair share of issues to get going. But utilizing the api documentation and how others have implemented it (https://github.com/bonnyr/wokwi-pca9685-custom-chip/blob/main/src/pca9685.chip.c#L209), we could then make comparable functions between the code and the SPI implementation. 
The main.c code was mainly used from Lab 2 communication methods, with the addition of a button to switch between the two. This button showcases both communication methods working with the chip, while keeping the code overhead minimal (no queues and tasks).


The wiring diagram is as follows:
GPIO 28: BUTTON
GPIO 21: CS
GPIO 20: MISO
GPIO 19: MOSI
GPIO 18: SCK
GPIO 17: SCL
GPIO 16: SDA
VBUS: Analog Temperature sensor, Chip VCC
GND: Analog Temperature sensor, Chip GND
