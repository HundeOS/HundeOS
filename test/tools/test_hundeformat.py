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


@pytest.mark.parametrize("macro", ["critical_section", "critical_section_try"])
def test_critical_section_blocks_have_separate_brace_lines(format_source, macro):
    arguments = "lock, { ready = true; }"
    if macro == "critical_section_try":
        arguments += ", { report_failure(); }"
    formatted = format_source(f"void example(void) {{ {macro}({arguments}); }}\n")
    expected = ["void example(void)", "{", f"    {macro}(", "        lock,", "        {", "            ready = true;"]
    if macro == "critical_section_try":
        expected += ["        },", "        {", "            report_failure();"]
    expected += ["        }", "    );", "}"]
    assert formatted == "\n".join(expected) + "\n"
    assert formatted == format_source(formatted)


@pytest.mark.parametrize("macro, first, second", [
    ("match_result", "success(value)", "error(code, message)"),
    ("match_optional", "some(value)", "none()"),
    ("match_find_result", "some(value)", "none()"),
    ("match_find_result_ptr", "some(value)", "none()"),
])
@pytest.mark.parametrize("body", ["work();", ""])
def test_short_match_branches_expand_to_multiline_blocks(format_source, macro, first, second, body):
    source = f"void example(void) {{ {macro}(call(), {first} {{ {body} }}, {second} {{ {body} }}); }}\n"
    formatted = format_source(source)
    for branch in (first, second):
        expected = f"        {branch}\n        {{\n"
        if body:
            expected += f"            {body}\n"
        expected += "        }"
        assert expected in formatted
    assert formatted == format_source(formatted)


def test_nested_critical_sections_align_with_match_blocks(format_source):
    source = """void example(void) {
    critical_section(lock, {
        critical_section_try(other, { work(); }, { fail(); });
        match_result(operation(), success_do_nothing(), error(code, message) { report(code); });
    });
}
"""
    formatted = format_source(source)
    assert "            critical_section_try(\n                other,\n                {\n" in formatted
    assert "\n            match_result(" in formatted
    assert "        }\n    );\n" in formatted
    assert formatted == format_source(formatted)


@pytest.mark.parametrize("macro, arguments", [
    ("for_each_ll", ["Items", "items", "item"]),
    ("for_each_ll_ptr", ["Items", "items", "item"]),
    ("for_each_fa", ["Items", "items", "index", "item"]),
    ("for_each_fa_ptr", ["Items", "items", "index", "item"]),
    ("for_each_atomic_fa_ptr", ["Items", "items", "index", "item"]),
    ("for_each_map", ["Items", "items", "index", "key", "item"]),
    ("for_each_map_ptr", ["Items", "items", "index", "key", "item"]),
    ("for_each_ring_buffer", ["Items", "items", "index", "item"]),
    ("for_each_ring_buffer_ptr", ["Items", "items", "index", "item"]),
    ("for_each_bstring", ["text", "index", "ch"]),
])
def test_iteration_macros_use_multiline_arguments_and_blocks(format_source, macro, arguments):
    source = f"void example(void) {{ {macro}({', '.join(arguments)}, {{ work(); }}); }}\n"
    expected = ["void example(void)", "{", f"    {macro}("]
    expected.extend(f"        {argument}," for argument in arguments)
    expected += ["        {", "            work();", "        }", "    );", "}"]
    formatted = format_source(source)
    assert formatted == "\n".join(expected) + "\n"
    assert formatted == format_source(formatted)


def test_nested_iteration_matches_and_critical_sections(format_source):
    source = """void example(void) {
    critical_section(lock, {
        for_each_ll(Items, items, item, {
            for_each_fa(Values, values, index, value, {
                match_optional(value, some(found) { work(found); }, none_do_nothing());
            });
        });
    });
}
"""
    formatted = format_source(source)
    assert "            for_each_ll(\n                Items,\n" in formatted
    assert "                    for_each_fa(\n                        Values,\n" in formatted
    assert "                            match_optional(" in formatted
    assert formatted == format_source(formatted)


@pytest.mark.parametrize("body", [
    "match_optional(value, some(item) { report(item); }, none_do_nothing())",
    "critical_section(value, { report(value); })",
    "for_each_ll(Items, value, item, { report(item); })",
])
def test_preserves_preprocessor_continuations(format_source, body):
    source = (
        "#define MATCH(value) \\\n"
        f"    {body}\n"
    )
    formatted = format_source(source)
    logical_lines = formatted.replace("\\\n", "").splitlines()
    assert 1 == len(logical_lines)
    assert "report(" in logical_lines[0]
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


def test_preserves_annotation_on_short_match_branch(format_source):
    source = """void example(void) {
    match_optional(
        value,
        some_do_nothing(),
        none() { legacy(NULL); } // hundelint: ignore-no-null
    );
}
"""
    formatted = format_source(source)
    lines = [line for line in formatted.splitlines() if "NULL" in line]
    assert len(lines) == 1
    assert "// hundelint: ignore-no-null" in lines[0]
    assert formatted.count("hundelint:") == 1
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
