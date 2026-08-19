CXX_LINUX = clang++
CC_LINUX = clang

CXX_WIN = x86_64-w64-mingw32-g++
CC_WIN = x86_64-w64-mingw32-gcc

CXX_MAC = x86_64-apple-darwin20.4-clang++
CC_MAC = x86_64-apple-darwin20.4-clang

INCLUDES = -Iinclude
LIB_DIR = -Llib

FLAGS_LINUX = $(LIB_DIR) -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
FLAGS_WIN = $(LIB_DIR) -static -lraylib -lopengl32 -lgdi32 -lwinmm -lcomdlg32 -lole32 -lws2_32
FLAGS_MAC = $(LIB_DIR) -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo

RPATH_LINUX = -Wl,-rpath,'$$ORIGIN'
RPATH_MAC = -Wl,-rpath,'@executable_path'

all: linux windows mac

linux:
	mkdir -p build/linux/assets
	$(CC_LINUX) -c source/tinyfiledialogs.c -o build/linux/tinyfiledialogs.o $(INCLUDES)
	$(CXX_LINUX) source/main.cpp build/linux/tinyfiledialogs.o lib/libdiscord_partner_sdk.so $(RPATH_LINUX) -o build/linux/BrickNet $(INCLUDES) $(FLAGS_LINUX)
	cp -r assets/* build/linux/assets/
	cp lib/libdiscord_partner_sdk.so build/linux/
	chmod +x build/linux/BrickNet

renderer:
	mkdir -p build/renderer/assets
	$(CC_LINUX) -c source/tinyfiledialogs.c -o build/renderer/tinyfiledialogs.o $(INCLUDES)
	$(CXX_LINUX) source/renderer.cpp build/renderer/tinyfiledialogs.o lib/libdiscord_partner_sdk.so $(RPATH_LINUX) -o build/renderer/BrickNetRendering $(INCLUDES) $(FLAGS_LINUX)
	cp -r assets/* build/renderer/assets/
	cp lib/libdiscord_partner_sdk.so build/renderer/
	chmod +x build/renderer/BrickNetRendering

windows:
	mkdir -p build/windows/assets
	$(CC_WIN) -c source/tinyfiledialogs.c -o build/windows/tinyfiledialogs.o $(INCLUDES)
	$(CXX_WIN) source/main.cpp build/windows/tinyfiledialogs.o lib/discord_partner_sdk.dll -o build/windows/BrickNet.exe $(INCLUDES) $(FLAGS_WIN)
	cp -r assets/* build/windows/assets/
	cp lib/*.dll build/windows/ 2>/dev/null || true

mac:
	mkdir -p build/mac/assets
	$(CC_MAC) -c source/tinyfiledialogs.c -o build/mac/tinyfiledialogs.o $(INCLUDES)
	$(CXX_MAC) source/main.cpp build/mac/tinyfiledialogs.o lib/libdiscord_partner_sdk.dylib $(RPATH_MAC) -o build/mac/BrickNet $(INCLUDES) $(FLAGS_MAC)
	cp -r assets/* build/mac/assets/
	cp lib/*.dylib build/mac/ 2>/dev/null || true
	chmod +x build/mac/BrickNet

clean:
	rm -rf build