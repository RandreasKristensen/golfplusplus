#pragma once

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace doctest_lite {
struct test_case {
    const char* name;
    void (*func)();
};

inline std::vector<test_case>& registry() {
    static std::vector<test_case> cases;
    return cases;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct registrar {
    registrar(const char* name, void (*func)()) {
        registry().push_back({name, func});
    }
};

inline int run_tests() {
    int failures = 0;
    int failed_tests = 0;
    for (const auto& entry : registry()) {
        const int before = failure_count();
        try {
            entry.func();
        } catch (...) {
            std::cerr << "Unhandled exception in test: " << entry.name << "\n";
            failure_count()++;
        }

        const int after = failure_count();
        if (after > before) {
            failed_tests++;
            std::cerr << "Test failed: " << entry.name << "\n";
        }
    }

    failures = failure_count();
    const int total_tests = static_cast<int>(registry().size());
    const int passed_tests = total_tests - failed_tests;
    std::cout << "Test summary: Total " << total_tests
              << ", Passed " << passed_tests
              << ", Failed " << failed_tests
              << ", Checks failed " << failures << "\n";
    return failures == 0 ? 0 : 1;
}
}

#define DOCTEST_LITE_CONCAT_IMPL(a, b) a##b
#define DOCTEST_LITE_CONCAT(a, b) DOCTEST_LITE_CONCAT_IMPL(a, b)

#define TEST_CASE(name) \
    static void DOCTEST_LITE_CONCAT(test_case_, __LINE__)(); \
    static doctest_lite::registrar DOCTEST_LITE_CONCAT(test_registrar_, __LINE__)(name, &DOCTEST_LITE_CONCAT(test_case_, __LINE__)); \
    static void DOCTEST_LITE_CONCAT(test_case_, __LINE__)()

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            doctest_lite::failure_count()++; \
            std::cerr << "CHECK failed: " << #expr << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        } \
    } while (0)

#ifdef DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
int main() {
    return doctest_lite::run_tests();
}
#endif
