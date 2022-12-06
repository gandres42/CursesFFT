DEPENDENCIES ---------------------------------------------
The following libraries are required to build:
- FFTW3
- ncurses
- PortAudio

RUNNING --------------------------------------------------
Once all dependencies are satisfied, build using:
    make build
After compiling, run using
    ./bin/main

NOTES ----------------------------------------------------
This version of curses-fft does not verify if your setup
is supported by PortAudio, or if all sample rates are
supported.