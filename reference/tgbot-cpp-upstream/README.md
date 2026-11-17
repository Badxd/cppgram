# Third-party reference: tgbot-cpp upstream examples

This folder is **not part of CppGram's official examples**. It is kept only as reference material for the
upstream tgbot-cpp project (https://github.com/reo7sp/tgbot-cpp, MIT).

- `dockerfiles/`: nine Dockerfiles that start `FROM reo7sp/tgbot-cpp`. They build and run the upstream
  library image, not CppGram.
- `echobot-submodule/`: an example that adds an upstream tgbot-cpp source tree with `add_subdirectory`.

Nothing here is built, tested, syntax-checked or verified by the CppGram release checks. Do not use these files as
CppGram examples. For CppGram examples see `examples/`.

Upstream copyright and MIT terms are in `LICENSE` at the repository root.
