# ECE 4760 Final Project: Chord Identifier

For our final project, we created a chord identifier using the Raspberry Pi Pico RP2040 microcontroller that uses a microphone and a Sallen-Key low pass filter to detect the chord played from an instrument and displays the result on a VGA screen and PuTTY serial interface.

First, we implemented a note identifier. We set up the microphone the same way as in Lab 1 and played pure tone frequencies. Using the FFT code provided from Lab 1, we performed an FFT on the audio sample and determined the maximum frequency from the FFT. From the maximum frequency, we used a formula to identify the note name and scale (e.g. A4, D3, etc.). We output the maximum frequency and note name on the serial interface. 

Building upon the note identifier, we used the same microphone and serial interface setup, and rather than looking for the single maximum frequency, we identified the three highest frequencies from the FFT. These frequencies use the note identifier to label the three most powerful notes. Applying the foundations of music theory, we determine which chord is played; we distinguish major, minor, augmented, and diminished chords as well as triads. 

Once the basic chord identification functionality was implemented, the next step was to improve the chord identification by implementing a low pass filter to limit the range of the frequencies picked up by the microphone. This filter allowed us to change the parameters of the sampling for our FFT algorithm, which increased the efficiency and accuracy of our chord identification. Finally, a Protothreads C program was implemented to make use of the two cores on the RP2040. The serial interface and VGA were implemented on one core, and the FFT was implemented on the other.
