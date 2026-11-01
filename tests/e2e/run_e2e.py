#!/usr/bin/env python3
"""Differential end-to-end test.

For each program in tests/programs, for each source language, run the source
through a small driver, translate it to every language (including itself), run
each translation the same way, and compare stdout. A translated program must
also survive a round trip back to its source language.

Equivalent versions of a program must agree with each other unless the
program's directory contains a `languages_differ` marker.

usage: run_e2e.py <grandc> <programs-dir> [program ...]
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

LANGUAGES = {"java": "java", "csharp": "cs", "typescript": "ts"}

CSPROJ = """<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>disable</Nullable>
    <ImplicitUsings>disable</ImplicitUsings>
    <StartupObject>Driver</StartupObject>
    <TreatWarningsAsErrors>false</TreatWarningsAsErrors>
  </PropertyGroup>
</Project>
"""


def run(command, cwd=None, shell=False):
    result = subprocess.run(command, cwd=cwd, shell=shell, capture_output=True,
                            encoding="utf-8", errors="replace")
    if result.returncode != 0:
        raise RuntimeError(f"{command} failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout.replace("\r\n", "\n")


def run_java(work, source, uses_entry):
    shutil.copy(source, work / "Prog.java")
    call = "Prog.main(new String[0]);" if uses_entry else "Prog.run();"
    (work / "Driver.java").write_text(
        "public class Driver { public static void main(String[] a) { " + call + " } }\n")
    run(["javac", "-encoding", "UTF-8", "Prog.java", "Driver.java"], cwd=work)
    return run(["java", "-Dfile.encoding=UTF-8", "-Dstdout.encoding=UTF-8", "Driver"], cwd=work)


def run_csharp(work, source, uses_entry):
    text = source.read_text(encoding="utf-8")
    (work / "Prog.cs").write_text(text, encoding="utf-8")
    if "class Prog" in text:
        call = "Prog.Main();" if uses_entry else "Prog.run();"
        driver = "public class Driver { public static void Main() { " + call + " } }\n"
    else:
        # A file of top-level statements is its own program.
        driver = None
    (work / "app.csproj").write_text(
        CSPROJ if driver else CSPROJ.replace("<StartupObject>Driver</StartupObject>", ""))
    if driver:
        (work / "Driver.cs").write_text(driver)
    run(["dotnet", "build", "-v", "q", "--nologo", "-o", "out"], cwd=work)
    prologue = "$OutputEncoding=[Console]::OutputEncoding=[Text.Encoding]::UTF8;"
    return run(["powershell", "-NoProfile", "-Command", prologue + " dotnet out/app.dll"], cwd=work)


def run_typescript(work, source, uses_entry):
    shutil.copy(source, work / "Prog.ts")
    (work / "package.json").write_text('{"type": "module"}\n')
    body = 'import "./Prog.ts";\n' if uses_entry else 'import { run } from "./Prog.ts";\nrun();\n'
    (work / "Driver.ts").write_text(body)
    return run(["node", "Driver.ts"], cwd=work)


RUNNERS = {"java": run_java, "csharp": run_csharp, "typescript": run_typescript}


def translate(grandc, source, language, output):
    command = [grandc, str(source), "--to", language, "-o", str(output), "--class", "Prog"]
    run(command)


def fresh(root, counter=[0]):
    counter[0] += 1
    path = root / str(counter[0])
    path.mkdir()
    return path


def check_program(grandc, program, root):
    failures = []
    uses_entry = (program / "uses_entry").exists()
    baselines = {}

    for language, extension in LANGUAGES.items():
        source = program / f"Prog.{extension}"
        try:
            baselines[language] = RUNNERS[language](fresh(root), source, uses_entry)
        except RuntimeError as error:
            failures.append(f"{language} source does not run: {error}")

    if not (program / "languages_differ").exists() and len(set(baselines.values())) > 1:
        failures.append(f"sources disagree: {baselines}")

    for source_language, baseline in baselines.items():
        source = program / f"Prog.{LANGUAGES[source_language]}"
        for target, extension in LANGUAGES.items():
            pair = f"{source_language} -> {target}"
            try:
                work = fresh(root)
                output = work / f"out.{extension}"
                translate(grandc, source, target, output)
                got = RUNNERS[target](fresh(root), output, uses_entry)
                if got != baseline:
                    failures.append(f"{pair}: output differs\n--- expected\n{baseline}\n--- got\n{got}")
                    continue
                if target == source_language:
                    continue
                back = work / f"back.{LANGUAGES[source_language]}"
                translate(grandc, output, source_language, back)
                got = RUNNERS[source_language](fresh(root), back, uses_entry)
                if got != baseline:
                    failures.append(f"{pair} -> back: output differs\n--- expected\n{baseline}\n--- got\n{got}")
            except RuntimeError as error:
                failures.append(f"{pair}: {error}")
    return failures


def main():
    grandc = sys.argv[1]
    programs = Path(sys.argv[2])
    selected = sys.argv[3:]
    failed = 0
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        for program in sorted(p for p in programs.iterdir() if p.is_dir()):
            if selected and program.name not in selected:
                continue
            failures = check_program(grandc, program, root)
            print(f"{'FAIL' if failures else 'ok  '} {program.name}", flush=True)
            for failure in failures:
                print("  " + failure.replace("\n", "\n  "), flush=True)
            failed += bool(failures)
    print(f"{failed} program(s) failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
