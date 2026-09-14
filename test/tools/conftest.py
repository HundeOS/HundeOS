import shlex

import pytest


def pytest_addoption(parser):
    parser.addoption("--clang-format", default="clang-format", help="Formatter command to test")


@pytest.fixture
def clang_format_command(pytestconfig):
    return shlex.split(pytestconfig.getoption("--clang-format"))
