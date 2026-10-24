# Android controller tests

This APK runs the production Android controller adapter with GameActivity 4.4.2
and Android Games Controller 2.0.2. It uses a transparent, nonfocusable activity
with preview disabled. The CLI registers two OS virtual controllers with `uinput`
and sends Java `MotionEvent` and `KeyEvent` objects through GameActivity's surface
and activity handlers. Native assertions inspect the resulting `InputDeviceState`.
No controller events are injected into other applications.

Build with JDK 21, Android SDK 36, NDK 29.0.14206865 and CMake 4.1.2 installed.
Set `ANDROID_HOME` to the SDK directory, then run from the repository root:

```sh
java -classpath platform/android/gradle/wrapper/gradle-wrapper.jar org.gradle.wrapper.GradleWrapperMain -p sources/game/tests/android --no-daemon assembleDebug
python sources/game/tests/android/run_controller_tests.py --adb adb --serial <authorized-host> --apk sources/game/tests/android/build/outputs/apk/debug/21kb-Controller-Tests-debug.apk --output build/android-controller-results
```

The host must provide the standard shell `uinput` tool and allow test APK
installation. A headless Android emulator can be started with `-no-window` and
`-no-audio`. Missing tools, failed registration, rejected events, timeouts and
failed assertions are failures, never skipped passes.

The test checks independent sticks, triggers, buttons and hats, hardware flat
regions, finite/range conversion, focus suppression, disconnection with held
inputs, slot isolation, reconnection with a new device ID, and native shutdown.
The engine exposes four controller slots. Extra library slots do not alias them.
Android axes pointing down are converted to the engine's positive-up convention.

An emulator run verifies the Android SDK/JNI/native event path with OS virtual
devices. Physical controller mappings, wireless transport and a fully packaged
game's lifecycle require their own device tests.
