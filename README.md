<div align="center">

# cpprogress

progress bars and spinners for C++17 terminal apps

![CI](https://github.com/ligumas/cpprogress/actions/workflows/ci.yml/badge.svg)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square)
![header-only](https://img.shields.io/badge/header--only-yes-brightgreen?style=flat-square)
![license](https://img.shields.io/badge/license-MIT-blue?style=flat-square)

</div>

drop `progress.hpp` into your project.

```cpp
#include "progress.hpp"

// basic bar
progress::Bar bar(total, "training");
for (auto& batch : batches) {
    train(batch);
    bar.update();
}
bar.finish();
```

output:

```
training [===================>        ]  72% 1243.5/s eta 00:03
```

**live suffix stats:**

update the suffix mid-loop to show per-step metrics (thread-safe):

```cpp
progress::Bar bar(epochs, "training");
for (int e = 0; e < epochs; e++) {
    float loss = train_epoch(e);
    bar.set_suffix("loss=" + std::to_string(loss));
    bar.update();
}
bar.finish();
```

output:

```
training [===================>        ]  72% 1243.5/s eta 00:03 loss=0.234
```

spinner for when you don't know the total:

```cpp
progress::Spinner s("loading dataset...");
s.start();
load_data();
s.stop("done");
```

**multiple bars at once:**

```cpp
progress::MultiBar mb;
auto& download = mb.add(1000, "download");
auto& extract  = mb.add(500,  "extract");
mb.start();

std::thread t1([&]{ for (int i=0; i<1000; i++) { do_work(); download.update(); } });
std::thread t2([&]{ for (int i=0; i<500;  i++) { do_work(); extract.update();  } });
t1.join(); t2.join();

mb.stop();
```

output:

```
download [===================>        ]  72% 1243.5/s eta 00:03
extract  [===========>                ]  44%  980.1/s eta 00:02
```

**range-based iteration:**

wrap any container with `progress::Range` to track progress in a range-for loop:

```cpp
std::vector<Image> images = load_images();
for (auto& img : progress::Range(images, "processing")) {
    process(img);
}
```

for numeric loops use `progress::irange`:

```cpp
for (size_t i : progress::irange(1000, "training")) {
    train_step(i);
}
```

**colored bars:**

```cpp
#include "progress.hpp"

progress::Bar bar(total, "training");
bar.bar_color = progress::color::green;   // green fill
bar.update(50);
bar.finish();

// available: color::green, yellow, red, blue, cyan, magenta
```

**Bar options:**

```cpp
bar.width = 40;        // bar width in chars
bar.show_eta = true;
bar.show_rate = true;
bar.bar_color = progress::color::cyan;
bar.min_interval_ms = 50; // re-render at most every N ms
bar.set_suffix("key=val"); // live suffix text, thread-safe
```

**License:** MIT
