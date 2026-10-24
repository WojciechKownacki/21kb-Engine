# Game script logging

Game hosts register the built-in `Log(message)` function before the script function
registry is locked. Lua scripts and compiled graphs use the same registration.

Desktop and Web hosts write UTF-8 text to stdout with the prefix
`kb_game: script: `, a trailing newline and an immediate flush. The generated Web
shell displays stdout in its output area. Android writes at info level to the
system log with the tag `21kb` and the same prefix. No reference to a temporary
startup diagnostics stream is retained by the function callback.

Build `kb_cli`, `kb_cooker` and `kb_game`, then run:

```sh
ctest --test-dir build -R '^kb_game_touch_script_package$' --output-on-failure
```

This Windows test authors and cooks a real Lua script, runs the packaged player
without a visible window, requires a rendering backend, and checks ASCII and
UTF-8 log output along with three rendered frames. A separately cooked failing
script must report its assertion and return a failed exit code. Sanitized MSVC
test packages also stage the configured AddressSanitizer runtime DLL.

Release and AddressSanitizer Windows package runs and a production WebGL package
in headless Chrome have exercised script output. Android host compilation and
static analysis are checked; delivery through an Android device system log is
still unverified. This logging change does not establish script fault reporting
in the shared Web/Linux runtime or Android update loop; those paths require
separate lifecycle tests.
