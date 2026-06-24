#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <cmath>
#include <sstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <cstdlib>

#ifdef _WIN32
  #include <io.h>
  #define PROGRESS_ISATTY_STDERR (_isatty(2) != 0)
  #define PROGRESS_ISATTY_STDOUT (_isatty(1) != 0)
#else
  #include <unistd.h>
  #define PROGRESS_ISATTY_STDERR (isatty(STDERR_FILENO) != 0)
  #define PROGRESS_ISATTY_STDOUT (isatty(STDOUT_FILENO) != 0)
#endif

namespace progress {

using clock_t = std::chrono::steady_clock;
using tp_t    = std::chrono::time_point<clock_t>;

namespace color {
    constexpr const char* green   = "\033[32m";
    constexpr const char* yellow  = "\033[33m";
    constexpr const char* red     = "\033[31m";
    constexpr const char* blue    = "\033[34m";
    constexpr const char* cyan    = "\033[36m";
    constexpr const char* magenta = "\033[35m";
    constexpr const char* reset   = "\033[0m";
}

inline std::string fmt_duration(double secs) {
    if (secs < 0 || !std::isfinite(secs)) return "--:--";
    int s = (int)secs;
    int m = s / 60; s %= 60;
    int h = m / 60; m %= 60;
    char buf[16];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else       std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

inline bool ansi_ok(std::ostream* s) {
    if (std::getenv("NO_COLOR") != nullptr) return false;
    if (s == &std::cerr) return PROGRESS_ISATTY_STDERR;
    if (s == &std::cout) return PROGRESS_ISATTY_STDOUT;
    return false;
}

class MultiBar;

class Bar {
public:
    size_t total;
    int width          = 40;
    int min_interval_ms = 50;
    char fill          = '=';
    char head          = '>';
    char empty         = ' ';
    bool show_eta      = true;
    bool show_rate     = true;
    std::string prefix;
    std::string bar_color;
    std::ostream* out  = &std::cerr;

    explicit Bar(size_t total, std::string label = "")
        : total(total), prefix(std::move(label)), n_(0),
          start_(clock_t::now()), last_render_(tp_t{}) {}

    void update(size_t n = 1) {
        std::lock_guard<std::mutex> lk(mtx_);
        n_ += n;
        if (n_ > total) n_ = total;
        if (!managed_) throttled_render();
    }

    void set(size_t n) {
        std::lock_guard<std::mutex> lk(mtx_);
        n_ = n > total ? total : n;
        if (!managed_) throttled_render();
    }

    void finish() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (finished_) return;
        finished_ = true;
        n_ = total;
        if (!managed_) {
            render();
            *out << "\n";
            out->flush();
        }
    }

    void set_suffix(std::string s) {
        std::lock_guard<std::mutex> lk(mtx_);
        suffix_ = std::move(s);
    }

    std::string line() {
        std::lock_guard<std::mutex> lk(mtx_);
        return build_line(true);
    }

    size_t current() const { return n_; }

private:
    friend class MultiBar;
    size_t n_;
    tp_t start_;
    tp_t last_render_;
    mutable std::mutex mtx_;
    bool managed_  = false;
    bool finished_ = false;
    std::string suffix_;

    void throttled_render() {
        auto now = clock_t::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_render_).count();
        if (ms >= min_interval_ms) {
            render();
            last_render_ = now;
        }
    }

    std::string build_line(bool force_ansi = false) {
        bool use_ansi = force_ansi || ansi_ok(out);
        double pct = total > 0 ? (double)n_ / total : 0.0;
        int filled = (int)(pct * width);
        if (filled > width) filled = width;

        std::ostringstream ss;
        if (!prefix.empty()) ss << prefix << " ";
        ss << "[";
        if (!bar_color.empty() && use_ansi) ss << bar_color;
        for (int i = 0; i < filled - 1; i++) ss << fill;
        if (filled > 0 && n_ < total) ss << head;
        else if (filled > 0) ss << fill;
        for (int i = filled; i < width; i++) ss << empty;
        if (!bar_color.empty() && use_ansi) ss << color::reset;
        ss << "] ";

        char pct_buf[8];
        std::snprintf(pct_buf, sizeof(pct_buf), "%3.0f%%", pct * 100.0);
        ss << pct_buf;

        double elapsed = std::chrono::duration<double>(clock_t::now() - start_).count();

        if (show_rate && elapsed > 0.1) {
            double rate = n_ / elapsed;
            char rbuf[24];
            if (rate >= 1.0) std::snprintf(rbuf, sizeof(rbuf), " %.1f/s", rate);
            else             std::snprintf(rbuf, sizeof(rbuf), " %.2fs/it", 1.0 / rate);
            ss << rbuf;
        }

        if (show_eta && n_ > 0 && n_ < total && elapsed > 0.1) {
            double rate = n_ / elapsed;
            double eta  = rate > 0 ? (total - n_) / rate : -1.0;
            ss << " eta " << fmt_duration(eta);
        }

        if (!suffix_.empty()) ss << " " << suffix_;

        return ss.str();
    }

