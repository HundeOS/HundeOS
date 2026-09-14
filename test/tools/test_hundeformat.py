import pathlib

import pytest

import hundeformat
import hundelint


ROOT = pathlib.Path(__file__).resolve().parents[2]


@pytest.fixture
def format_source(clang_format_command):
    def format_code(source):
        return hundeformat.format_source(source, ROOT / "kernel" / "example.c", clang_format_command)

    return format_code


def test_nested_matches_satisfy_lint_and_are_idempotent(format_source):
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
    formatted = format_source(source)
    assert [] == hundelint._lint_match_branch_macros(pathlib.Path("kernel/example.c"), formatted)
    assert formatted == format_source(formatted)
    assert "some(item)\n" in formatted
    assert "error(code, message)\n" in formatted
    assert "if (item) {" not in formatted
    assert "            );\n" in formatted
    assert "        );\n" in formatted


def test_places_all_match_macro_closings_on_an_aligned_line(format_source):
    source = """void example(void) {
    match_optional(optional(), some(value) { report(value); }, none_do_nothing());
    match_optional(collection_at(), some(value) { report(value); }, none_do_nothing());
    match_result(result(), success(value) { report(value); }, error_do_nothing());
    match_find_result(result(), success(value) { report(value); }, error_do_nothing());
    match_find_result_ptr(result(), success(value) { report(value); }, error_do_nothing());
}
"""
    formatted = format_source(source)
    closing_lines = [line for line in formatted.splitlines() if line.strip() == ");"]
    assert ["    );"] * 5 == closing_lines
    assert formatted == format_source(formatted)


def test_preserves_preprocessor_continuations(format_source):
    source = (
        "#define MATCH(value) \\\n"
        "    match_optional(value, some(item) { report(item); }, none_do_nothing())\n"
    )
    formatted = format_source(source)
    logical_lines = formatted.replace("\\\n", "").splitlines()
    assert 1 == len(logical_lines)
    assert "none_do_nothing()" in logical_lines[0]
    assert formatted == format_source(formatted)


def test_preserves_existing_initializer_annotation(format_source):
    source = (
        "static Example an_initializer_with_a_long_name_to_force_wrapping = { "
        "// hundelint: ignore-constructor-required\n"
        "    .field = 42,\n};\n"
    )
    formatted = format_source(source)
    assert "// hundelint: ignore-constructor-required" in formatted.splitlines()[0]
    assert 1 == formatted.count("hundelint:")
    assert formatted == format_source(formatted)


def test_preserves_annotation_on_long_condition(format_source):
    source = (
        "void example(void) {\n"
        "    if (first_value == NULL || second_value == NULL || third_value == NULL || fourth_value == NULL || fifth_value == NULL) // hundelint: ignore-no-null\n"
        "    { return; }\n}\n"
    )
    formatted = format_source(source)
    lines = [line for line in formatted.splitlines() if "NULL" in line]
    assert 1 == len(lines)
    assert "hundelint: ignore-no-null" in lines[0]
    assert formatted == format_source(formatted)


def test_leaves_comments_and_strings_untouched():
    source = (
        'const char *text = "match_optional(x, some(v) {}, none() {})";\n'
        '/* match_result(x, success(v) {}, error(c, m) {}) */\n'
        '/* {\n    // hundelint: example\n} */\n'
    )
    assert source == hundeformat.preserve_conventions(source)


def test_file_selection_includes_untracked_sources_but_not_generated_or_vendored_files(tmp_path):
    paths = (
        "kernel/new.c", "common_includes/api.h", "test/unit/test_new.c",
        "test/unit/test_new_Runner.c", "kernel/build/generated.c",
        "vendor/library.c", "user/lib/newlib/library.c",
        "user/drivers/video_term/emoji_atlas.h", "arch/start.s",
    )
    for name in paths:
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    selected = {str(path.relative_to(tmp_path)) for path in hundeformat.source_paths(tmp_path)}
    assert {"kernel/new.c", "common_includes/api.h", "test/unit/test_new.c"} == selected
