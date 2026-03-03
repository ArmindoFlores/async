#!/usr/bin/env python3
import argparse
import dataclasses
import difflib
import fcntl
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


TEST_RESULT_PATTERN = re.compile(r'/\*\s*TEST RESULT\s*(\{.*?\})\s*\*/', re.DOTALL)


@dataclasses.dataclass
class TestResult:
    valgrind: str | None = None
    exit_code: int = 0
    stdout: str | None = None
    stderr: str | None = None


@dataclasses.dataclass
class GlobalTestReport:
    total: int
    failures: int


@dataclasses.dataclass
class TestReport:
    path: Path
    expected: TestResult
    result: TestResult | None = None
    error_message: str | None = None


def get_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="A tool to run tests in a C project."
    )
    parser.add_argument("directory", help="The test directory.")
    parser.add_argument(
        "--bin", "-b",
        default="bin", 
        help="The directory where executables can be found."
    )
    parser.add_argument(
        "--recursive", "-r", 
        action="store_true", 
        help="Recursively look for tests in the test directory."
    )
    parser.add_argument(
        "--list", "-l", 
        action="store_true", 
        help="List test files instead of running them."
    )
    parser.add_argument(
        "--extensions", "-e", 
        default="c,C",
        help="Find test files with the specified extensions."
    )
    return parser


def _r(string: str) -> str:
    return f"\x1b[1;31m{string}\x1b[m"

def _g(string: str) -> str:
    return f"\x1b[1;32m{string}\x1b[m"

def _y(string: str) -> str:
    return f"\x1b[1;33m{string}\x1b[m"

def gather_test_files(directory: str, extensions: set[str], recursive: bool = False) -> list[Path]:
    if recursive:
        return [f for f in Path(directory).rglob("*") if f.suffix[1:] in extensions]
    return [f for f in Path(directory).glob("*") if f.suffix[1:] in extensions]


def find_expected_result_from_test_source(source: Path) -> TestResult | str:
    try:
        with source.open() as f:
            re_match = TEST_RESULT_PATTERN.search(f.read())
    except Exception as e:
        return f"failed to read source file ({e!s})"
    
    if re_match is None:
        return "expected result not specified"
    try:
        json_result = json.loads(re_match.group(1))
    except json.JSONDecodeError as e:
        return f"failed to decode expected result ({e!s})"
    
    if not isinstance(json_result, dict):
        return "expected test result must be a JSON object"
    
    if "exit_code" in json_result and not isinstance(json_result["exit_code"], int):
        return "exit_code must be an integer"
    
    if "stdout" in json_result and not isinstance(json_result["stdout"], (list, str)):
        return "stdout must be a string or a list of strings"
    
    if "stderr" in json_result and not isinstance(json_result["stderr"], (list, str)):
        return "stderr must be a string or a list of strings"

    if "stdout" in json_result and isinstance(json_result["stdout"], list):
        try:
            json_result["stdout"] = "\n".join(json_result["stdout"])
        except TypeError:
            return "stdout must be a string or a list of strings"
        
    if "stderr" in json_result and isinstance(json_result["stderr"], list):
        try:
            json_result["stderr"] = "\n".join(json_result["stderr"])
        except TypeError:
            return "stderr must be a string or a list of strings"


    return TestResult(
        exit_code=json_result.get("exit_code", 0),
        stdout=json_result.get("stdout"),
        stderr=json_result.get("stderr"),
    )


def find_executable_from_test_source(source: Path, directory: str) -> Path | None:
    source_file_name = source.stem
    try:
        return next(Path(directory).rglob(source_file_name))
    except Exception:
        return None


def read_all(fd: int, chunk_size: int = 4096) -> bytes:
    buffer = b""
    while (chunk := os.read(fd, chunk_size)) != b"":
        buffer += chunk
    return buffer

def run_test(executable: Path) -> TestResult | str:
    read_fd, write_fd = os.pipe()
    high_write_fd = fcntl.fcntl(write_fd, fcntl.F_DUPFD, 100)
    os.close(write_fd)
    write_fd = high_write_fd
    write_closed = False

    try:
        process = subprocess.run(
            ["valgrind", "--xml=yes", f"--xml-fd={write_fd}", "--leak-check=full", str(executable.resolve())],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            pass_fds=(write_fd,)
        )
        os.close(write_fd)
        write_closed = True
        valgrind_output = read_all(read_fd)
    except subprocess.CalledProcessError as e:
        return f"{e!s}"
    finally:
        os.close(read_fd)
        if not write_closed:
            os.close(write_fd)

    return TestResult(
        exit_code=process.returncode,
        stdout=process.stdout.decode(),
        stderr=process.stderr.decode(),
        valgrind=valgrind_output.decode()
    )


