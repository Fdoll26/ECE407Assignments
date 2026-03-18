from machine import I2S, Pin
import math
import time
import array
# --- CONFIGURATION ---
SCK_PIN = 11
WS_PIN = 12
SD_PIN = 10
# I2S Config
I2S_ID = 0
SAMPLE_RATE = 16000
BITS_PER_SAMPLE = 32
BUFFER_LENGTH = 64
# --- CALIBRATION VALUE ---
DB_OFFSET = -46.72
# Initialize I2S
audio_in = I2S(
    I2S_ID,
    sck=Pin(SCK_PIN),
    ws=Pin(WS_PIN),
    sd=Pin(SD_PIN),
    mode=I2S.RX,
    bits=BITS_PER_SAMPLE,
    format=I2S.STEREO,
    rate=SAMPLE_RATE,
    ibuf=2048 # Internal buffer size
)
# Create a buffer to store the raw bytes read from the I2S
read_buffer = bytearray(BUFFER_LENGTH * 4)
print("Starting Decibel Meter...")
while True:
    # Read data from the INMP441 into the buffer
    num_bytes_read = audio_in.readinto(read_buffer)
    # Determine how many samples we actually read
    samples_read = num_bytes_read // 4 
    # How does this get number of samples? this just counts number of bits and divides 
    # it by 4 which is a byte boundary, this is wrong
    print(f"Samples read: {samples_read}")
    if samples_read > 0:
        # **FIXED: Use array.array to cast bytes to 32-bit signed integers ('i')**
        mic_samples = array.array('i', read_buffer)
        sum_squares = 0.0
        for i in range(samples_read):
            # Shift right by 8 to get the correct 24-bit integer value
            processed_sample = mic_samples[i] >> 8
            # Accumulate sum of squares
            sum_squares += processed_sample * processed_sample
            # Calculate RMS
        rms = math.sqrt(sum_squares / samples_read)
        # Avoid log(0) error
        if rms <= 0:
            rms = 1
        # Calculate dB
        db = 20.0 * math.log10(rms)
        # Apply calibration
        final_db = db + DB_OFFSET
        # Print to Serial
        print(f"Raw dB: {db:.2f} | Final dB: {final_db:.2f}")
    time.sleep(0.05)	

