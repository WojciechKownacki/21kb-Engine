#!/usr/bin/env python3
"""Exercise the native Android signing broker with a temporary test key."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import tempfile
import zipfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--java", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bundle", type=Path)
    args = parser.parse_args()
    probe, java = args.probe.resolve(strict=True), args.java.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, JAVA_HOME=str(java.parent.parent))
    results = []

    def run(command, password_input=None):
        return subprocess.run([str(value) for value in command], input=password_input,
            capture_output=True, text=True, encoding="utf-8", errors="replace", env=environment,
            timeout=240, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def record(name, condition, result):
        results.append({"name": name, "passed": bool(condition), "exitCode": result.returncode})
        if not condition:
            raise RuntimeError(f"{name} failed: {result.stdout}\n{result.stderr}")

    try:
        with tempfile.TemporaryDirectory(prefix="BundleSigning", dir=output) as directory:
            root = Path(directory).resolve()
            root.relative_to(output)
            key = root / "test.p12"
            password = secrets.token_hex(24)
            generated = run([java, "--module", "java.base/sun.security.tools.keytool.Main",
                "-genkeypair", "-keystore", key, "-storetype", "PKCS12", "-keyalg", "RSA",
                "-keysize", "2048", "-validity", "3650", "-alias", "upload",
                "-dname", "CN=Engine Bundle Test"], password + "\n" + password + "\n")
            record("temporary key", generated.returncode == 0, generated)
            jobs = root / "jobs"
            jobs.mkdir()

            def sign(name, *, alias="upload", kind="aab", supplied_password=password, existing=False):
                job = jobs / name
                job.mkdir()
                source, signed = job / "unsigned.aab", job / "signed.aab"
                if args.bundle:
                    # A Development bundle already carries Gradle's debug JAR
                    # signature. The Release broker consumes an unsigned bundle;
                    # retain every payload byte while removing only JAR signing
                    # metadata from this disposable input fixture.
                    with zipfile.ZipFile(args.bundle.resolve(strict=True)) as original, \
                            zipfile.ZipFile(source, "w") as unsigned:
                        for entry in original.infolist():
                            parts = Path(entry.filename).parts
                            metadata = len(parts) == 2 and parts[0].upper() == "META-INF"
                            leaf = parts[-1].upper()
                            if metadata and (leaf == "MANIFEST.MF" or leaf.endswith((".SF", ".RSA", ".DSA", ".EC"))):
                                continue
                            with original.open(entry) as reader, unsigned.open(entry, "w") as writer:
                                shutil.copyfileobj(reader, writer)
                else:
                    with zipfile.ZipFile(source, "w") as archive:
                        archive.writestr("base/assets/sample.txt", b"runtime fixture")
                if existing:
                    signed.write_bytes(b"preserve existing output")
                request, response = job / "request.json", job / "response.json"
                session = secrets.token_hex(16)
                request.write_text(json.dumps({"schema": 2, "session": session, "java": str(java),
                    "format": kind, "keystore": str(key), "keyAlias": alias,
                    "inputPackage": str(source), "outputPackage": str(signed)}), encoding="utf-8")
                result = run([probe, "--android-signing", request, response, jobs, key, "upload"],
                    supplied_password + "\n" + supplied_password + "\n")
                if name == "valid":
                    record(name, result.returncode == 0 and response.is_file() and signed.is_file(), result)
                    if json.loads(response.read_text()) != {"schema": 1, "session": session, "succeeded": True}:
                        raise RuntimeError("Signing response did not bind the expected session")
                else:
                    record(name, result.returncode != 0 and not response.exists() and
                        (signed.read_bytes() == b"preserve existing output" if existing else not signed.exists()), result)
                return job, signed

            valid_job, signed = sign("valid")
            verification = [java, "--module", "jdk.jartool/sun.security.tools.jarsigner.Main", "-verify", "-strict",
                            "-keystore", valid_job / "android-bundle-public.jks"]
            verified = run([*verification, signed, "upload"])
            record("independent strict verification", verified.returncode == 0, verified)
            (output / "signed-sha256.txt").write_text(hashlib.sha256(signed.read_bytes()).hexdigest())
            with zipfile.ZipFile(signed, "a") as archive:
                archive.writestr("base/assets/unsigned.txt", b"unsigned fixture")
            rejected = run([*verification, signed, "upload"])
            record("unsigned entry rejected", rejected.returncode != 0, rejected)
            sign("wrong alias", alias="other")
            sign("wrong format", kind="apk")
            sign("wrong password", supplied_password="incorrect-test-password")
            sign("existing output", existing=True)
        (output / "result.json").write_text(json.dumps({"status": "PASS", "tests": results}, indent=2))
        print(f"PASS: {len(results)} native bundle signing checks")
        return 0
    except Exception as error:
        (output / "result.json").write_text(json.dumps({"status": "FAIL", "tests": results, "error": str(error)}, indent=2))
        print(str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
