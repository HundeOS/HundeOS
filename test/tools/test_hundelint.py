import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("hundelint", ROOT / "tools" / "hundelint.py")
hundelint = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(hundelint)


class HundelintTest(unittest.TestCase):
    def rules_for(self, source, path="kernel/example.c"):
        findings = hundelint.lint_source(pathlib.Path(path), source)
        return [finding.rule for finding in findings]

    def ast_rules_for(self, source, path="kernel/example.c"):
        try:
            parser = hundelint._load_tree_sitter()
        except RuntimeError:
            self.skipTest("tree-sitter is not installed")
        tree = hundelint._parse_c(parser, source.encode("utf-8"))
        self.assertIsNotNone(tree)
        findings = hundelint.lint_source(pathlib.Path(path), source, tree)
        return [finding.rule for finding in findings]

    def test_reports_null(self):
        self.assertIn("no-null", self.rules_for("void *x = NULL;\n"))

    def test_ignores_null_with_line_comment(self):
        self.assertNotIn(
            "no-null",
            self.rules_for("void *x = NULL; // hundelint: ignore-no-null\n"),
        )

    def test_does_not_report_null_in_string_or_comment(self):
        source = 'const char *text = "NULL";\n// NULL\n/* NULL */\n'
        self.assertNotIn("no-null", self.rules_for(source))

    def test_reports_goto(self):
        self.assertIn("no-goto", self.rules_for("goto done;\n"))

    def test_reports_raw_char_pointer_and_array_buffers(self):
        rules = self.rules_for("char *name;\nchar buffer[16];\n")
        self.assertEqual(2, rules.count("no-raw-char-buffer"))

    def test_allows_const_char_pointers(self):
        rules = self.rules_for("const char *name;\n")
        self.assertNotIn("no-raw-char-buffer", rules)

    def test_allows_main_argv(self):
        rules = self.rules_for("int main(int argc, char ** argv) { return argc; }\n")
        self.assertNotIn("no-raw-char-buffer", rules)

    def test_allows_argv_in_parse_helpers(self):
        rules = self.rules_for("bool parse(int argc, char ** argv) { return argc > 0; }\n")
        self.assertNotIn("no-raw-char-buffer", rules)

    def test_reports_optional_result_internals(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) { "
            "if (value.present && result.ok) { value.some.val; result.error.code; } }\n"
        )
        rules = self.ast_rules_for(source)
        self.assertEqual(6, rules.count("no-optional-result-internals"))

    def test_allows_optional_result_internals_in_macro_headers(self):
        source = "void example(Optional(BString) value) { value.present; value.some.val; }\n"
        rules = self.ast_rules_for(source, "common/types/optional.h")
        self.assertNotIn("no-optional-result-internals", rules)

    def test_does_not_report_unprotected_optional_result_field_names(self):
        source = "void example(Device device) { if (device.ok) { device.error.code; } }\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("no-optional-result-internals", rules)

    def test_reports_collection_optional_internals(self):
        source = "void example(FindResultOptional(ProcessTable) result) { result.present; result.some.val; }\n"
        rules = self.ast_rules_for(source)
        self.assertEqual(3, rules.count("no-optional-result-internals"))

    def test_reports_collection_internals(self):
        source = "void example(void) { BString text; ExampleLinkedList list; text.len; list.head->value; }\n"
        rules = self.ast_rules_for(source)
        self.assertEqual(2, rules.count("no-collection-internals"))

    def test_does_not_report_unprotected_struct_field_names(self):
        rules = self.ast_rules_for("Ext2Read read;\nreturn read.data + read.count;\n")
        self.assertNotIn("no-collection-internals", rules)

    def test_reports_collection_internals_for_function_parameters(self):
        source = "void example(ConstBStringPtr text, ExampleLinkedList * list) { text->len; list->head; }\n"
        rules = self.ast_rules_for(source)
        self.assertEqual(2, rules.count("no-collection-internals"))

    def test_reports_collection_internals_for_macro_defined_alias(self):
        source = "FixedArrayDefine(BString, 4, CommandArguments, bstring_equals);\nvoid example(CommandArguments args) { args.len; }\n"
        rules = self.ast_rules_for(source)
        self.assertEqual(1, rules.count("no-collection-internals"))

    def test_does_not_report_unknown_nested_member_access(self):
        source = "void example(Wrapper wrapper) { wrapper.text.len; }\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("no-collection-internals", rules)

    def test_does_not_record_initializer_arguments_as_collection_symbols(self):
        source = (
            "void example(Context * context, BlockContext * block_context) { "
            "BString message = BString__sprintf(context, block_context); "
            "context->buffer; block_context->data; message.len; }\n"
        )
        rules = self.ast_rules_for(source)
        self.assertEqual(1, rules.count("no-collection-internals"))

    def test_reports_zero_initializer_followed_by_field_assignments(self):
        source = (
            "void example(Node * node) {\n"
            "    DirectoryEntry entry = (DirectoryEntry) { 0 };\n"
            "    entry.attributes = node->attributes;\n"
            "    entry.mode = node->mode;\n"
            "}\n"
        )
        rules = self.ast_rules_for(source)
        self.assertIn("constructor-required", rules)

    def test_does_not_report_zero_initializer_without_field_assignments(self):
        source = (
            "void example(void) {\n"
            "    DirectoryEntry entry = (DirectoryEntry) { 0 };\n"
            "    use(entry);\n"
            "}\n"
        )
        rules = self.ast_rules_for(source)
        self.assertNotIn("constructor-required", rules)

    def test_allows_constructor_required_ignore_comment(self):
        source = (
            "void example(Node * node) {\n"
            "    DirectoryEntry entry = (DirectoryEntry) { 0 }; // hundelint: ignore-constructor-required\n"
            "    entry.attributes = node->attributes;\n"
            "}\n"
        )
        rules = self.ast_rules_for(source)
        self.assertNotIn("constructor-required", rules)

    def test_reports_non_static_member_function_without_self_first_arg(self):
        source = "void BString__copy(BStringPtr target, BStringPtr source) {}\n"
        rules = self.ast_rules_for(source)
        self.assertIn("member-first-arg-self", rules)

    def test_allows_non_static_member_function_with_self_first_arg(self):
        source = "void BString__copy(BStringPtr self, BStringPtr source) {}\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("member-first-arg-self", rules)

    def test_allows_static_member_function_without_self_first_arg(self):
        source = "static void BString__copy(BStringPtr target, BStringPtr source) {}\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("member-first-arg-self", rules)

    def test_allows_non_member_function_without_self_first_arg(self):
        source = "void copy(BStringPtr target, BStringPtr source) {}\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("member-first-arg-self", rules)

    def test_allows_member_constructor_with_no_args(self):
        source = "BString BString__new() { return EMPTY_BSTRING; }\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("member-first-arg-self", rules)

    def test_allows_namespaced_function_when_first_arg_is_not_member_type(self):
        source = "IOHandle IO__open(ConstBStringPtr path, Flags flags) { return 0; }\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("member-first-arg-self", rules)

    def test_allows_namespaced_enum_helper_when_first_arg_is_not_pointer_like(self):
        source = "const char * ProcessState__to_string(ProcessState state) { return \"state\"; }\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("member-first-arg-self", rules)

    def test_allows_member_first_arg_ignore_comment(self):
        source = "void BString__copy(BStringPtr target) { // hundelint: ignore-member-first-arg-self\n}\n"
        rules = self.ast_rules_for(source)
        self.assertNotIn("member-first-arg-self", rules)

    def test_reports_legacy_match_macros(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) {\n"
            "    Match(BString, value)\n"
            "        IsSome(text, use(text))\n"
            "        IsNone(return)\n"
            "    ResultMatch(ByteCount, result)\n"
            "        IsSuccess(count, use(count))\n"
            "        IsError(code, message, return)\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(6, rules.count("legacy-match-macros"))

    def test_allows_new_match_macros(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) {\n"
            "    match_optional(BString, value,\n"
            "        some(text) { use(text); },\n"
            "        none() { return; }\n"
            "    );\n"
            "    match_result(ByteCount, result,\n"
            "        success(count)\n"
            "        {\n"
            "            use(count);\n"
            "        },\n"
        "        error(code, message)\n"
        "        {\n"
        "            report(code);\n"
        "            return;\n"
        "        }\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertNotIn("legacy-match-macros", rules)
        self.assertNotIn("match-branch-macros", rules)
        self.assertNotIn("match-branch-format", rules)
        self.assertNotIn("match-branch-block-format", rules)

    def test_allows_explicit_do_nothing_and_test_branch_macros_in_tests(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) {\n"
            "    match_optional(BString, value,\n"
            "        some_do_nothing(),\n"
            "        none_test_fail()\n"
            "    );\n"
            "    match_result(ByteCount, result,\n"
            "        success_do_nothing(),\n"
            "        error_test_ok()\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source, "test/example.c")
        self.assertNotIn("match-branch-macros", rules)
        self.assertNotIn("match-branch-format", rules)
        self.assertNotIn("match-branch-block-format", rules)
        self.assertNotIn("test-fail-branch-outside-tests", rules)

    def test_rejects_test_fail_branch_macros_outside_tests(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) {\n"
            "    match_optional(BString, value,\n"
            "        some_test_fail(),\n"
            "        none_do_nothing()\n"
            "    );\n"
            "    match_result(ByteCount, result,\n"
            "        success_do_nothing(),\n"
            "        error_test_fail()\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(2, rules.count("test-fail-branch-outside-tests"))
        self.assertEqual(2, rules.count("match-branch-macros"))

    def test_rejects_error_do_nothing_and_empty_error_blocks(self):
        source = (
            "void example(Result(ByteCount) result) {\n"
            "    match_result(ByteCount, result,\n"
            "        success_do_nothing(),\n"
            "        error_do_nothing()\n"
            "    );\n"
            "    match_result(ByteCount, result,\n"
            "        success_do_nothing(),\n"
            "        error(code, message) {}\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(1, rules.count("match-branch-macros"))
        self.assertEqual(1, rules.count("trivial-error-branch"))

    def test_rejects_trivial_error_return_break_and_continue_blocks(self):
        source = (
            "void example(Result(ByteCount) result) {\n"
            "    match_result(ByteCount, result,\n"
            "        success_do_nothing(),\n"
            "        error(code, message) { return; }\n"
            "    );\n"
            "    match_result(ByteCount, result,\n"
            "        success_do_nothing(),\n"
            "        error(code, message) { break; }\n"
            "    );\n"
            "    match_result(ByteCount, result,\n"
            "        success_do_nothing(),\n"
            "        error(code, message) { continue; }\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(3, rules.count("trivial-error-branch"))

    def test_allows_trivial_error_branch_with_explicit_ignore(self):
        source = (
            "void example(Result(ByteCount) result) {\n"
            "    match_result(ByteCount, result, // hundelint: ignore-trivial-error-branch\n"
            "        success_do_nothing(),\n"
            "        error(code, message) { return; }\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertNotIn("match-branch-macros", rules)
        self.assertNotIn("trivial-error-branch", rules)

    def test_reports_inline_match_branch_macros(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) {\n"
            "    match_optional(BString, value, some(text) { use(text); }, none() { return; });\n"
            "    match_result(ByteCount, result,\n"
            "        success(count) { use(count); }, error(code, message) { report(code); return; }\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(2, rules.count("match-branch-format"))

    def test_reports_multiline_match_branch_brace_on_same_line(self):
        source = (
            "void example(Result(ByteCount) result) {\n"
            "    match_result(ByteCount, result,\n"
            "        success(count) {\n"
            "            use(count);\n"
            "        },\n"
            "        error(code, message) { return; }\n"
            "    );\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(1, rules.count("match-branch-block-format"))

    def test_reports_match_without_branch_macros(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) {\n"
            "    match_optional(BString, value, text, { use(text); }, { return; });\n"
            "    match_result(ByteCount, result, count, { use(count); }, code, message, { return; });\n"
            "    match_optional(BString, value, some(text), { use(text); }, none(), { return; });\n"
            "    match_result(ByteCount, result, success(count), { use(count); }, error(code, message), { return; });\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(4, rules.count("match-branch-macros"))

    def test_allows_legacy_unwrap_macros_in_macro_headers(self):
        source = "#define Unwrap(T, opt, alternate_val) ((opt).present ? (opt).some.val : (alternate_val))\n"
        rules = self.rules_for(source, "common/types/optional.h")
        self.assertNotIn("legacy-match-macros", rules)

    def test_does_not_report_legacy_match_macros_in_string_or_comment(self):
        source = 'const char *text = "Match(BString, value)";\n// ResultMatch(ByteCount, result)\n'
        rules = self.rules_for(source)
        self.assertNotIn("legacy-match-macros", rules)

    def test_allows_legacy_match_macro_ignore_comment(self):
        source = "Match(BString, value) // hundelint: ignore-legacy-match-macros\n"
        rules = self.rules_for(source)
        self.assertNotIn("legacy-match-macros", rules)

    def test_reports_legacy_critical_section_macros(self):
        source = (
            "void example(mutex_t lock) {\n"
            "    CriticalSection(lock, value = 1;)\n"
            "    CriticalSectionTry(lock, value = 2;, value = 3;)\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(2, rules.count("legacy-critical-section-macros"))

    def test_reports_raw_control_flow_in_critical_section(self):
        source = (
            "int example(mutex_t lock) {\n"
            "    critical_section(lock,\n"
            "    {\n"
            "        if (failed) { return 1; }\n"
            "        if (done) { break; }\n"
            "        continue;\n"
            "    });\n"
            "    return 0;\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(3, rules.count("critical-section-control-flow"))

    def test_allows_cs_control_flow_macros_in_critical_section(self):
        source = (
            "int example(mutex_t lock) {\n"
            "    critical_section(lock,\n"
            "    {\n"
            "        if (failed) { cs_return 1; }\n"
            "        if (done) { cs_exit_then(return 2); }\n"
            "    });\n"
            "    return 0;\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertNotIn("critical-section-control-flow", rules)

    def test_reports_block_macro_without_semicolon(self):
        source = (
            "void example(Optional(BString) value, TestList list) {\n"
            "    match_optional(BString, value,\n"
            "        some(text) { use(text); },\n"
            "        none() {}\n"
            "    )\n"
            "    for_each_ll(TestList, list, item,\n"
            "    {\n"
            "        use(item);\n"
            "    })\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(2, rules.count("block-macro-semicolon"))

    def test_allows_block_macro_with_semicolon(self):
        source = (
            "void example(Optional(BString) value, TestList list) {\n"
            "    match_optional(BString, value,\n"
            "        some(text) { use(text); },\n"
            "        none() {}\n"
            "    );\n"
            "    for_each_ll(TestList, list, item,\n"
            "    {\n"
            "        use(item);\n"
            "    });\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertNotIn("block-macro-semicolon", rules)

    def test_reports_unwrap_macros(self):
        source = (
            "void example(Optional(BString) value, Result(ByteCount) result) {\n"
            "    BString text = Unwrap(BString, value, EMPTY_BSTRING);\n"
            "    ByteCount count = ResultUnwrap(ByteCount, result, 0);\n"
            "}\n"
        )
        rules = self.rules_for(source)
        self.assertEqual(2, rules.count("legacy-match-macros"))

    def test_allows_result_unwrap_macro(self):
        source = "ByteCount count = result_unwrap(ByteCount, result, 0);\n"
        rules = self.rules_for(source)
        self.assertNotIn("legacy-match-macros", rules)

    def test_allows_collection_internals_in_type_implementations(self):
        source = "void example(void) { BString text; ExampleLinkedList list; text.len; list.head->value; }\n"
        rules = self.ast_rules_for(source, "common/types/bstring.c")
        self.assertNotIn("no-collection-internals", rules)

    def test_skips_newlib_files(self):
        files = hundelint.iter_c_files([ROOT / "user" / "lib"])
        self.assertFalse(
            any("user/lib/newlib/" in path.as_posix() for path in files)
        )


if __name__ == "__main__":
    unittest.main()
