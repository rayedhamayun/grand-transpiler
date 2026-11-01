
#!/usr/bin/env python3

import argparse
import difflib
import json
import shlex
import subprocess
import sys
import time
import shutil
import os


from dataclasses import dataclass, field
from pathlib import Path


# =========================================================
# RESULTS
# =========================================================

@dataclass
class TestResult:
    program: str
    source_language: str
    target_language: str

    total: int = 0
    passed: int = 0
    failed: int = 0
    skipped: int = 0

    transpile_ok: bool = False
    build_ok: bool = False

    errors: list = field(default_factory=list)
    duration: float = 0.0

    @property
    def success_rate(self):
        if self.total == 0:
            return 0.0

        return self.passed / self.total * 100


# =========================================================
# CONFIGURATION
# =========================================================

def load_config(config_path):
    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)

    required = [
        "transpiler",
        "languages",
        "language_commands",
        "programs"
    ]

    for key in required:
        if key not in config:
            raise ValueError(
                f"Missing configuration key: '{key}'"
            )

    if not isinstance(config["languages"], list):
        raise ValueError("'languages' must be an array")

    if not isinstance(config["programs"], dict):
        raise ValueError("'programs' must be an object")

    config["_base_dir"] = str(config_path.parent)

    return config


def resolve_path(base_dir, path):
    path = Path(path)

    if not path.is_absolute():
        path = base_dir / path

    return path.resolve()


def get_language_config(config, language):
    commands = config.get("language_commands", {})

    if language not in commands:
        raise ValueError(
            f"No execution configuration for '{language}'"
        )

    return commands[language]



# =========================================================
# COMMAND EXECUTION
# =========================================================

def expand_command(command, variables):
    """
    Expand placeholders in a command.

    Commands are executed as argument arrays rather than
    through a shell.
    """

    if isinstance(command, str):
        command = shlex.split(command)

    expanded = [str(a).format_map(variables) for a in command]

    exe = shutil.which(expanded[0])
    if exe:
        expanded[0] = exe
    return expanded


def execute_command(
    command,
    variables,
    cwd=None,
    arguments=None,
    stdin=None,
    timeout=30,
    env=None
):
    """
    Execute a command.

    Arguments are appended individually, without shell
    interpretation.
    """

    expanded = expand_command(command, variables)

    if arguments:
        expanded.extend(str(arg) for arg in arguments)

    return subprocess.run(
        expanded,
        cwd=cwd,
        input=stdin,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        env=env
    )


def print_command_error(description, result):
    print(f"    ERROR: {description}")

    if result.stdout.strip():
        print("    stdout:")
        print(result.stdout.rstrip())

    if result.stderr.strip():
        print("    stderr:")
        print(result.stderr.rstrip())


# =========================================================
# VARIABLES AND OUTPUT NORMALIZATION
# =========================================================

def get_variables(
    program,
    language,
    source,
    output_dir,
    project_dir,
    class_name=None
):
    return {
        "program": program,
        "language": language,
        "source": str(source),
        "source_name": Path(source).name,
        "output_dir": str(output_dir),
        "project_dir": str(project_dir),
        "class_name": class_name or Path(source).stem
    }


def normalize_output(output, config):
    """
    Normalize output before comparing it.

    Defaults to normalizing line endings only.
    """

    comparison = config.get("comparison", {})

    if comparison.get("normalize_line_endings", True):
        output = output.replace("\r\n", "\n")
        output = output.replace("\r", "\n")

    if comparison.get("strip_trailing_whitespace", False):
        output = "\n".join(
            line.rstrip()
            for line in output.split("\n")
        )

    if comparison.get("strip_outer_whitespace", False):
        output = output.strip()

    return output



# =========================================================
# WORKSPACES
# =========================================================

def prepare_workspace(
    config,
    language,
    workspace,
    class_name,
    function,
    module
):
    """
    Fill a directory with the language's driver and the
    generated entry point that calls the function under test.

    Programs contain only their functions, so the same entry
    point works for canonical and transpiled code.
    """

    base_dir = Path(config["_base_dir"])
    lang_config = get_language_config(config, language)

    workspace.mkdir(parents=True, exist_ok=True)

    for name, template in lang_config.get("workspace", {}).items():
        text = resolve_path(base_dir, template).read_text(
            encoding="utf-8"
        )

        text = text.replace("{{class_name}}", class_name)
        text = text.replace("{{function}}", function)
        text = text.replace("{{module}}", module)

        (workspace / name).write_text(text, encoding="utf-8")