    void render() {
        if (ansi_ok(out)) {
            *out << "\r" << build_line(true) << "   ";
        } else {
            *out << build_line(false) << "\n";
        }
        out->flush();
    }
};

// Display multiple bars simultaneously, each on its own line.
// Add all bars before calling start().
class MultiBar {
public:
    Bar& add(size_t total, std::string label = "") {
        bars_.push_back(std::make_unique<Bar>(total, std::move(label)));
        bars_.back()->managed_ = true;
        return *bars_.back();
    }

    void start() {
        ansi_ = ansi_ok(&std::cerr);
        if (ansi_) {
            for (size_t i = 0; i < bars_.size(); i++)
                std::cerr << "\n";
            running_ = true;
            t_ = std::thread([this] { loop(); });
        }
    }

    void stop() {
        if (running_) {
            running_ = false;
            if (t_.joinable()) t_.join();
        }
        if (!done_) {
            done_ = true;
            redraw();
        }
    }

    ~MultiBar() {
        if (running_) stop();
        else if (!done_ && !bars_.empty()) {
            done_ = true;
            redraw();
        }
    }

private:
    std::vector<std::unique_ptr<Bar>> bars_;
    std::atomic<bool> running_{false};
    std::thread t_;
    bool ansi_ = false;
    bool done_ = false;

    void loop() {
        while (running_) {
            redraw();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void redraw() {
        int n = (int)bars_.size();
        if (n == 0) return;
        if (ansi_) {
            std::ostringstream out;
            out << "\033[" << n << "A";
            for (int i = 0; i < n; i++)
                out << "\r\033[K" << bars_[i]->line() << "\n";
            std::cerr << out.str();
            std::cerr.flush();
        } else {
            for (int i = 0; i < n; i++)
                std::cerr << bars_[i]->line() << "\n";
            std::cerr.flush();
        }
    }
};

class Spinner {
public:
    std::string prefix;
    int interval_ms = 80;

    explicit Spinner(std::string msg = "")
        : prefix(std::move(msg)), running_(false) {}

    ~Spinner() { if (running_) stop(); }

    void start() {
        running_ = true;
        t_ = std::thread([this] { loop(); });
    }

    void stop(const std::string& final_msg = "") {
        running_ = false;
        if (t_.joinable()) t_.join();
        std::cerr << "\r";
        if (!final_msg.empty()) std::cerr << final_msg << "\n";
        else                    std::cerr << std::string(60, ' ') << "\r";
        std::cerr.flush();
    }

private:
    std::atomic<bool> running_;
    std::thread t_;
    static constexpr const char* frames_[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
    static constexpr const char* ascii_frames_[] = {"|","/","-","\\"};

    void loop() {
        bool use_ansi = ansi_ok(&std::cerr);
        int i = 0;
        while (running_) {
            if (use_ansi) {
                std::cerr << "\r" << frames_[i % 10];
            } else {
                std::cerr << "\r" << ascii_frames_[i % 4];
            }
            if (!prefix.empty()) std::cerr << " " << prefix;
            std::cerr << "   ";
            std::cerr.flush();
            i++;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    }
};

template<typename Container>
class Range {
public:
    Range(Container& c, std::string label = "")
        : c_(c), bar_(c.size(), std::move(label)) {}

    ~Range() { bar_.finish(); }

    struct Iter {
        typename Container::iterator it;
        Bar& bar;
        Iter& operator++() { ++it; bar.update(); return *this; }
        bool operator!=(const Iter& o) const { return it != o.it; }
        auto& operator*() { return *it; }
    };

    Iter begin() { return {c_.begin(), bar_}; }
    Iter end()   { return {c_.end(), bar_}; }

private:
    Container& c_;
    Bar bar_;
};

class IRange {
public:
    IRange(size_t count, std::string label = "")
        : from_(0), count_(count), bar_(count, std::move(label)) {}

    IRange(size_t from, size_t to, std::string label = "")
        : from_(from), count_(to > from ? to - from : 0),
          bar_(to > from ? to - from : 0, std::move(label)) {}

    ~IRange() { bar_.finish(); }

    struct Iter {
        size_t i;
        size_t from;
        Bar& bar;
        size_t operator*() const { return from + i; }
        Iter& operator++() { ++i; bar.update(); return *this; }
        bool operator!=(const Iter& o) const { return i != o.i; }
    };

    Iter begin() { return {0, from_, bar_}; }
    Iter end()   { return {count_, from_, bar_}; }

private:
    size_t from_;
    size_t count_;
    Bar bar_;
};

inline IRange irange(size_t n, std::string label = "") {
    return IRange(n, std::move(label));
}

inline IRange irange(size_t from, size_t to, std::string label = "") {
    return IRange(from, to, std::move(label));
}

} // namespace progress
 // those who know phonk aura 
