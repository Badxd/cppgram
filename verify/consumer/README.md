# Independent consumer check

A project that uses only the installed CppGram package. It does not use any library source file.

```sh
cmake -S . -B build/consumer -G Ninja -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build build/consumer
./build/consumer/consumer
```

Expected output: `consumer ok: app_version 1.0.0, offline API calls succeeded`. The check makes no network calls.