def clean_directory(path):
    if path.exists():
        shutil.rmtree(path)


# =========================================================
# BUILD AND EXECUTION
# =========================================================

def build_implementation(
    config,
    program,
    language,
    source,
    output_dir,
    project_dir,
    class_name=None
):
    """
    Run the configured build command.

    A null build command means compilation is not required
    or is handled by the run command.
    """

    lang_config = get_language_config(config, language)

    build_command = lang_config.get("build")

    if not build_command:
        return True

    variables = get_variables(
        program,
        language,
        source,
        output_dir,
        project_dir,
        class_name
    )

    try:
        result = execute_command(
            build_command,
            variables,
            cwd=project_dir,
            timeout=config.get("build_timeout", 120)
        )

        if result.returncode != 0:
            print_command_error("Compilation failed", result)
            return False

        return True

    except subprocess.TimeoutExpired:
        print("    ERROR: Compilation timed out.")
        return False

    except Exception as e:
        print(f"    ERROR: Build failed: {e}")
        return False


def run_implementation(
    config,
    program,
    language,
    source,
    output_dir,
    project_dir,
    arguments,
    class_name=None
):
    """
    Execute a program using command-line arguments.

    Each element in the input array becomes one argument.

    Example:
        ["3", "5", "6"]

    Executes:
        command source 3 5 6

    Returns:
        (success, stdout, error_message)
    """

    lang_config = get_language_config(config, language)

    run_command = lang_config.get("run")

    if not run_command:
        return False, "", "Missing run command"

    variables = get_variables(
        program,
        language,
        source,
        output_dir,
        project_dir,
        class_name
    )

    try:
        result = execute_command(
            run_command,
            variables,
            cwd=project_dir,
            arguments=arguments,
            timeout=config.get("execution_timeout", 30)
        )

        if result.returncode != 0:
            return (
                False,
                result.stdout,
                result.stderr or f"Exit code: {result.returncode}"
            )

        return True, result.stdout, ""

    except subprocess.TimeoutExpired:
        return False, "", "Execution timed out"

    except Exception as e:
        return False, "", str(e)


# =========================================================
# TRANSPILATION
# =========================================================

def transpile(
    config,
    program,
    source_language,
    target_language,
    source_file,
    output_file,
    class_name
):
    """
    Invoke grand-transpiler.

    Supported placeholders:
        {program}
        {source_language}
        {target_language}
        {input}
        {output}
        {class_name}

    Directories in the optional transpiler "path" setting are
    added to PATH for libraries the transpiler needs.
    """

    base_dir = Path(config["_base_dir"])

    command = list(config["transpiler"]["command"])
    command[0] = str(resolve_path(base_dir, command[0]))

    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(
        config["transpiler"].get("path", []) + [env.get("PATH", "")]
    )

    variables = {
        "program": program,
        "source_language": source_language,
        "target_language": target_language,
        "input": str(source_file),
        "output": str(output_file),
        "class_name": class_name
    }

    try:
        result = execute_command(
            command,
            variables,
            cwd=base_dir,
            timeout=config.get("transpilation_timeout", 120),
            env=env
        )

        if result.returncode != 0:
            print_command_error("Transpilation failed", result)
            return False

        if not output_file.is_file():
            print(
                f"    ERROR: Transpiler did not create "
                f"'{output_file}'"
            )
            return False

        return True

    except subprocess.TimeoutExpired:
        print("    ERROR: Transpilation timed out.")
        return False

    except Exception as e:
        print(f"    ERROR: Could not invoke transpiler: {e}")
        return False




# =========================================================
# INPUT VALIDATION
# =========================================================

