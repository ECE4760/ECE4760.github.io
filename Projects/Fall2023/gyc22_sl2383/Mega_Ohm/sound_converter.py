'''
Giacomo Yong Cuomo (gyc22)

This file functions as a converter from WAV files to C-arrays.
Requires WAV files of 16bit at 44.1 kHz (either stereo or mono).
Outputs a txt file with the schematic of a C-array in the range of [0,4095],
used in the RP2040.
'''

from ctypes import sizeof
from unicodedata import decimal
import wave
import numpy

# Read file to get buffer                                                                                               
ifile = wave.open("SoundOhms.wav")
samples = ifile.getnframes()
audio = ifile.readframes(samples)

# Convert buffer to float32 using NumPy                                                                                 
audio_as_np_int16 = numpy.frombuffer(audio, dtype=numpy.int16)
audio_as_np_float32 = audio_as_np_int16.astype(numpy.float32)

# Normalise float32 array so that values are between -1.0 and +1.0                                                      
max_int16 = 2**15
audio_normalised = audio_as_np_float32 / max_int16

minimum = min(audio_as_np_float32)
maximum = max(audio_as_np_float32)
print(str(minimum) + " and " + str(maximum))

f = open("sound_file.txt", "w")
f.write("const uint16_t Sound_Ohms[] = {\r")
for i in audio_as_np_float32:
    # convert from 0 to 4095 ints
    i = ((i-minimum)/(maximum-minimum))*4095
    # append configuration bits
    i = 0b0011000000000000 + i
    # print each finalised i
    f.write(str(numpy.int16(i)) + ", ")
f.write("};")
print(len(audio_as_np_float32))
