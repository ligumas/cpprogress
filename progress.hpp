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

namespace progress {

using clock_t = std::chrono::steady_clock;
using tp_t    = std::chrono::time_point<clock_t>;

inline std::string fmt_duration(double secs) {
    if (secs < 0 || std::isinf(secs)) return "--:--";
    int s = (int)secs;
    int m = s / 60; s %= 60;
    int h = m / 60; m %= 60;
    char buf[16];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else       std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

class MultiBar;

class Bar {
public:
    size_t total;
    int width       = 40;
    char fill       = '=';
    char head       = '>';
    char empty      = ' ';
    bool show_eta   = true;
    bool show_rate  = true;
    std::string prefix;
    std::ostream* out = &std::cerr;

    explicit Bar(size_t total, std::string label = "")
        : total(total), prefix(std::move(label)), n_(0), start_(clock_t::now()) {}

    void update(size_t n = 1) {
        std::lock_guard<std::mutex> lk(mtx_);
        n_ += n;
        if (n_ > total) n_ = total;
        if (!managed_) render();
    }

    void set(size_t n) {
        std::lock_guard<std::mutex> lk(mtx_);
        n_ = n > total ? total : n;
        if (!managed_) render();
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

    // Returns the rendered bar line (thread-safe, used by MultiBar)
    std::string line() {
        std::lock_guard<std::mutex> lk(mtx_);
        return build_line();
    }

    size_t current() const { return n_; }

private:
    friend class MultiBar;
    size_t n_;
    tp_t start_;
    mutable std::mutex mtx_;
    bool managed_  = false;
    bool finished_ = false;

    std::string build_line() {
        double pct = total > 0 ? (double)n_ / total : 0.0;
        int filled = (int)(pct * width);
        if (filled > width) filled = width;

        std::ostringstream ss;
        if (!prefix.empty()) ss << prefix << " ";
        ss << "[";
        for (int i = 0; i < filled - 1; i++) ss << fill;
        if (filled > 0 && n_ < total) ss << head;
        else if (filled > 0) ss << fill;
        for (int i = filled; i < width; i++) ss << empty;
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

        if (show_eta && n_ > 0 && n_ < total) {
            double rate = n_ / elapsed;
            double eta  = rate > 0 ? (total - n_) / rate : -1.0;
            ss << " eta " << fmt_duration(eta);
        }

        return ss.str();
    }

    void render() {
        *out << "\r" << build_line() << "   ";
        out->flush();
    }
};

// Display multiple bars simultaneously, each on its own line.
// Add all bars before calling start().
class MultiBar {
public:
    // Returns a reference valid for this MultiBar's lifetime.
    Bar& add(size_t total, std::string label = "") {
        bars_.push_back(std::make_unique<Bar>(total, std::move(label)));
        bars_.back()->managed_ = true;
        return *bars_.back();
    }

    void start() {
        for (size_t i = 0; i < bars_.size(); i++)
            std::cerr << "\n";
        running_ = true;
        t_ = std::thread([this] { loop(); });
    }

    void stop() {
        running_ = false;
        if (t_.joinable()) t_.join();
        redraw();
    }

    ~MultiBar() {
        if (running_) stop();
    }

private:
    std::vector<std::unique_ptr<Bar>> bars_;
    std::atomic<bool> running_{false};
    std::thread t_;

    void loop() {
        while (running_) {
            redraw();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void redraw() {
        int n = (int)bars_.size();
        if (n == 0) return;
        std::ostringstream out;
        out << "\033[" << n << "A";
        for (int i = 0; i < n; i++)
            out << "\r\033[K" << bars_[i]->line() << "\n";
        std::cerr << out.str();
        std::cerr.flush();
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

    void loop() {
        int i = 0;
        while (running_) {
            std::cerr << "\r" << frames_[i % 10];
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
        : count_(count), bar_(count, std::move(label)) {}

    ~IRange() { bar_.finish(); }

    struct Iter {
        size_t i;
        Bar& bar;
        size_t operator*() const { return i; }
        Iter& operator++() { ++i; bar.update(); return *this; }
        bool operator!=(const Iter& o) const { return i != o.i; }
    };

    Iter begin() { return {0, bar_}; }
    Iter end()   { return {count_, bar_}; }

private:
    size_t count_;
    Bar bar_;
};

inline IRange irange(size_t n, std::string label = "") {
    return IRange(n, std::move(label));
}

} // namespace progress

