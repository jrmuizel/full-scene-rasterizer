CXXFLAGS=-std=c++11 -g -O2 -Wall

bench-raster-bird: bench-raster-bird.o rasterizer.o skia-utils.o
	$(CXX) $(CXXFLAGS) $^   -o $@

clean:
	rm *.o bench-raster-bird
