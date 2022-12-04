build:
	g++ src/main.cpp -g -o bin/main -I lib/SlidingDFT -lportaudio -lncurses -lfftw3
run: 
	./bin/main $@