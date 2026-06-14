#include "progress.hpp"
#include <cassert>
#include <sstream>
#include <thread>
#include <vector>

int ok = 0, fail = 0;
#define check(cond, msg) if(cond){ok++;}else{std::cerr<<"\nFAIL: "<<msg<<"\n";fail++;}

void test_bar_progress() {
    progress::Bar b(100);
    b.out = &std::cerr;
    b.update(50);
    check(b.current() == 50, "current after update");
    b.set(80);
    check(b.current() == 80, "current after set");
    b.update(100);
    check(b.current() == 100, "clamps to total");
}

void test_bar_zero_total() {
    progress::Bar b(0);
    b.update(1);
    check(b.current() == 0, "zero total clamped");
}

void test_fmt_duration() {
    check(progress::fmt_duration(0) == "00:00", "0 secs");
    check(progress::fmt_duration(65) == "01:05", "65 secs");
    check(progress::fmt_duration(3661) == "1:01:01", "1h1m1s");
    check(progress::fmt_duration(-1) == "--:--", "negative");
}

void test_bar_finish() {
    progress::Bar b(10);
    b.update(5);
    b.finish();
    check(b.current() == 10, "finish sets to total");
}

void test_bar_multithreaded() {
    progress::Bar b(1000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&b] {
            for (int j = 0; j < 250; j++) b.update();
        });
    }
    for (auto& t : threads) t.join();
    b.finish();
    check(b.current() == 1000, "multithreaded updates sum correctly");
}

void test_prefix_settable() {
    progress::Bar b(10);
    b.prefix = "loading";
    b.update(5);
    check(b.current() == 5, "prefix settable post-construction");
}

void test_bar_line_method() {
    progress::Bar b(100, "test");
    b.update(50);
    std::string l = b.line();
    check(l.find("test") != std::string::npos, "line() contains prefix");
    check(l.find("50%") != std::string::npos, "line() contains percent");
}

void test_multibar_basic() {
    progress::MultiBar mb;
    auto& b1 = mb.add(100, "task1");
    auto& b2 = mb.add(200, "task2");
    mb.start();
    b1.update(50);
    b2.update(100);
    check(b1.current() == 50, "multibar b1 at 50");
    check(b2.current() == 100, "multibar b2 at 100");
    mb.stop();
    check(b1.current() == 50, "multibar b1 unchanged after stop");
    check(b2.current() == 100, "multibar b2 unchanged after stop");
}

void test_multibar_multithreaded() {
    progress::MultiBar mb;
    auto& b1 = mb.add(500, "a");
    auto& b2 = mb.add(500, "b");
    mb.start();
    std::thread t1([&b1]{ for(int i=0;i<500;i++) b1.update(); });
    std::thread t2([&b2]{ for(int i=0;i<500;i++) b2.update(); });
    t1.join(); t2.join();
    mb.stop();
    check(b1.current() == 500, "multibar multithreaded b1 correct");
    check(b2.current() == 500, "multibar multithreaded b2 correct");
}

void test_multibar_empty() {
    progress::MultiBar mb;
    mb.start();
    mb.stop();
    check(true, "empty multibar start/stop doesnt crash");
}

int main() {
    test_bar_progress();
    test_bar_zero_total();
    test_fmt_duration();
    test_bar_finish();
    test_bar_multithreaded();
    test_prefix_settable();
    test_bar_line_method();
    test_multibar_basic();
    test_multibar_multithreaded();
    test_multibar_empty();

    std::cerr << "\n" << ok << "/" << (ok + fail) << " tests passed\n";
    return fail == 0 ? 0 : 1;
}
