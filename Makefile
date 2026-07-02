.PHONY: clean_build build

build:
	cmake --build build

clean_build:
	rm -rf build
	mkdir build
	cmake -B build -G Ninja
	cmake --build build

clean_build_debug:
	rm -rf build
	mkdir build
	cmake -DCMAKE_BUILD_TYPE=debug -B build -G Ninja
	cmake --build build

