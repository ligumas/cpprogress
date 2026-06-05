<div align="center">

# cpprogress

progress bars and spinners for C++17 terminal apps

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

spinner for when you don't know the total:

```cpp
progress::Spinner s("loading dataset...");
s.start();
load_data();
s.stop("done");
```

**Bar options:**

```cpp
bar.width = 40;        // bar width in chars
bar.show_eta = true;
bar.show_rate = true;
```

**License:** MIT
