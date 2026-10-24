# Runtime provider staging

Shared particle provider libraries are staged beside the game, CLI and editor.
The copy targets still run when the provider changes without relinking a host.
Their build dependencies serialize these copies so consumers sharing an output
directory cannot overwrite the same library concurrently. Each consumer retains
its own destination, including separate output directories. Static provider
builds do not add these library copy targets.

With CMake 3.27+ and Ninja on `PATH`, run the regression tests with:

```sh
python -m unittest discover -s scripts/tests -p test_runtime_staging.py -v
```

The tests configure real Ninja builds and run three copy actions in parallel.
They check serialized access, shared and separate destinations, binary contents,
and repeated registration without dependency cycles. A negative control removes
serialization and must detect overlapping access.

Parallel Windows Release and AddressSanitizer game/CLI builds and the editor,
game and CLI staging targets have been checked with matching source/staged
library hashes. Web and Android static builds are checked separately. Other
build generators and platforms still require their own execution evidence.
