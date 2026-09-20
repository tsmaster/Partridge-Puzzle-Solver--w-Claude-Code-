# Compiler and flags
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

TARGETS = rangesolver

all: $(TARGETS)

%: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGETS)

.PHONY: all clean
