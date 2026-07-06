help:
	@grep '^[^#[:space:]].*:' Makefile

clean:
	@echo "--- Cleaning PISA vectorized ---"
	@rm -fr idx
	@rm -fr build
	@rm -fr CMakeCache.txt
	@rm -fr CMakeFiles


compile-within-container: build/bin/build_index build/bin/do_query
	echo "compiling"

compile:
	docker build -t pisa-vectorized-builder .
	docker run --rm -it -v "$(shell pwd)":/home/pisa/src/ pisa-vectorized-builder

index: idx

# oneTBB is built as a shared library somewhere under the build tree; locate it
# so the binaries can find libtbb.so at runtime.
TBB_LIB := $(shell dirname $(shell find $(shell pwd)/build -name 'libtbb.so*' 2>/dev/null | head -n1) 2>/dev/null)

serve: build/bin/do_query
	@LD_LIBRARY_PATH=$(TBB_LIB) ./build/bin/do_query idx

build/bin/%:
	@echo "--- Building PISA vectorized ---"
	@mkdir -p build
	@cmake -B build/ .
	@make -C build/

idx: build/bin/build_index
	@echo "--- Indexing PISA vectorized ---"
	@mkdir -p idx
	@LD_LIBRARY_PATH=$(TBB_LIB) ./build/bin/build_index < ${CORPUS}
