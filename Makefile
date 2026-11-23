# Foundry: a CLAP drum machine for the Akai MPC Sample (64-bit ARM Linux, Cortex-A55).
# Build inside a Linux arm64 image with ./build.sh, which runs `make all test`.
#   make            build/Foundry.clap
#   make test       the tests against the built plug-in (tests/host.cpp) and the sound's fingerprint
#                   (tests/fingerprint.cpp: the output to the bit, against tests/fingerprint.txt)
#   make bench      build/bench: renders busy patterns; tools/bench.sh counts its instructions
#   make sanitize   the tests and fingerprint scenarios under AddressSanitizer and UBSan (build/asan)
CXX ?= g++
OUT := build
.DEFAULT_GOAL := all
WARN := -Wall -Wextra -Werror
# The MPC's cores. -ffp-contract=fast lets the compiler fuse multiply-adds; nothing here relies on IEEE errno,
# traps or exceptions.
OPT := -O3 -mcpu=cortex-a55 -ffp-contract=fast -fno-math-errno -fno-trapping-math
CXXFLAGS := -std=c++17 $(OPT) $(WARN) -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -Ithird_party/clap/include -Isrc -I$(OUT)
# Only clap_entry is exported; libstdc++ and libgcc are linked in so the MPC needs nothing beyond libc and libm. -s
# leaves out the symbol table (the code is the same; the exported clap_entry stays).
LDFLAGS := -shared -s -static-libgcc -static-libstdc++ -Wl,--gc-sections -Wl,-z,now -Wl,-O1 -Wl,--version-script=src/exports.map

SOURCES := src/foundry.cpp
HEADERS := $(wildcard src/*.h src/ui/*.h) $(OUT)/screen-art.h

$(OUT)/bake-screen: tools/bake-screen.cpp
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -O2 $(WARN) $< -lpng -o $@

$(OUT)/screen-art.h: assets/foundry-panel.png $(OUT)/bake-screen
	$(OUT)/bake-screen $< > $@.tmp && mv $@.tmp $@

.PHONY: all test bench clean fingerprint-update sanitize
all: $(OUT)/Foundry.clap

$(OUT)/Foundry.clap: $(SOURCES) $(HEADERS) src/exports.map Makefile
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(OUT)/host: tests/host.cpp tests/wav.h src/melodee_screen.h src/fx.h src/dsp.h src/params.h Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -O2 $(WARN) -Ithird_party/clap/include tests/host.cpp -ldl -o $@

$(OUT)/fingerprint: tests/fingerprint.cpp tests/fingerprint_scenarios.h Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -O2 $(WARN) -Ithird_party/clap/include tests/fingerprint.cpp -ldl -o $@

test: $(OUT)/Foundry.clap $(OUT)/host $(OUT)/fingerprint
	$(OUT)/host $(OUT)/Foundry.clap $(OUT)
	$(OUT)/fingerprint $(OUT)/Foundry.clap tests/fingerprint.txt > /dev/null

# The sound's hashes after a change meant to change the sound (see tests/fingerprint.cpp).
fingerprint-update: $(OUT)/Foundry.clap $(OUT)/fingerprint
	$(OUT)/fingerprint $(OUT)/Foundry.clap > tests/fingerprint.txt

$(OUT)/bench: tests/bench.cpp $(SOURCES) $(HEADERS) Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 $(OPT) $(WARN) -g -Ithird_party/clap/include -Isrc -I$(OUT) tests/bench.cpp -o $@

bench: $(OUT)/bench

# The tests and every fingerprint scenario under AddressSanitizer and UBSan (build/asan): any read past a buffer,
# leak or undefined behaviour stops it. The fingerprints' hashes hold only for the release flags, so a mismatch
# there (exit 1) is expected; a sanitizer's report aborts instead (abort_on_error for both, so UBSan's is not an exit 1).
SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g1
sanitize: $(OUT)/screen-art.h
	@mkdir -p $(OUT)/asan
	$(CXX) $(subst -O3,-O1,$(CXXFLAGS)) $(SAN) $(SOURCES) -shared -Wl,--version-script=src/exports.map -o $(OUT)/asan/Foundry.clap
	$(CXX) -std=c++17 -O1 $(SAN) -Ithird_party/clap/include tests/host.cpp -ldl -o $(OUT)/asan/host
	$(CXX) -std=c++17 -O1 $(SAN) -Ithird_party/clap/include tests/fingerprint.cpp -ldl -o $(OUT)/asan/fingerprint
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 $(OUT)/asan/host $(OUT)/asan/Foundry.clap $(OUT)/asan
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 \
	    $(OUT)/asan/fingerprint $(OUT)/asan/Foundry.clap tests/fingerprint.txt > $(OUT)/asan/fingerprint.log 2>&1; \
	    rc=$$?; if [ $$rc -gt 1 ]; then cat $(OUT)/asan/fingerprint.log; exit $$rc; fi; echo "sanitize: every fingerprint scenario ran clean"

clean:
	rm -rf $(OUT)
