#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>

namespace {

// With NMSC_TEST_TRACE set, each test case's name goes to stderr as it starts. A crash that ends the process at once
// (on Windows a fast fail, 0xc0000409) loses the buffered report on stdout, so without this a CI log cannot say which
// test it was in. stderr is unbuffered, and the name is flushed before the test runs.
struct TraceListener : doctest::IReporter {
    const bool on = std::getenv("NMSC_TEST_TRACE") != nullptr;

    explicit TraceListener(const doctest::ContextOptions&) {}

    void test_case_start(const doctest::TestCaseData& tc) override {
        if (!on) return;
        std::fprintf(stderr, "[test] %s (%s:%d)\n", tc.m_name, tc.m_file.c_str(), tc.m_line);
        std::fflush(stderr);
    }
    void report_query(const doctest::QueryData&) override {}
    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats&) override {}
    void test_case_reenter(const doctest::TestCaseData&) override {}
    void test_case_end(const doctest::CurrentTestCaseStats&) override {}
    void test_case_exception(const doctest::TestCaseException&) override {}
    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData&) override {}
    void log_message(const doctest::MessageData&) override {}
    void test_case_skipped(const doctest::TestCaseData&) override {}
};

} // namespace

REGISTER_LISTENER("nmsc_trace", 1, TraceListener);
