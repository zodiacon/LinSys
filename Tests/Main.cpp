#include <cstdio>
#include <cstring>
#include "TestFramework.h"

namespace {
	int s_Failures = 0;	// in the running test
}

std::vector<Tests::TestCase>& Tests::Registry() {
	static std::vector<TestCase> tests;
	return tests;
}

void Tests::Fail(const char* file, int line, const std::string& message) {
	if (auto slash = strrchr(file, '/'))
		file = slash + 1;
	printf("    %s(%d): %s\n", file, line, message.c_str());
	s_Failures++;
}

// Tests [text | -text]: the tests whose names contain the text, or (with '-') all but those ("-Live" leaves out those
// reading the live system); the exit code is the number that failed
int main(int argc, char** argv) {
	auto filter = argc > 1 ? std::string(argv[1]) : std::string();
	bool exclude = !filter.empty() && filter[0] == '-';
	if (exclude)
		filter.erase(0, 1);
	int run = 0, failed = 0;
	for (auto& test : Tests::Registry()) {
		bool contains = std::string(test.Name).find(filter) != std::string::npos;
		if (!filter.empty() && contains == exclude)
			continue;
		s_Failures = 0;
		test.Function();
		run++;
		if (s_Failures) {
			failed++;
			printf("FAIL %s\n", test.Name);
		}
		else
			printf("pass %s\n", test.Name);
	}
	printf("\n%d tests, %d failed\n", run, failed);
	return failed;
}
