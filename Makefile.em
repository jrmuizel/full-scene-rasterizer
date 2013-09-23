CXX=~/src/emscripten/emcc
CXXFLAGS=-std=c++11 -O2 -s TOTAL_MEMORY=67108864 --js-library library-canvas.js 
OBJS=bench-raster-bird.o rasterizer.o skia-utils.o

bird.js: $(OBJS) library-canvas.js
	$(CXX) $(CXXFLAGS) $(OBJS) -s EXPORTED_FUNCTIONS="['_drawFrame','_init']"  -o $@
