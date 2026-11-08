CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror
BUILD := build

.PHONY: test clean

test: $(BUILD)/core_tests
	./$(BUILD)/core_tests

$(BUILD)/core_tests: tests/core_tests.cpp $(wildcard include/dihedra/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Iinclude $< -o $@

clean:
	rm -rf $(BUILD)