def validate_valgrind(output: str) -> list[str]:
    if output is None:
        print(f"{_y('warning:')} no valgrind output")
        return []
    try:
        valgrind_xml = ET.fromstring(output)
    except Exception:
        print(f"{_y('warning:')} failed to parse valgrind output")
        return []
    
    errors = {}
    for error in valgrind_xml.findall("error"):
        tid = getattr(error.find("tid"), "text")
        if tid is None:
            continue

        if tid in errors:
            errors[tid]["count"] += 1
            continue

        what = error.find("what")
        if what is None:
            what = error.find("xwhat")
            if what is not None:
                what = f"memory leak: {what.find('text').text}"
        else:
            what = what.text.lower()
        
        errors[tid] = {
            "what": what if what is not None else "unknown",
            "count": 1
        }
    
    return [
        f"{error['count']} instance{'s' if error['count'] != 1 else ''} of {error['what']}"
        for error in errors.values()
    ]


def diff(result: TestResult, expected: TestResult) -> list[str]:
    stdout_diff = difflib.unified_diff(
        (result.stdout or "").splitlines(), (expected.stdout or "").splitlines()
    )
    stderr_diff = difflib.unified_diff(
        (result.stderr or "").splitlines(), (expected.stderr or "").splitlines()
    )
    errors = []
    stdout_diff_str = "".join(("\t"+line for line in stdout_diff))
    stderr_diff_str = "".join(("\t"+line for line in stderr_diff))
    if stdout_diff_str != "":
        errors.append("\ntest output in stdout differs from expected:\n"+stdout_diff_str)
    if stderr_diff_str != "":
        errors.append("\ntest output in stderr differs from expected:\n"+stderr_diff_str)
    return errors

def generate_report(source: Path, result: TestResult, expected: TestResult) -> TestReport: 
    valgrind_errors = validate_valgrind(result.valgrind)
    diff_errors = diff(result, expected)

    error_msg = ""
    if len(valgrind_errors):
        error_msg += "\nvalgrind found the following issues:\n"
        error_msg += "\n".join(("\t- "+line for line in valgrind_errors))
    if len(diff_errors):
        error_msg += "\n" if error_msg != "" else ""
        error_msg += "\n".join(diff_errors)
    if result.exit_code != expected.exit_code:
        error_msg += "\n" if error_msg != "" else ""
        error_msg += f"test exited with code {result.exit_code} (expected {expected.exit_code})"

    return TestReport(
        path=source,
        result=result,
        expected=expected,
        error_message=error_msg if error_msg != "" else None
    )

def report(test_report: TestReport, global_report: GlobalTestReport) -> None:
    if test_report.error_message is not None:
        global_report.failures += 1
        print(f"{_r('error')} in test '{test_report.path}': {test_report.error_message}")
        return
    print(f"test '{test_report.path}' has {_g('succeeded')}")


def main(args: argparse.Namespace):
    extensions = set(args.extensions.split(","))
    files = gather_test_files(args.directory, extensions, args.recursive)
    if len(files) == 0:
        print("No tests found.")
    
    if args.list:
        for file in files:
            print(file)
        return
    
    global_report = GlobalTestReport(len(files), 0)
    for test in files:
        expected = find_expected_result_from_test_source(test)
        if isinstance(expected, str):
            report(TestReport(test, None, TestResult(), expected), global_report)
            continue
        
        executable = find_executable_from_test_source(
            test,
            args.bin
        )
        if executable is None:
            report(TestReport(test, None, expected, "could not find executable"), global_report)
            continue

        result = run_test(executable)
        result_report = generate_report(test, result, expected)
        report(result_report, global_report)
    
    if global_report.failures == 0:
        print(_g("all tests passed!"))
        return
    
    print(f"{global_report.total - global_report.failures} tests {_g('passed')}, {global_report.failures} tests {_r('failed')}")


if __name__ == "__main__":
    parser = get_parser()
    main(parser.parse_args())
