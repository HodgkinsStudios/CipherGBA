.PHONY: all clean run
all:
	./build.sh
clean:
	@if [ -d build ]; then cmake --build build --target clean; fi
run:
	./run.sh
