import importlib.util
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
RUN_TESTS_PATH = PROJECT_ROOT / "test" / "run_tests.py"


def load_run_tests():
    spec = importlib.util.spec_from_file_location("run_tests", RUN_TESTS_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_generate_makefile_adds_manifest_defines(tmp_path):
    run_tests = load_run_tests()
    manifest = tmp_path / "manifest.yml"
    manifest.write_text(
        "manifest:\n"
        "  defines:\n"
        "    - KERNEL_MEMORY_TRACE\n"
        "  source_files: []\n",
        encoding="utf-8",
    )

    run_tests.generate_makefile(str(manifest), tmp_path, str(tmp_path / "unity"), [])

    generated = (tmp_path / "Makefile.gen").read_text(encoding="utf-8")
    assert "-DKERNEL_MEMORY_TRACE" in generated