def validate_inputs(program, inputs):
    """
    Validate the nested input structure.

    Each nested array represents one independent execution.
    """

    if not isinstance(inputs, list):
        raise ValueError(
            f"Inputs for '{program}' must be an array"
        )

    for index, test_case in enumerate(inputs):

        if not isinstance(test_case, list):
            raise ValueError(
                f"Input {index + 1} for '{program}' "
                f"must be an array"
            )

        for argument in test_case:
            if not isinstance(argument, str):
                raise ValueError(
                    f"Every argument in input {index + 1} "
                    f"for '{program}' must be a string"
                )

    return inputs


# =========================================================
# CANONICAL IMPLEMENTATION / EXPECTED OUTPUTS
# =========================================================

def generate_expected_outputs(
    config,
    program,
    source_language,
    source_file,
    inputs,
    expected_dir,
    workspace,
    function,
    class_name
):
    """
    Execute the canonical implementation for every input.

    Each canonical implementation gets its own independent
    expected output directory.

    Returns:
        Dictionary mapping test index to expected stdout.
        None if the canonical implementation cannot build.
    """

    print(
        f"\n  Generating expected outputs "
        f"({source_language})"
    )

    expected_dir.mkdir(parents=True, exist_ok=True)

    clean_directory(workspace)

    extension = get_language_config(
        config, source_language
    )["extension"]

    # A class name means the file must be named after it.
    declares_class = source_language in config["programs"][
        program
    ].get("class_names", {})

    workspace_source = workspace / (
        class_name + extension
        if declares_class
        else source_file.name
    )

    prepare_workspace(
        config,
        source_language,
        workspace,
        class_name,
        function,
        workspace_source.stem
    )

    shutil.copy(source_file, workspace_source)

    project_dir = workspace.resolve()

    if not build_implementation(
        config,
        program,
        source_language,
        workspace_source,
        expected_dir,
        project_dir,
        class_name
    ):
        print("  Canonical implementation could not be built.")
        return None

    expected = {}

    for index, arguments in enumerate(inputs, start=1):

        test_name = f"test_{index:03d}"

        print(f"    {test_name}: ", end="")

        success, output, error = run_implementation(
            config,
            program,
            source_language,
            workspace_source,
            expected_dir,
            project_dir,
            arguments,
            class_name
        )

        if not success:
            print("ORACLE FAILURE")
            print(f"      {error}")

            expected[index] = None
            continue

        expected_file = expected_dir / f"{test_name}.out"

        expected_file.write_text(
            output,
            encoding="utf-8"
        )

        expected[index] = output

        print("PASS")

    if any(value is None for value in expected.values()):
        print(
            "  WARNING: Some canonical executions failed. "
            "Those test cases will be skipped for this source."
        )

    return expected


# =========================================================
# OUTPUT COMPARISON
# =========================================================

def save_output_diff(
    expected,
    actual,
    expected_file,
    actual_file,
    diff_file
):
    """
    Save expected output, actual output and a unified diff.
    """

    expected_file.write_text(expected, encoding="utf-8")
    actual_file.write_text(actual, encoding="utf-8")

    diff = difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        fromfile="expected",
        tofile="actual"
    )

    diff_file.write_text(
        "".join(diff),
        encoding="utf-8"
    )




# =========================================================
# TARGET TESTING
# =========================================================

def test_target(
    config,
    program,
    source_language,
    target_language,
    generated_source,
    inputs,
    expected_outputs,
    output_dir,
    class_name=None
):
    """
    Build a generated implementation and compare every
    execution against the corresponding canonical output.
    """

    result = TestResult(
        program=program,
        source_language=source_language,
        target_language=target_language
    )

    start_time = time.time()

    print(
        f"\n  Testing {source_language} -> {target_language}"
    )

    project_dir = output_dir.resolve()

    if not build_implementation(
        config,
        program,
        target_language,
        generated_source,
        output_dir,
        project_dir,
        class_name
    ):
        result.errors.append("Compilation failed")
        result.duration = time.time() - start_time
        return result

    result.build_ok = True

    for index, arguments in enumerate(inputs, start=1):

        test_name = f"test_{index:03d}"

        expected_output = expected_outputs.get(index)

        if expected_output is None:
            print(f"    {test_name}: SKIPPED (oracle failure)")
            result.skipped += 1
            continue

        print(f"    {test_name}: ", end="")

        success, actual_output, error = run_implementation(
            config,
            program,
            target_language,
            generated_source,
            output_dir,
            project_dir,
            arguments,
            class_name
        )

        result.total += 1

        if not success:
            print("RUNTIME ERROR")
            result.failed += 1
            result.errors.append(
                f"{test_name}: Runtime error: {error}"
            )
            continue

        expected_normalized = normalize_output(
            expected_output, config
        )

        actual_normalized = normalize_output(
            actual_output, config
        )

        if actual_normalized == expected_normalized:
            print("PASS")
            result.passed += 1

        else:
            print("FAIL (output mismatch)")
            result.failed += 1

            result.errors.append(
                f"{test_name}: Output mismatch"
            )

            mismatch_dir = output_dir / "mismatches"
            mismatch_dir.mkdir(parents=True, exist_ok=True)

            save_output_diff(
                expected_normalized,
                actual_normalized,
                mismatch_dir / f"{test_name}.expected.out",
                mismatch_dir / f"{test_name}.actual.out",
                mismatch_dir / f"{test_name}.diff"
            )

    result.duration = time.time() - start_time

    return result


