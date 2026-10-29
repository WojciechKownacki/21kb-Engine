# Android app bundles

Android exports support APK and AAB formats. APK remains the default. Select
`aab` in the Android package format field and provide the path to an official
bundletool JAR in the Build Game panel. The equivalent CLI options are
`--android-format aab --android-bundletool <bundletool.jar>`.

The exporter supports the `Android.ASTC.arm64` and `Android.ETC2.arm64` targets.
It builds an install-time `runtimeassets` asset pack containing `game.kbpack`.
The base module contains the native host, trust anchor and third-party notices.
The runtime accesses the installed asset pack through Android's asset manager;
this path does not implement on-demand downloads or download progress UI.

## Signing and validation

Release exports require an Android keystore and key alias. The Windows editor's
native signing broker passes passwords to the JDK through standard input. It
signs the AAB using the JDK JAR signing tool and verifies it against the selected
key's public certificate. A public-only temporary trust store permits strict
verification without passing the private keystore password on the command line.
APK signing retains its separate APK signer protocol.

CLI callers can provide `--android-signing-broker <executable>`. The executable
receives `--request <json> --response <json>`. Without that option, the exporter
emits `SIGNING_REQUEST|<request>|<response>` for a supervising process, such as
the editor, to service. An unattended Release invocation needs one of these
signing integrations. The schema 2 AAB request carries the session, Java path,
keystore, alias, format and input/output package paths; it contains no passwords.
Success requires a schema 1 response with the same session and `succeeded: true`.

Before publication to the output directory, the exporter runs bundletool
validation and checks the application identity, version, SDK levels,
debuggability, ARM64 host, texture profile and exact payload hashes. The asset
module must declare unconditional install-time delivery.

## Local installation

Set `ANDROID_SERIAL` to the intended connected device and request `--launch`.
The exporter uses bundletool to build a device-specific local-testing APK set,
installs those splits, and waits for the runtime's matching-profile first-frame
message. An AAB is not directly installable with `adb install`.

Local-testing APKs use bundletool's debug signing. This is independent of the
upload signature on a Release AAB; it does not verify store signing or store
distribution. The exporter does not upload packages to a store.

## Verification tools

`python -m unittest discover -s scripts/tests -p test_android_bundle.py -v`
checks payload validation, signing request identity, split-install orchestration
and rejection of installer failures. Installer failure injection is not a
physical device storage-exhaustion test.

After building `kb_editor_build_game_tests`, run
`sources/editor/tests/AndroidBundleSigningTests.py --probe <test-executable>
--java <java-executable> --output <test-output-directory> --bundle <bundle.aab>`
to exercise the native broker with a disposable key and strict verification.
When supplied an existing bundle, the test preserves its application entries
and removes existing JAR signing metadata in a temporary copy, matching the
unsigned input produced by a Release build.
The test also checks unsigned-entry rejection, incorrect credentials and
preservation of preexisting output files. This test does not install the bundle.
