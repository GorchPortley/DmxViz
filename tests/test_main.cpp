#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <iostream>

// Custom main: unbuffered output so that, if a test hangs and CI kills the
// process, the log still shows the last test that finished (run with
// --duration=true to print every test case as it completes).
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << std::unitbuf;
    doctest::Context context(argc, argv);
    return context.run();
}