# =========================================================
# PROGRAM ORCHESTRATION
# =========================================================

def test_program(
    config,
    program_name,
    program_config,
    target_filter=None
):
    """
    Test all canonical implementations of a program.

    Each canonical implementation is tested independently
    against every other registered target language.
    """

    print("\n" + "=" * 70)
    print(f"PROGRAM: {program_name}")
    print("=" * 70)

    base_dir = Path(config["_base_dir"])

    inputs = validate_inputs(
        program_name,
        program_config.get("inputs", [])
    )

    if not inputs:
        print("  No test inputs configured.")
        return []

    implementations = program_config.get(
        "files", {}
    )

    if not implementations:
        print("  No canonical implementations configured.")
        return []

    output_root = resolve_path(
        base_dir,
        program_config["outputs"]
    )

    results = []

    # Process each canonical implementation independently.
    for source_language, source_path in implementations.items():

        print("\n" + "-" * 70)
        print(f"CANONICAL SOURCE: {source_language}")
        print("-" * 70)

        if source_language not in config["languages"]:
            print(
                f"  ERROR: {source_language} is not registered "
                f"in the languages list."
            )
            continue

        source_file = resolve_path(base_dir, source_path)

        if not source_file.is_file():
            print(f"  ERROR: Source file not found: {source_file}")
            continue

        source_output_dir = output_root / source_language

        expected_dir = source_output_dir / "expected"

        source_class_name = program_config.get(
            "class_names", {}
        ).get(
            source_language,
            source_file.stem
        )

        function = program_config.get("entry", {}).get(
            source_language
        )

        if not function:
            print(
                f"  ERROR: No entry function for "
                f"{source_language}."
            )
            continue

        # Generate expected outputs using THIS canonical source.
        expected_outputs = generate_expected_outputs(
            config,
            program_name,
            source_language,
            source_file,
            inputs,
            expected_dir,
            source_output_dir / "canonical",
            function,
            source_class_name
        )

        # If the canonical source cannot build, record failures
        # for its target pairs and continue with other sources.
        if expected_outputs is None:

            for target_language in config["languages"]:

                if target_language == source_language:
                    continue

                if (
                    target_filter is not None
                    and target_language != target_filter
                ):
                    continue

                results.append(TestResult(
                    program=program_name,
                    source_language=source_language,
                    target_language=target_language,
                    errors=["Canonical implementation failed to build"]
                ))

            continue

        # Transpile into every other registered language.
        for target_language in config["languages"]:

            if target_language == source_language:
                continue

            if (
                target_filter is not None
                and target_language != target_filter
            ):
                continue

            target_settings = get_language_config(
                config, target_language
            )

            extension = target_settings["extension"]

            filename = program_config.get(
                "filenames", {}
            ).get(
                target_language,
                target_settings.get(
                    "generated_name", program_name
                ) + extension
            )

            class_name = Path(filename).stem

            target_dir = source_output_dir / target_language

            clean_directory(target_dir)

            prepare_workspace(
                config,
                target_language,
                target_dir,
                class_name,
                function,
                class_name
            )

            output_file = target_dir / filename

            print(
                f"\n  Transpiling "
                f"{source_language} -> {target_language}"
            )

            transpile_success = transpile(
                config,
                program_name,
                source_language,
                target_language,
                source_file,
                output_file,
                class_name
            )

            if not transpile_success:
                results.append(TestResult(
                    program=program_name,
                    source_language=source_language,
                    target_language=target_language,
                    errors=["Transpilation failed"]
                ))
                continue

            result = test_target(
                config,
                program_name,
                source_language,
                target_language,
                output_file,
                inputs,
                expected_outputs,
                target_dir,
                class_name
            )

            result.transpile_ok = True

            results.append(result)

    return results




