CXXFLAGS=-std=c++11 -g -O2 -Wall -DNDEBUG

all: bench-raster-bird simple-test

bench-raster-bird: bench-raster-bird.o rasterizer.o skia-utils.o shader.o
	$(CXX) $(CXXFLAGS) $^   -o $@

simple-test: simple-test.o rasterizer.o skia-utils.o shader.o
	$(CXX) $(CXXFLAGS) $^   -o $@

lut-test: lut-test.o shader.o
	$(CXX) $(CXXFLAGS) $^   -o $@

clean:
	rm *.o bench-raster-bird
