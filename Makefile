.PHONY: clean_build build

build:
	cmake --build build

clean_build:
	rm -rf build
	mkdir build
	cmake -B build -G Ninja
	cmake --build build