# =========================================================
# SUMMARY
# =========================================================

def print_summary(results):

    print("\n")
    print("=" * 105)
    print("FINAL TEST SUMMARY")
    print("=" * 105)

    if not results:
        print("No transpilation pairs were tested.")
        return

    print(
        f"{'PROGRAM':<18}"
        f"{'PAIR':<28}"
        f"{'INPUTS':>8}"
        f"{'PASSED':>9}"
        f"{'FAILED':>9}"
        f"{'SKIPPED':>9}"
        f"{'SUCCESS':>10}"
    )

    print("-" * 105)

    total_passed = 0
    total_failed = 0
    total_skipped = 0
    total_attempted = 0

    for result in results:

        pair = (
            f"{result.source_language} -> "
            f"{result.target_language}"
        )

        success_rate = (
            f"{result.success_rate:.1f}%"
            if result.total > 0
            else "N/A"
        )

        print(
            f"{result.program:<18}"
            f"{pair:<28}"
            f"{result.total:>8}"
            f"{result.passed:>9}"
            f"{result.failed:>9}"
            f"{result.skipped:>9}"
            f"{success_rate:>10}"
        )

        total_passed += result.passed
        total_failed += result.failed
        total_skipped += result.skipped
        total_attempted += result.total

    print("-" * 105)

    total_rate = (
        total_passed / total_attempted * 100
        if total_attempted else 0
    )

    print(
        f"{'TOTAL':<46}"
        f"{total_attempted:>8}"
        f"{total_passed:>9}"
        f"{total_failed:>9}"
        f"{total_skipped:>9}"
        f"{total_rate:>9.1f}%"
    )

    print("=" * 105)

    failures = [
        result for result in results
        if result.errors
    ]

    if failures:
        print("\nFAILURE DETAILS")

        for result in failures:

            print(
                f"\n  {result.program}: "
                f"{result.source_language} -> "
                f"{result.target_language}"
            )

            for error in result.errors:
                print(f"    - {error}")

    print()


# =========================================================
# COMMAND LINE INTERFACE
# =========================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "Test grand-transpiler against multiple "
            "canonical implementations."
        )
    )

    parser.add_argument(
        "program",
        nargs="?",
        help="Program to test. Omit to test all programs."
    )

    parser.add_argument(
        "target",
        nargs="?",
        help="Optional target language."
    )

    parser.add_argument(
        "--config",
        default="config.json",
        help="Configuration file (default: config.json)"
    )

    args = parser.parse_args()

    config_path = Path(args.config).resolve()

    try:
        config = load_config(config_path)

    except Exception as e:
        print(f"Configuration error: {e}")
        return 2

    programs = config["programs"]
    languages = config["languages"]

    if args.program and args.program not in programs:
        print(f"Unknown program: {args.program}")
        print("Available programs:", ", ".join(programs))
        return 2

    if args.target and args.target not in languages:
        print(f"Unknown target language: {args.target}")
        print("Available languages:", ", ".join(languages))
        return 2

    selected_programs = (
        [args.program]
        if args.program
        else list(programs.keys())
    )

    all_results = []

    print("=" * 70)
    print("GRAND TRANSPILER TEST HARNESS")
    print("=" * 70)

    for program_name in selected_programs:

        try:
            results = test_program(
                config,
                program_name,
                programs[program_name],
                args.target
            )

            all_results.extend(results)

        except Exception as e:
            print(
                f"\nERROR while testing {program_name}: {e}"
            )

    print_summary(all_results)

    failed = any(
        result.errors or result.failed > 0
        for result in all_results
    )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
