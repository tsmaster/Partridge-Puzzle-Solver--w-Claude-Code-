# Compiler and flags
# -march=native tunes for the machine doing the build; since each machine builds its own binary
# locally (never copy this binary to a different machine), that's safe. LTO isn't included since
# there's only one translation unit here, so it has nothing to optimize across.
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native

TARGETS = rangesolver

all: $(TARGETS)

%: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGETS)

.PHONY: all clean
