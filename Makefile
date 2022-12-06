build:
	g++ src/main.cpp -g -o bin/main -lportaudio -lncurses -lfftw3
run: 
	./bin/main $@
clean:
	rm ./bin/main