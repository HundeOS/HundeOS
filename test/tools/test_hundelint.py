import pathlib

import pytest

import hundelint


ROOT = pathlib.Path(__file__).resolve().parents[2]


@pytest.fixture
def rules_for():
    def lint(source, path="kernel/example.c"):
        findings = hundelint.lint_source(pathlib.Path(path), source)
        return [finding.rule for finding in findings]

    return lint


@pytest.fixture
def parser():
    return hundelint._load_tree_sitter()


@pytest.fixture
def ast_rules_for(parser):
    def lint(source, path="kernel/example.c"):
        tree = hundelint._parse_c(parser, source.encode("utf-8"))
        assert tree is not None
        findings = hundelint.lint_source(pathlib.Path(path), source, tree)
        return [finding.rule for finding in findings]

    return lint


def test_reports_null(rules_for):
    assert "no-null" in rules_for("void *x = NULL;\n")


def test_ignores_null_with_line_comment(rules_for):
    assert "no-null" not in rules_for("void *x = NULL; // hundelint: ignore-no-null\n")


def test_does_not_report_null_in_string_or_comment(rules_for):
    source = 'const char *text = "NULL";\n// NULL\n/* NULL */\n'
    assert "no-null" not in rules_for(source)


def test_reports_pointer_comparison_to_zero(ast_rules_for):
    source = (
        "typedef struct Fiber * FiberPtr;\n"
        "void example(FiberPtr fiber, void * raw) {\n"
        "    if (fiber == 0) { }\n"
        "    if (0 != raw) { }\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert 2 == rules.count("no-null")


def test_reports_pointer_array_item_comparison_to_zero(ast_rules_for):
    source = (
        "typedef struct Fiber * FiberPtr;\n"
        "static FiberPtr fibers[16] = { 0 };\n"
        "void example(int index) {\n"
        "    if (fibers[index] == 0) { }\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert 1 == rules.count("no-null")


def test_reports_pointer_field_zero_initializer(ast_rules_for):
    source = (
        "typedef struct Fiber * FiberPtr;\n"
        "typedef struct Fiber Fiber;\n"
        "struct Fiber { FiberPtr next; int count; };\n"
        "Fiber example(void) { return (Fiber) { .next = 0, .count = 0 }; }\n"
    )
    rules = ast_rules_for(source)
    assert 1 == rules.count("no-null")


def test_reports_address_field_zero_initializer(ast_rules_for):
    source = (
        "typedef unsigned long Address;\n"
        "typedef struct Fiber Fiber;\n"
        "struct Fiber { Address stack_base; size_t stack_size; };\n"
        "Fiber example(void) { return (Fiber) { .stack_base = 0, .stack_size = 0 }; }\n"
    )
    rules = ast_rules_for(source)
    assert 1 == rules.count("no-null")


def test_does_not_report_integer_comparison_to_zero(ast_rules_for):
    source = "void example(int status) { if (status == 0) { } }\n"
    assert "no-null" not in ast_rules_for(source)


def test_reports_goto(rules_for):
    assert "no-goto" in rules_for("goto done;\n")


def test_reports_conditional_compilation_inside_function(ast_rules_for):
    source = "void example(void) {\n#ifdef ENABLE_TRACE\ntrace();\n#endif\n}\n"
    assert "no-conditional-compilation-in-function" in ast_rules_for(source)


def test_allows_conditionally_defined_function_called_unconditionally(ast_rules_for):
    source = (
        "#ifdef ENABLE_TRACE\n"
        "static void trace(void) { emit(); }\n"
        "#else\n"
        "#define trace()\n"
        "#endif\n"
        "void example(void) { trace(); }\n"
    )
    assert "no-conditional-compilation-in-function" not in ast_rules_for(source)


def test_allows_header_guard(ast_rules_for):
    source = "#ifndef EXAMPLE_H\n#define EXAMPLE_H\nvoid example(void);\n#endif\n"
    assert "no-conditional-compilation-in-function" not in ast_rules_for(source, "kernel/example.h")


def test_reports_raw_char_pointer_and_array_buffers(rules_for):
    rules = rules_for("char *name;\nchar buffer[16];\n")
    assert 2 == rules.count("no-raw-char-buffer")


def test_allows_const_char_pointers(rules_for):
    rules = rules_for("const char *name;\n")
    assert "no-raw-char-buffer" not in rules


def test_allows_main_argv(rules_for):
    rules = rules_for("int main(int argc, char ** argv) { return argc; }\n")
    assert "no-raw-char-buffer" not in rules


def test_allows_argv_in_parse_helpers(rules_for):
    rules = rules_for("bool parse(int argc, char ** argv) { return argc > 0; }\n")
    assert "no-raw-char-buffer" not in rules


def test_reports_optional_result_internals(ast_rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) { "
        "if (value.present && result.ok) { value.some.val; result.error.code; } }\n"
    )
    rules = ast_rules_for(source)
    assert 6 == rules.count("no-optional-result-internals")


def test_reports_optional_payload_internals_on_nested_struct_field(ast_rules_for):
    source = (
        "void example(IRQDispatcherPtr self) { "
        "self->interrupt_controller.some.val; self->dispatch_result.success.val; }\n"
    )
    rules = ast_rules_for(source)
    assert 4 == rules.count("no-optional-result-internals")


def test_allows_optional_result_internals_in_macro_headers(ast_rules_for):
    source = "void example(Optional(BString) value) { value.present; value.some.val; }\n"
    rules = ast_rules_for(source, "common/types/optional.h")
    assert "no-optional-result-internals" not in rules


def test_does_not_report_unprotected_optional_result_field_names(ast_rules_for):
    source = "void example(Device device) { if (device.ok) { device.error.code; } }\n"
    rules = ast_rules_for(source)
    assert "no-optional-result-internals" not in rules


def test_reports_collection_optional_internals(ast_rules_for):
    source = "void example(FindResultOptional(ProcessTable) result) { result.present; result.some.val; }\n"
    rules = ast_rules_for(source)
    assert 3 == rules.count("no-optional-result-internals")


def test_reports_generated_collection_typedefs(rules_for):
    source = (
        "void example(void) {\n"
        "    Values__type value;\n"
        "    Values__type_ptr pointer;\n"
        "    Values__type_ptr__optional optional;\n"
        "    ValueMap__value_type_ptr mapped;\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 4 == rules.count("no-generated-collection-types")


def test_allows_generated_collection_typedefs_inside_implementation(rules_for):
    source = "typedef type alias__type;\n"
    rules = rules_for(source, "common/types/fixed_array.h")
    assert "no-generated-collection-types" not in rules


def test_allows_inferred_optional_matcher(rules_for):
    source = (
        "void example(void) {\n"
        "    match_optional(values_at(),\n"
        "        some(value)\n"
        "        {\n"
        "            use(value);\n"
        "        },\n"
        "        none_do_nothing()\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert "match-branch-macros" not in rules
    assert "block-macro-semicolon" not in rules


def test_allows_match_find_result_branches(rules_for):
    source = (
        "void example(ProcessTable processes) {\n"
        "    match_find_result(ProcessTable__find(&processes, pid_matches, &pid),\n"
        "        some(process)\n"
        "        {\n"
        "            use(process);\n"
        "        },\n"
        "        none()\n"
        "        {\n"
        "            return;\n"
        "        }\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert "match-branch-macros" not in rules
    assert "block-macro-semicolon" not in rules


def test_allows_match_find_result_ptr_branches(rules_for):
    source = (
        "void example(ProcessTable processes) {\n"
        "    match_find_result_ptr(ProcessTable__find(&processes, pid_matches, &pid),\n"
        "        some(process)\n"
        "        {\n"
        "            use(process);\n"
        "        },\n"
        "        none()\n"
        "        {\n"
        "            return;\n"
        "        }\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert "match-branch-macros" not in rules
    assert "block-macro-semicolon" not in rules


def test_reports_match_find_result_without_branch_macros(rules_for):
    source = (
        "void example(ProcessTable processes) {\n"
        "    match_find_result(ProcessTable__find(&processes, pid_matches, &pid),\n"
        "        { use(process); },\n"
        "        none()\n"
        "        {\n"
        "            return;\n"
        "        }\n"
        "    );\n"
        "}\n"
    )
    assert "match-branch-macros" in rules_for(source)


def test_allows_direct_find_result_matching_when_the_entry_is_needed(rules_for):
    source = (
        "void example(ProcessTable processes) {\n"
        "    match_optional(ProcessTable__find(&processes, pid_matches, &pid),\n"
        "        some(process)\n"
        "        {\n"
        "            use(process.value);\n"
        "        },\n"
        "        none_do_nothing()\n"
        "    );\n"
        "}\n"
    )
    assert "match-branch-macros" not in rules_for(source)


def test_reports_explicit_type_arguments_to_matchers(rules_for):
    source = (
        "void example(void) {\n"
        "    match_optional(Value, optional(), some(value) {}, none_do_nothing());\n"
        "    match_result(Value, result(), success(value) {}, error(code, message) { return; });\n"
        "    match_find_result(Values, find(), some(value) {}, none_do_nothing());\n"
        "    match_find_result_ptr(Values, find(), some(value) {}, none_do_nothing());\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 4 == rules.count("typed-match-argument")


def test_reports_legacy_collection_optional_matcher(rules_for):
    source = "match_collection_optional(optional(), some(value) {}, none_do_nothing());\n"
    assert "legacy-collection-optional-matcher" in rules_for(source)


def test_reports_collection_internals(ast_rules_for):
    source = "void example(void) { BString text; ExampleLinkedList list; text.len; list.head->value; }\n"
    rules = ast_rules_for(source)
    assert 2 == rules.count("no-collection-internals")


def test_does_not_report_unprotected_struct_field_names(ast_rules_for):
    rules = ast_rules_for("Ext2Read read;\nreturn read.data + read.count;\n")
    assert "no-collection-internals" not in rules


def test_reports_collection_internals_for_function_parameters(ast_rules_for):
    source = "void example(ConstBStringPtr text, ExampleLinkedList * list) { text->len; list->head; }\n"
    rules = ast_rules_for(source)
    assert 2 == rules.count("no-collection-internals")


def test_reports_collection_internals_for_macro_defined_alias(ast_rules_for):
    source = "FixedArrayDefine(BString, 4, CommandArguments, bstring_equals);\nvoid example(CommandArguments args) { args.len; }\n"
    rules = ast_rules_for(source)
    assert 1 == rules.count("no-collection-internals")


def test_does_not_report_unknown_nested_member_access(ast_rules_for):
    source = "void example(Wrapper wrapper) { wrapper.text.len; }\n"
    rules = ast_rules_for(source)
    assert "no-collection-internals" not in rules


def test_reports_atomic_fixed_array_storage_access(ast_rules_for):
    source = (
        "AtomicFixedArrayDefine(uint64_t, 4, Slots);\n"
        "void example(SlotsPtr slots) { slots->data[0] = 7; }\n"
        "uint64_t read_slot(ConstSlotsPtr slots) { return slots->data[0]; }\n"
    )
    assert 2 == ast_rules_for(source).count("no-collection-internals")


def test_does_not_record_initializer_arguments_as_collection_symbols(ast_rules_for):
    source = (
        "void example(Context * context, BlockContext * block_context) { "
        "BString message = BString__sprintf(context, block_context); "
        "context->buffer; block_context->data; message.len; }\n"
    )
    rules = ast_rules_for(source)
    assert 1 == rules.count("no-collection-internals")


def test_reports_zero_initializer_followed_by_field_assignments(ast_rules_for):
    source = (
        "void example(Node * node) {\n"
        "    DirectoryEntry entry = (DirectoryEntry) { 0 };\n"
        "    entry.attributes = node->attributes;\n"
        "    entry.mode = node->mode;\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" in rules


def test_reports_zero_initializer_without_field_assignments(ast_rules_for):
    source = (
        "void example(void) {\n"
        "    DirectoryEntry entry = (DirectoryEntry) { 0 };\n"
        "    use(entry);\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" in rules


def test_allows_constructor_required_ignore_comment(ast_rules_for):
    source = (
        "void example(Node * node) {\n"
        "    DirectoryEntry entry = (DirectoryEntry) { 0 }; // hundelint: ignore-constructor-required\n"
        "    entry.attributes = node->attributes;\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" not in rules


def test_reports_direct_named_type_compound_literal(ast_rules_for):
    source = (
        "void example(void) {\n"
        "    Fat32fsDirectoryEntryLocation location = "
        "(Fat32fsDirectoryEntryLocation) { .sector = 1, .offset = 2 };\n"
        "    use(location);\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" in rules


def test_reports_direct_named_type_initializer_list(ast_rules_for):
    source = (
        "void example(uint64_t bus, uint64_t device) {\n"
        "    SdhciPciFunction function = { .bus = bus, .device = device, .function = 0 };\n"
        "    use(function);\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" in rules


def test_allows_direct_named_type_initializer_list_inside_constructor(ast_rules_for):
    source = (
        "SdhciPciFunction SdhciPciFunction__new(uint64_t bus, uint64_t device, uint64_t function) {\n"
        "    SdhciPciFunction result = { .bus = bus, .device = device, .function = function };\n"
        "    return result;\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" not in rules


def test_allows_direct_named_type_initializer_list_with_ignore_comment(ast_rules_for):
    source = (
        "void example(uint64_t bus, uint64_t device) {\n"
        "    SdhciPciFunction function = { .bus = bus, .device = device, .function = 0 }; "
        "// hundelint: ignore-constructor-required\n"
        "    use(function);\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" not in rules


def test_reports_direct_named_type_compound_literal_without_explicit_allowlist(ast_rules_for):
    source = (
        "void example(void) {\n"
        "    Fat32fsVolume volume = (Fat32fsVolume) { .root_cluster = 2 };\n"
        "    use(volume);\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" in rules


def test_allows_named_type_compound_literal_inside_constructor(ast_rules_for):
    source = (
        "Fat32fsDirectoryEntryLocation Fat32fsDirectoryEntryLocation__new(uint32_t sector, size_t offset) {\n"
        "    return (Fat32fsDirectoryEntryLocation) { .sector = sector, .offset = offset };\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" not in rules


def test_allows_named_type_compound_literal_inside_constructor_variant(ast_rules_for):
    source = (
        "Message Message__new_anything(uint64_t source) {\n"
        "    return (Message) { .source = source };\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" not in rules


def test_allows_named_type_compound_literal_inside_heap_constructor_variant(ast_rules_for):
    source = (
        "NodePtr Node__heap_new_with_metadata(BString name) {\n"
        "    NodePtr node = alloc();\n"
        "    *node = (Node) { .name = name };\n"
        "    return node;\n"
        "}\n"
    )
    rules = ast_rules_for(source)
    assert "constructor-required" not in rules


def test_allows_array_compound_literal(ast_rules_for):
    source = "void example(void) { char * value = (char[]) { 'o', 'k', 0 }; }\n"
    rules = ast_rules_for(source)
    assert "constructor-required" not in rules


def test_reports_non_static_member_function_without_self_first_arg(ast_rules_for):
    source = "void BString__copy(BStringPtr target, BStringPtr source) {}\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" in rules


def test_allows_non_static_member_function_with_self_first_arg(ast_rules_for):
    source = "void BString__copy(BStringPtr self, BStringPtr source) {}\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_allows_static_member_function_without_self_first_arg(ast_rules_for):
    source = "static void BString__copy(BStringPtr target, BStringPtr source) {}\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_allows_non_member_function_without_self_first_arg(ast_rules_for):
    source = "void copy(BStringPtr target, BStringPtr source) {}\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_allows_member_constructor_with_no_args(ast_rules_for):
    source = "BString BString__new() { return empty_bstr(); }\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_allows_namespaced_function_when_first_arg_is_not_member_type(ast_rules_for):
    source = "IOHandle IO__open(ConstBStringPtr path, Flags flags) { return 0; }\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_allows_namespaced_function_with_non_receiver_argument_not_named_self(ast_rules_for):
    source = "DirectoryEntryCount IO__list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries) { return 0; }\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_allows_namespaced_enum_helper_when_first_arg_is_not_pointer_like(ast_rules_for):
    source = "const char * ProcessState__to_string(ProcessState state) { return \"state\"; }\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_allows_member_first_arg_ignore_comment(ast_rules_for):
    source = "void BString__copy(BStringPtr target) { // hundelint: ignore-member-first-arg-self\n}\n"
    rules = ast_rules_for(source)
    assert "member-first-arg-self" not in rules


def test_reports_bstring_parameter_by_value(ast_rules_for):
    source = "void example(BString value) {}\n"
    rules = ast_rules_for(source)
    assert "no-struct-param-by-value" in rules


def test_reports_collection_parameter_by_value(ast_rules_for):
    source = (
        "FixedArrayDefine(BString, 4, CommandArguments, bstring_equals);\n"
        "void example(CommandArguments args) {}\n"
    )
    rules = ast_rules_for(source)
    assert "no-struct-param-by-value" in rules


def test_reports_struct_parameter_by_value(ast_rules_for):
    source = (
        "typedef struct Message Message;\n"
        "void example(Message message) {}\n"
    )
    rules = ast_rules_for(source)
    assert "no-struct-param-by-value" in rules


def test_reports_explicit_struct_parameter_by_value(ast_rules_for):
    source = "void example(struct Message message) {}\n"
    rules = ast_rules_for(source)
    assert "no-struct-param-by-value" in rules


def test_reports_optional_result_parameter_by_value(ast_rules_for):
    source = "void example(Optional(BString) value, Result(ByteCount) count) {}\n"
    rules = ast_rules_for(source)
    assert 2 == rules.count("no-struct-param-by-value")


def test_allows_struct_return_by_value(ast_rules_for):
    source = "BString example(void) { return bstr(\"ok\"); }\n"
    rules = ast_rules_for(source)
    assert "no-struct-param-by-value" not in rules


def test_allows_struct_parameters_by_pointer_typedef(ast_rules_for):
    source = "void example(ConstBStringPtr text, BStringPtr target, ConstMessagePtr message) {}\n"
    rules = ast_rules_for(source)
    assert "no-struct-param-by-value" not in rules
    assert "raw-struct-pointer" not in rules


def test_reports_raw_struct_pointer_parameter(ast_rules_for):
    source = (
        "typedef struct Message { int value; } Message;\n"
        "void example(const Message * message) {}\n"
    )
    rules = ast_rules_for(source)
    assert "raw-struct-pointer" in rules


def test_reports_raw_typedef_struct_pointer_parameter(ast_rules_for):
    source = (
        "typedef struct ServiceEvent { int type; } ServiceEvent;\n"
        "void example(const ServiceEvent * event) {}\n"
    )
    rules = ast_rules_for(source)
    assert "raw-struct-pointer" in rules


def test_reports_raw_struct_pointer_local(ast_rules_for):
    source = (
        "typedef struct Elf64Header { int entry; } Elf64Header;\n"
        "void example(void) { Elf64Header * header = get_header(); }\n"
    )
    rules = ast_rules_for(source)
    assert "raw-struct-pointer" in rules


def test_allows_struct_value_initialized_from_pointer_cast(ast_rules_for):
    source = (
        "typedef struct BString { int len; } BString;\n"
        "BString from_chars(const char * chars);\n"
        "void example(void) { BString text = from_chars((const char *) data); }\n"
    )
    rules = ast_rules_for(source)
    assert "raw-struct-pointer" not in rules


def test_reports_raw_optional_result_pointer_parameters(ast_rules_for):
    source = (
        "void example(const Optional(BString) * value, Result(ByteCount) * count) {}\n"
    )
    rules = ast_rules_for(source)
    assert 2 == rules.count("optional-result-pointer-param-macro")


def test_allows_optional_result_pointer_parameter_macros(ast_rules_for):
    source = (
        "void example(OptionalPtr(BString) value, ResultPtr(ByteCount) count) {}\n"
    )
    rules = ast_rules_for(source)
    assert "optional-result-pointer-param-macro" not in rules


def test_allows_struct_param_by_value_ignore_comment(ast_rules_for):
    source = "void example(BString value) { // hundelint: ignore-no-struct-param-by-value\n}\n"
    rules = ast_rules_for(source)
    assert "no-struct-param-by-value" not in rules


def test_allows_match_macros(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    match_optional(value,\n"
        "        some(text) { use(text); },\n"
        "        none() { return; }\n"
        "    );\n"
        "    match_result(result,\n"
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
    rules = rules_for(source)
    assert "match-branch-macros" not in rules
    assert "match-branch-format" not in rules
    assert "match-branch-block-format" not in rules


def test_allows_explicit_do_nothing_and_test_branch_macros_in_tests(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    match_optional(value,\n"
        "        some_do_nothing(),\n"
        "        none_test_fail()\n"
        "    );\n"
        "    match_result(result,\n"
        "        success_do_nothing(),\n"
        "        error_test_ok()\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source, "test/example.c")
    assert "match-branch-macros" not in rules
    assert "match-branch-format" not in rules
    assert "match-branch-block-format" not in rules
    assert "test-fail-branch-outside-tests" not in rules


def test_rejects_test_fail_branch_macros_outside_tests(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    match_optional(value,\n"
        "        some_test_fail(),\n"
        "        none_do_nothing()\n"
        "    );\n"
        "    match_result(result,\n"
        "        success_do_nothing(),\n"
        "        error_test_fail()\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 2 == rules.count("test-fail-branch-outside-tests")
    assert 2 == rules.count("match-branch-macros")


def test_rejects_error_do_nothing_and_empty_error_blocks(rules_for):
    source = (
        "void example(Result(ByteCount) result) {\n"
        "    match_result(result,\n"
        "        success_do_nothing(),\n"
        "        error_do_nothing()\n"
        "    );\n"
        "    match_result(result,\n"
        "        success_do_nothing(),\n"
        "        error(code, message) {}\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 1 == rules.count("match-branch-macros")
    assert 1 == rules.count("trivial-error-branch")


def test_rejects_trivial_error_return_break_and_continue_blocks(rules_for):
    source = (
        "void example(Result(ByteCount) result) {\n"
        "    match_result(result,\n"
        "        success_do_nothing(),\n"
        "        error(code, message) { return; }\n"
        "    );\n"
        "    match_result(result,\n"
        "        success_do_nothing(),\n"
        "        error(code, message) { break; }\n"
        "    );\n"
        "    match_result(result,\n"
        "        success_do_nothing(),\n"
        "        error(code, message) { continue; }\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 3 == rules.count("trivial-error-branch")


def test_allows_trivial_error_branch_with_explicit_ignore(rules_for):
    source = (
        "void example(Result(ByteCount) result) {\n"
        "    match_result(result, // hundelint: ignore-trivial-error-branch\n"
        "        success_do_nothing(),\n"
        "        error(code, message) { return; }\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert "match-branch-macros" not in rules
    assert "trivial-error-branch" not in rules


def test_reports_result_success_value_copied_to_outer_variable(ast_rules_for):
    source = (
        "Result(Output) example(Result(Input) input) {\n"
        "    Input saved;\n"
        "    match_result(input,\n"
        "        success(value)\n"
        "        {\n"
        "            saved = value;\n"
        "        },\n"
        "        error(code, message)\n"
        "        {\n"
        "            return Error(Output, message, code);\n"
        "        }\n"
        "    );\n"
        "    return use(saved);\n"
        "}\n"
    )
    assert "no-result-success-value-escape" in ast_rules_for(source)


def test_reports_cast_result_success_value_copied_to_outer_variable(ast_rules_for):
    source = (
        "Result(Output) example(Result(Address) input) {\n"
        "    ValuePtr saved;\n"
        "    match_result(input,\n"
        "        success(value)\n"
        "        {\n"
        "            saved = (ValuePtr)value;\n"
        "        },\n"
        "        error(code, message)\n"
        "        {\n"
        "            return Error(Output, message, code);\n"
        "        }\n"
        "    );\n"
        "    return use(saved);\n"
        "}\n"
    )
    assert "no-result-success-value-escape" in ast_rules_for(source)


def test_reports_redundant_result_success_alias_used_inside_branch(ast_rules_for):
    source = (
        "Result(Output) example(Result(Input) input) {\n"
        "    Input saved;\n"
        "    match_result(input,\n"
        "        success(value)\n"
        "        {\n"
        "            saved = value;\n"
        "            return use(saved);\n"
        "        },\n"
        "        error(code, message)\n"
        "        {\n"
        "            return Error(Output, message, code);\n"
        "        }\n"
        "    );\n"
        "    return Error(Output, unknown_error, OUTPUT_ERROR);\n"
        "}\n"
    )
    assert "no-result-success-value-escape" in ast_rules_for(source)


def test_allows_result_success_value_carried_across_loop_back_edge(ast_rules_for):
    source = (
        "Result(Output) example(Result(Input) input) {\n"
        "    Input current = initial();\n"
        "    while (should_continue(current)) {\n"
        "        match_result(input,\n"
        "            success(value)\n"
        "            {\n"
        "                current = value;\n"
        "                continue;\n"
        "            },\n"
        "            error(code, message)\n"
        "            {\n"
        "                return Error(Output, message, code);\n"
        "            }\n"
        "        );\n"
        "    }\n"
        "    return Success(Output, OUTPUT_OK);\n"
        "}\n"
    )
    assert "no-result-success-value-escape" not in ast_rules_for(source)


def test_allows_result_success_branch_that_does_work_and_returns(ast_rules_for):
    source = (
        "Result(Output) example(Result(Input) input) {\n"
        "    match_result(input,\n"
        "        success(value)\n"
        "        {\n"
        "            return use(value);\n"
        "        },\n"
        "        error(code, message)\n"
        "        {\n"
        "            return Error(Output, message, code);\n"
        "        }\n"
        "    );\n"
        "    return Error(Output, unknown_error, OUTPUT_ERROR);\n"
        "}\n"
    )
    assert "no-result-success-value-escape" not in ast_rules_for(source)


def test_allows_result_success_value_stored_on_object(ast_rules_for):
    source = (
        "Result(Output) example(StatePtr self, Result(Input) input) {\n"
        "    match_result(input,\n"
        "        success(value)\n"
        "        {\n"
        "            self->input = value;\n"
        "            return Success(Output, OUTPUT_OK);\n"
        "        },\n"
        "        error(code, message)\n"
        "        {\n"
        "            return Error(Output, message, code);\n"
        "        }\n"
        "    );\n"
        "    return Error(Output, unknown_error, OUTPUT_ERROR);\n"
        "}\n"
    )
    assert "no-result-success-value-escape" not in ast_rules_for(source)


def test_allows_result_success_value_in_non_result_function(ast_rules_for):
    source = (
        "void example(Result(Input) input) {\n"
        "    Input saved;\n"
        "    match_result(input,\n"
        "        success(value)\n"
        "        {\n"
        "            saved = value;\n"
        "        },\n"
        "        error(code, message)\n"
        "        {\n"
        "            report(code, &message);\n"
        "            return;\n"
        "        }\n"
        "    );\n"
        "    use(saved);\n"
        "}\n"
    )
    assert "no-result-success-value-escape" not in ast_rules_for(source)


def test_reports_inline_match_branch_macros(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    match_optional(value, some(text) { use(text); }, none() { return; });\n"
        "    match_result(result,\n"
        "        success(count) { use(count); }, error(code, message) { report(code); return; }\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 2 == rules.count("match-branch-format")


def test_reports_multiline_match_branch_brace_on_same_line(rules_for):
    source = (
        "void example(Result(ByteCount) result) {\n"
        "    match_result(result,\n"
        "        success(count) {\n"
        "            use(count);\n"
        "        },\n"
        "        error(code, message) { return; }\n"
        "    );\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 1 == rules.count("match-branch-block-format")


def test_reports_match_without_branch_macros(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    match_optional(value, text, { return; });\n"
        "    match_result(result, count, { use(count); }, code, message, { return; });\n"
        "    match_optional(value, some(text), { use(text); }, none(), { return; });\n"
        "    match_result(result, success(count), { use(count); }, error(code, message), { return; });\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 4 == rules.count("match-branch-macros")


def test_reports_legacy_critical_section_macros(rules_for):
    source = (
        "void example(mutex_t lock) {\n"
        "    CriticalSection(lock, value = 1;)\n"
        "    CriticalSectionTry(lock, value = 2;, value = 3;)\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 2 == rules.count("legacy-critical-section-macros")


def test_rejects_unscoped_user_print_calls(rules_for):
    source = "void example(void) { printf(\"hello\\n\"); print(\"bye\\n\"); }\n"
    rules = rules_for(source, "user/example.c")
    assert 2 == rules.count("legacy-user-print")


def test_allows_scoped_user_print_calls(rules_for):
    source = "void example(void) { IO__printf(&format); IO__print(&message); }\n"
    rules = rules_for(source, "user/example.c")
    assert "legacy-user-print" not in rules


def test_rejects_legacy_user_print_bridge(rules_for):
    source = "void print(const char * text) { IO__print(&btext); }\n"
    rules = rules_for(source, "user/lib/sys/print.c")
    assert "legacy-user-print" in rules


def test_rejects_legacy_user_printf_bridge(rules_for):
    source = "int printf(const char * format, ...) { return IO__printf(&bformat); }\n"
    rules = rules_for(source, "user/lib/sys/print.c")
    assert "legacy-user-print" in rules


def test_allows_non_user_print_calls(rules_for):
    source = "void example(void) { print(\"boot\\n\"); }\n"
    rules = rules_for(source, "arch/aarch64/boot/print.c")
    assert "legacy-user-print" not in rules


def test_reports_raw_control_flow_in_critical_section(rules_for):
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
    rules = rules_for(source)
    assert 3 == rules.count("critical-section-control-flow")


def test_allows_cs_control_flow_macros_in_critical_section(rules_for):
    source = (
        "int example(mutex_t lock) {\n"
        "    critical_section(lock,\n"
        "    {\n"
        "        if (failed) { cs_return(1); }\n"
        "        if (done) { cs_exit_then(return 2); }\n"
        "    });\n"
        "    return 0;\n"
        "}\n"
    )
    rules = rules_for(source)
    assert "critical-section-control-flow" not in rules


def test_rejects_statement_style_critical_section_return(rules_for):
    assert "critical-section-return-syntax" in rules_for("int example(void) { cs_return value; }")
    assert "critical-section-return-syntax" in rules_for("void example(void) { cs_return; }")


def test_allows_function_style_critical_section_returns(rules_for):
    source = "int example(void) { cs_return(value); } void other(void) { cs_return_void(); }"
    assert "critical-section-return-syntax" not in rules_for(source)


def test_reports_block_macro_without_semicolon(rules_for):
    source = (
        "void example(Optional(BString) value, TestList list) {\n"
        "    match_optional(value,\n"
        "        some(text) { use(text); },\n"
        "        none() {}\n"
        "    )\n"
        "    for_each_ll(TestList, list, item,\n"
        "    {\n"
        "        use(item);\n"
        "    })\n"
        "}\n"
    )
    rules = rules_for(source)
    assert 2 == rules.count("block-macro-semicolon")


def test_allows_block_macro_with_semicolon(rules_for):
    source = (
        "void example(Optional(BString) value, TestList list) {\n"
        "    match_optional(value,\n"
        "        some(text) { use(text); },\n"
        "        none() {}\n"
        "    );\n"
        "    for_each_ll(TestList, list, item,\n"
        "    {\n"
        "        use(item);\n"
        "    });\n"
        "}\n"
    )
    rules = rules_for(source)
    assert "block-macro-semicolon" not in rules


def test_allows_unwrap_macros(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    BString text = unwrap_optional(value, empty_bstr());\n"
        "    ByteCount count = unwrap_result(result, 0);\n"
        "}\n"
    )
    rules = rules_for(source)
    assert "match-branch-macros" not in rules
    assert "typed-unwrap-argument" not in rules


def test_reports_legacy_unwrap_macros(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    BString text = unwrap(BString, value, empty_bstr());\n"
        "    ByteCount count = result_unwrap(ByteCount, result, 0);\n"
        "}\n"
    )
    assert 2 == rules_for(source).count("legacy-unwrap-macro")


def test_reports_explicit_unwrap_type_arguments(rules_for):
    source = (
        "void example(Optional(BString) value, Result(ByteCount) result) {\n"
        "    BString text = unwrap_optional(BString, value, empty_bstr());\n"
        "    ByteCount count = unwrap_result(ByteCount, result, 0);\n"
        "}\n"
    )
    assert 2 == rules_for(source).count("typed-unwrap-argument")


def test_allows_collection_internals_in_type_implementations(ast_rules_for):
    source = "void example(void) { BString text; ExampleLinkedList list; text.len; list.head->value; }\n"
    rules = ast_rules_for(source, "common/types/bstring.c")
    assert "no-collection-internals" not in rules


def test_skips_newlib_files():
    files = hundelint.iter_c_files([ROOT / "user" / "lib"])
    assert not any("user/lib/newlib/" in path.as_posix() for path in files)
