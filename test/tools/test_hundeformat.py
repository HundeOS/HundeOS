import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import hundeformat
import hundelint


COMMAND = ["clang-format"]
if "--" in sys.argv:
    separator = sys.argv.index("--")
    COMMAND = sys.argv[separator + 1:]
    del sys.argv[separator:]


class HundeformatTest(unittest.TestCase):
    def format(self, source):
        return hundeformat.format_source(source, ROOT / "kernel" / "example.c", COMMAND)

    def test_nested_matches_satisfy_lint_and_are_idempotent(self):
        source = """void example(void) {
    critical_section(lock, {
        match_result(operation(), success_do_nothing(), error(code, message) {
            match_optional(value, some(item) {
                if (item) { report(item); }
            }, none_do_nothing());
            report(code);
        });
    });
}
"""
        formatted = self.format(source)
        self.assertEqual([], hundelint._lint_match_branch_macros(pathlib.Path("kernel/example.c"), formatted))
        self.assertEqual(formatted, self.format(formatted))
        self.assertIn("some(item)\n", formatted)
        self.assertIn("error(code, message)\n", formatted)
        self.assertNotIn("if (item) {", formatted)
        self.assertIn("            );\n", formatted)
        self.assertIn("        );\n", formatted)

    def test_places_all_match_macro_closings_on_an_aligned_line(self):
        source = """void example(void) {
    match_optional(optional(), some(value) { report(value); }, none_do_nothing());
    match_optional(collection_at(), some(value) { report(value); }, none_do_nothing());
    match_result(result(), success(value) { report(value); }, error_do_nothing());
    match_find_result(result(), success(value) { report(value); }, error_do_nothing());
    match_find_result_ptr(result(), success(value) { report(value); }, error_do_nothing());
}
"""
        formatted = self.format(source)
        closing_lines = [line for line in formatted.splitlines() if line.strip() == ");"]
        self.assertEqual(["    );"] * 5, closing_lines)
        self.assertEqual(formatted, self.format(formatted))

    def test_preserves_preprocessor_continuations(self):
        source = (
            "#define MATCH(value) \\\n"
            "    match_optional(value, some(item) { report(item); }, none_do_nothing())\n"
        )
        formatted = self.format(source)
        logical_lines = formatted.replace("\\\n", "").splitlines()
        self.assertEqual(1, len(logical_lines))
        self.assertIn("none_do_nothing()", logical_lines[0])
        self.assertEqual(formatted, self.format(formatted))

    def test_preserves_existing_initializer_annotation(self):
        source = (
            "static Example an_initializer_with_a_long_name_to_force_wrapping = { "
            "// hundelint: ignore-constructor-required\n"
            "    .field = 42,\n};\n"
        )
        formatted = self.format(source)
        self.assertIn("// hundelint: ignore-constructor-required", formatted.splitlines()[0])
        self.assertEqual(1, formatted.count("hundelint:"))
        self.assertEqual(formatted, self.format(formatted))

    def test_preserves_annotation_on_long_condition(self):
        source = (
            "void example(void) {\n"
            "    if (first_value == NULL || second_value == NULL || third_value == NULL || fourth_value == NULL || fifth_value == NULL) // hundelint: ignore-no-null\n"
            "    { return; }\n}\n"
        )
        formatted = self.format(source)
        lines = [line for line in formatted.splitlines() if "NULL" in line]
        self.assertEqual(1, len(lines))
        self.assertIn("hundelint: ignore-no-null", lines[0])
        self.assertEqual(formatted, self.format(formatted))

    def test_leaves_comments_and_strings_untouched(self):
        source = (
            'const char *text = "match_optional(x, some(v) {}, none() {})";\n'
            '/* match_result(x, success(v) {}, error(c, m) {}) */\n'
            '/* {\n    // hundelint: example\n} */\n'
        )
        self.assertEqual(source, hundeformat.preserve_conventions(source))

    def test_file_selection_includes_untracked_sources_but_not_generated_or_vendored_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            paths = (
                "kernel/new.c", "common_includes/api.h", "test/unit/test_new.c",
                "test/unit/test_new_Runner.c", "kernel/build/generated.c",
                "vendor/library.c", "user/lib/newlib/library.c",
                "user/drivers/video_term/emoji_atlas.h", "arch/start.s",
            )
            for name in paths:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            with patch.object(hundeformat, "ROOT", root):
                selected = {str(path.relative_to(root)) for path in hundeformat.source_paths()}
            self.assertEqual({"kernel/new.c", "common_includes/api.h", "test/unit/test_new.c"}, selected)


if __name__ == "__main__":
    unittest.main()
