#!/usr/bin/env python3
import argparse
import dataclasses
import fnmatch
import re
import sys
from pathlib import Path


CHECKED_SUFFIXES = {".c", ".h"}
DEFAULT_ROOTS = ("arch", "common", "kernel", "user")
IGNORED_DIRS = {"build", "vendor", ".git", "__pycache__"}
IGNORED_PATHS = ("user/lib/newlib/**",)


@dataclasses.dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    column: int
    rule: str
    message: str


@dataclasses.dataclass(frozen=True)
class Rule:
    name: str
    message: str
    pattern: re.Pattern
    allow_paths: tuple[str, ...] = ()


PROTECTED_COLLECTION_FIELDS = ("buffer", "len", "head", "tail", "items", "data", "capacity", "count")
PROTECTED_OPTIONAL_RESULT_FIELDS = ("present", "some", "none", "ok", "success", "error", "val", "code")
PROTECTED_COLLECTION_DECLARATION_MACRO = re.compile(
    r"\b(?P<macro>FixedArrayDefine|AtomicFixedArrayDefine|LinkedListDefine|MapDefine|RingBufferDefine)\s*\((?P<args>[^;]+)\)"
)
BLOCK_STATEMENT_MACROS = (
    "match_optional",
    "match_result",
    "match_find_result",
    "match_find_result_ptr",
    "critical_section",
    "critical_section_try",
    "for_each_ll",
    "for_each_ll_ptr",
    "for_each_fa",
    "for_each_fa_ptr",
    "for_each_atomic_fa_ptr",
    "for_each_map",
    "for_each_map_ptr",
    "for_each_bstring",
    "for_each_ring_buffer",
    "for_each_ring_buffer_ptr",
)


RULES = (
    Rule(
        name="no-null",
        message="use Optional/Result instead of NULL or pointer-zero comparisons",
        pattern=re.compile(r"\bNULL\b"),
        allow_paths=(
            "arch/**",
            "common/types/**",
            "kernel/syscall/**",
        ),
    ),
    Rule(
        name="no-goto",
        message="goto is not allowed",
        pattern=re.compile(r"\bgoto\b"),
    ),
    Rule(
        name="no-conditional-compilation-in-function",
        message="move conditional compilation to a complete function or macro definition and call it unconditionally",
        pattern=re.compile(r"(?!)"),
    ),
    Rule(
        name="no-raw-char-buffer",
        message="use BString/DynString instead of raw char buffers",
        pattern=re.compile(
            r"\b(?:unsigned\s+|signed\s+)?char\s*(?:\*+\s*[A-Za-z_][A-Za-z0-9_]*|[A-Za-z_][A-Za-z0-9_]*\s*\[)"
        ),
        allow_paths=(
            "arch/**",
            "common/types/bstring.c",
            "common/types/bstring.h",
            "common/types/dyn_string.c",
            "common/types/dyn_string.h",
            "kernel/arch/**",
            "kernel/debug.h",
            "kernel/memory_page.h",
            "kernel/panic.c",
            "kernel/panic.h",
            "kernel/print.c",
            "kernel/print.h",
            "kernel/syscall/syslog_handler.c",
            "user/lib/arch/**",
            "user/lib/sys/console.c",
            "user/lib/sys/io.c",
            "user/lib/sys/io.h",
            "user/lib/sys/print.c",
            "user/lib/sys/service.c",
            "user/lib/sys/service.h",
            "user/user.h",
            "user/drivers/virtio_keyboard/main.c",
        ),
    ),
    Rule(
        name="no-optional-result-internals",
        message="use match_optional/match_result or accessors instead of Optional/Result internals",
        pattern=re.compile(
            r"(?:\.|->)(?:present|some|none|ok|success)\b|(?:\.|->)error\s*(?:\.|->)(?:code|error)\b"
        ),
        allow_paths=("common/types/find_result.h", "common/types/optional.h", "common/types/result.h"),
    ),
    Rule(
        name="no-collection-internals",
        message="use type methods instead of direct string/list/array internals",
        pattern=re.compile(r"(?:\.|->)(?:buffer|len|head|tail|items|data|capacity|count)\b"),
        allow_paths=(
            "common/types/bstring.c",
            "common/types/bstring.h",
            "common/types/dyn_string.c",
            "common/types/dyn_string.h",
            "common/types/find_result.h",
            "common/types/fixed_array.h",
            "common/types/hash_table.h",
            "common/types/linked_list.h",
            "common/types/map.h",
            "kernel/types/ring_buffer.h",
            "user/lib/types/fixed_array.h",
            "user/lib/types/linked_list.h",
            "user/lib/types/map.h",
        ),
    ),
    Rule(
        name="no-generated-collection-types",
        message="use public collection APIs and inferred match macros instead of generated collection typedefs",
        pattern=re.compile(
            r"\b[A-Za-z_][A-Za-z0-9_]*__(?:type(?:_ptr)?(?:__optional)?|optional|value_type(?:_ptr)?|FindResult)\b"
        ),
        allow_paths=(
            "common/types/atomic_fixed_array.h",
            "common/types/fixed_array.h",
            "common/types/linked_list.h",
            "common/types/map.h",
        ),
    ),
    Rule(
        name="legacy-collection-optional-matcher",
        message="use the inferred match_optional macro",
        pattern=re.compile(r"\bmatch_collection_optional\s*\("),
    ),
    Rule(
        name="legacy-unwrap-macro",
        message="use unwrap_optional or unwrap_result",
        pattern=re.compile(r"\b(?:unwrap|result_unwrap)\s*\("),
    ),
    Rule(
        name="constructor-required",
        message="use a Type__new method instead of direct struct literals or zero-initializer plus field assignments",
        pattern=re.compile(r"$^"),
    ),
    Rule(
        name="member-first-arg-self",
        message="first argument to a non-static Type__method function must be named self when it has the Type receiver",
        pattern=re.compile(r"$^"),
    ),
    Rule(
        name="no-struct-param-by-value",
        message="pass structs, strings, and collections as const/non-const pointers instead of by value",
        pattern=re.compile(r"$^"),
    ),
    Rule(
        name="optional-result-pointer-param-macro",
        message="use OptionalPtr or ResultPtr/ConstResultPtr for Optional/Result pointer parameters",
        pattern=re.compile(r"$^"),
    ),
    Rule(
        name="raw-struct-pointer",
        message="use TypeNamePtr/ConstTypeNamePtr typedefs instead of raw struct pointers",
        pattern=re.compile(r"$^"),
    ),
    Rule(
        name="legacy-critical-section-macros",
        message="use critical_section/critical_section_try instead of CriticalSection/CriticalSectionTry",
        pattern=re.compile(r"\b(?:CriticalSection|CriticalSectionTry)\s*\("),
    ),
    Rule(
        name="legacy-user-print",
        message="use IO__print/IO__printf instead of unscoped print/printf",
        pattern=re.compile(r"\b(?:print|printf)\s*\("),
    ),
    Rule(
        name="test-fail-branch-outside-tests",
        message="use *_test_fail branch macros only in tests",
        pattern=re.compile(r"\b(?:some|none|success|error)_test_fail\s*\("),
        allow_paths=("test/**",),
    ),
    Rule(
        name="critical-section-control-flow",
        message="use cs_return or cs_exit_then for early exits inside critical_section",
        pattern=re.compile(r"(?!)"),
    ),
    Rule(
        name="match-branch-macros",
        message="use inferred match macros with success/error or some/none branches",
        pattern=re.compile(r"(?!)"),
        allow_paths=(
            "common/types/dyn_string.h",
            "common/types/find_result.h",
            "common/types/fixed_array.h",
            "common/types/optional.h",
            "common/types/result.h",
            "common/types/hashtable.h",
            "kernel/types/linked_list.h",
            "kernel/types/ring_buffer.h",
        ),
    ),
    Rule(
        name="typed-match-argument",
        message="match macros infer their payload type; remove the explicit type argument",
        pattern=re.compile(r"(?!)"),
        allow_paths=(
            "common/types/find_result.h",
            "common/types/optional.h",
            "common/types/result.h",
        ),
    ),
    Rule(
        name="typed-unwrap-argument",
        message="unwrap macros derive their value type; remove the explicit type argument",
        pattern=re.compile(r"(?!)"),
    ),
    Rule(
        name="match-branch-format",
        message="put each match branch macro on its own line",
        pattern=re.compile(r"(?!)"),
        allow_paths=(
            "common/types/dyn_string.h",
            "common/types/find_result.h",
            "common/types/fixed_array.h",
            "common/types/optional.h",
            "common/types/result.h",
            "common/types/hashtable.h",
            "kernel/types/linked_list.h",
            "kernel/types/ring_buffer.h",
        ),
    ),
    Rule(
        name="match-branch-block-format",
        message="put multiline match branch opening braces on their own line",
        pattern=re.compile(r"(?!)"),
        allow_paths=(
            "common/types/dyn_string.h",
            "common/types/fixed_array.h",
            "common/types/optional.h",
            "common/types/result.h",
            "common/types/hashtable.h",
            "kernel/types/linked_list.h",
            "kernel/types/ring_buffer.h",
        ),
    ),
    Rule(
        name="trivial-error-branch",
        message="error branches must log, trace, return an error, or use an explicit ignore",
        pattern=re.compile(r"(?!)"),
    ),
    Rule(
        name="no-result-success-value-escape",
        message="use the success payload directly instead of copying it to a redundant outer local",
        pattern=re.compile(r"(?!)"),
    ),
    Rule(
        name="block-macro-semicolon",
        message="put a semicolon after block-style macro calls",
        pattern=re.compile(r"(?!)"),
    ),
)

AST_RULE_NAMES = {
    "constructor-required",
    "member-first-arg-self",
    "no-struct-param-by-value",
    "optional-result-pointer-param-macro",
    "raw-struct-pointer",
    "no-collection-internals",
    "no-optional-result-internals",
    "no-conditional-compilation-in-function",
}
TEXT_RULES = tuple(rule for rule in RULES if rule.name not in AST_RULE_NAMES)
NO_NULL_RULE = next(rule for rule in RULES if rule.name == "no-null")
COLLECTION_RULE = next(rule for rule in RULES if rule.name == "no-collection-internals")
OPTIONAL_RESULT_RULE = next(rule for rule in RULES if rule.name == "no-optional-result-internals")
CONSTRUCTOR_RULE = next(rule for rule in RULES if rule.name == "constructor-required")
MEMBER_FIRST_ARG_RULE = next(rule for rule in RULES if rule.name == "member-first-arg-self")
STRUCT_PARAM_BY_VALUE_RULE = next(rule for rule in RULES if rule.name == "no-struct-param-by-value")
OPTIONAL_RESULT_POINTER_PARAM_RULE = next(rule for rule in RULES if rule.name == "optional-result-pointer-param-macro")
RAW_STRUCT_POINTER_RULE = next(rule for rule in RULES if rule.name == "raw-struct-pointer")
CONDITIONAL_COMPILATION_RULE = next(
    rule for rule in RULES if rule.name == "no-conditional-compilation-in-function"
)
MATCH_BRANCH_RULE = next(rule for rule in RULES if rule.name == "match-branch-macros")
MATCH_BRANCH_FORMAT_RULE = next(rule for rule in RULES if rule.name == "match-branch-format")
MATCH_BRANCH_BLOCK_FORMAT_RULE = next(rule for rule in RULES if rule.name == "match-branch-block-format")
TYPED_MATCH_ARGUMENT_RULE = next(rule for rule in RULES if rule.name == "typed-match-argument")
TYPED_UNWRAP_ARGUMENT_RULE = next(rule for rule in RULES if rule.name == "typed-unwrap-argument")
TRIVIAL_ERROR_BRANCH_RULE = next(rule for rule in RULES if rule.name == "trivial-error-branch")
RESULT_SUCCESS_VALUE_ESCAPE_RULE = next(
    rule for rule in RULES if rule.name == "no-result-success-value-escape"
)
CRITICAL_SECTION_CONTROL_FLOW_RULE = next(rule for rule in RULES if rule.name == "critical-section-control-flow")
BLOCK_MACRO_SEMICOLON_RULE = next(rule for rule in RULES if rule.name == "block-macro-semicolon")


def _load_tree_sitter():
    try:
        from tree_sitter import Language, Parser
        import tree_sitter_c
    except ImportError as exc:
        raise RuntimeError(
            "hundelint needs tree-sitter and tree-sitter-c. Install test requirements first."
        ) from exc

    language = Language(tree_sitter_c.language())
    parser = Parser()
    if hasattr(parser, "set_language"):
        parser.set_language(language)
    else:
        parser.language = language
    return parser


def _parse_c(parser, source: bytes):
    return parser.parse(source)


def _normalise(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(Path.cwd().resolve()).as_posix()
    except ValueError:
        return path.as_posix().lstrip("./")


def _is_allowed(rule: Rule, path: Path) -> bool:
    normalised = _normalise(path)
    return any(fnmatch.fnmatch(normalised, allowed) for allowed in rule.allow_paths)


def _is_ignored_path(path: Path) -> bool:
    normalised = _normalise(path)
    return any(fnmatch.fnmatch(normalised, ignored) for ignored in IGNORED_PATHS)


def _is_test_path(path: Path) -> bool:
    return fnmatch.fnmatch(_normalise(path), "test/**")


def _is_user_path(path: Path) -> bool:
    return fnmatch.fnmatch(_normalise(path), "user/**")


def _line_ignores(raw_line: str) -> set[str]:
    marker = "hundelint:"
    if marker not in raw_line:
        return set()
    comment = raw_line.split(marker, 1)[1]
    ignores = set()
    for token in re.split(r"[\s,]+", comment.strip()):
        if token.startswith("ignore-"):
            ignores.add(token.removeprefix("ignore-"))
    return ignores


def _is_const_char_match(clean_line: str, start: int) -> bool:
    prefix = clean_line[:start].rstrip()
    return prefix.endswith("const")


def _is_argv_match(clean_line: str, start: int, end: int) -> bool:
    match_text = clean_line[start:end]
    return "**" in match_text and "argv" in match_text


def _node_text(source: bytes, node) -> str:
    return source[node.start_byte:node.end_byte].decode("utf-8")


def _line_starts(source: str) -> list[int]:
    starts = [0]
    for index, char in enumerate(source):
        if char == "\n":
            starts.append(index + 1)
    return starts


def _line_column_for_offset(starts: list[int], offset: int) -> tuple[int, int]:
    low = 0
    high = len(starts) - 1
    while low <= high:
        mid = (low + high) // 2
        if starts[mid] <= offset:
            low = mid + 1
        else:
            high = mid - 1
    line_index = max(0, high)
    return line_index + 1, offset - starts[line_index] + 1


def _walk(node):
    stack = [node]
    while stack:
        current = stack.pop()
        yield current
        stack.extend(reversed(current.children))


def _is_identifier_node(node) -> bool:
    return node.type in {"identifier", "type_identifier"}


def _protected_collection_types(source: str) -> set[str]:
    names = {"BString", "ConstBString", "BStringPtr", "ConstBStringPtr", "DynString", "DynStringPtr"}
    for match in PROTECTED_COLLECTION_DECLARATION_MACRO.finditer(source):
        args = [arg.strip() for arg in match.group("args").split(",")]
        alias_index = {
            "FixedArrayDefine": 2,
            "AtomicFixedArrayDefine": 2,
            "LinkedListDefine": 1,
            "MapDefine": 3,
            "RingBufferDefine": 1,
        }[match.group("macro")]
        if len(args) > alias_index:
            alias = args[alias_index]
            names.add(alias)
            names.add(f"{alias}Ptr")
            if match.group("macro") == "AtomicFixedArrayDefine":
                names.add(f"Const{alias}Ptr")
    return names


def _normalise_type_name(type_name: str) -> str:
    return (
        type_name.replace("const ", "")
        .replace("struct ", "")
        .replace("*", "")
        .strip()
    )


def _is_protected_collection_type(type_name: str, protected_types: set[str]) -> bool:
    cleaned = _normalise_type_name(type_name)
    if cleaned.startswith("Const") and cleaned.endswith("Ptr"):
        cleaned = cleaned.removeprefix("Const")
    if cleaned in protected_types:
        return True
    if cleaned.endswith("Ptr") and cleaned.removesuffix("Ptr") in protected_types:
        return True
    return cleaned.endswith(("Array", "LinkedList", "Map", "RingBuffer"))


def _is_optional_result_type(type_name: str) -> bool:
    cleaned = _normalise_type_name(type_name)
    return (
        cleaned.startswith(("Optional(", "Result(", "_Optional(", "FAOptional(", "FAPtrOptional(", "FindResultOptional("))
        or cleaned.startswith(("Optional__", "Result_"))
        or cleaned.endswith("__optional")
    )


def _is_pointer_type(type_name: str) -> bool:
    return "*" in type_name or _normalise_type_name(type_name).endswith("Ptr")


def _is_parameter_pointer_like(node) -> bool:
    declarator = node.child_by_field_name("declarator")
    if declarator is None:
        return False
    return any(
        descendant.type in {"pointer_declarator", "abstract_pointer_declarator", "array_declarator"}
        for descendant in _walk(declarator)
    )


def _is_function_parameter(node) -> bool:
    parent = node.parent
    if parent is None or parent.type != "parameter_list":
        return False
    return parent.parent is not None and parent.parent.type == "function_declarator"


def _is_nullish_storage_type(type_name: str) -> bool:
    return _is_pointer_type(type_name) or _normalise_type_name(type_name) == "Address"


def _is_zero_literal(source: bytes, node) -> bool:
    return node.type == "number_literal" and _node_text(source, node) == "0"


def _comparison_identifier_name(source: bytes, node) -> str | None:
    if node.type == "identifier":
        return _node_text(source, node)
    if node.type in {"parenthesized_expression", "subscript_expression"}:
        for child in node.children:
            name = _comparison_identifier_name(source, child)
            if name is not None:
                return name
    return None


def _declaration_type_text(source: bytes, node) -> str:
    type_node = node.child_by_field_name("type")
    if type_node is not None:
        return _node_text(source, type_node)

    parts = []
    for child in node.children:
        if child.type in {
            "primitive_type",
            "type_identifier",
            "sized_type_specifier",
            "struct_specifier",
        }:
            parts.append(_node_text(source, child))
    return " ".join(parts)


def _declarator_identifiers(node) -> list:
    if node.type == "field_identifier":
        return []
    if node.type == "function_declarator":
        return _declarator_identifiers(node.child_by_field_name("declarator"))
    if node.type == "identifier":
        return [node]
    declarator = node.child_by_field_name("declarator")
    if declarator is not None:
        return _declarator_identifiers(declarator)
    result = []
    for child in node.children:
        result.extend(_declarator_identifiers(child))
    return result


def _record_declaration_symbols(
    source: bytes,
    node,
    type_matches,
    symbols: dict[str, str],
) -> None:
    type_name = _declaration_type_text(source, node)
    if not type_name:
        return

    declarator = node.child_by_field_name("declarator")
    if declarator is None:
        return

    for identifier in _declarator_identifiers(declarator):
        symbol_type_name = type_name
        current = identifier.parent
        while current is not None:
            if current == node:
                break
            if current.type == "pointer_declarator":
                symbol_type_name = f"{symbol_type_name} *"
                break
            if current == declarator:
                break
            current = current.parent
        if type_matches(symbol_type_name):
            symbols[_node_text(source, identifier)] = symbol_type_name


def _symbol_table(source: bytes, tree, type_matches) -> dict[str, str]:
    symbols = {}
    for node in _walk(tree.root_node):
        if node.type in {"declaration", "parameter_declaration"}:
            _record_declaration_symbols(source, node, type_matches, symbols)
    return symbols


def _enclosing_function(node):
    current = node.parent
    while current is not None:
        if current.type == "function_definition":
            return current
        current = current.parent
    return None


def _scoped_symbol_table(source: bytes, tree, node, type_matches) -> dict[str, str]:
    symbols = _symbol_table(source, tree, type_matches)
    function = _enclosing_function(node)
    if function is None:
        return symbols

    for scoped_node in _walk(function):
        if scoped_node.start_byte > node.start_byte:
            continue
        if scoped_node.type in {"declaration", "parameter_declaration"}:
            _record_declaration_symbols(source, scoped_node, type_matches, symbols)
    return symbols


def _field_declarator_identifiers(node) -> list:
    if node.type == "field_identifier":
        return [node]
    declarator = node.child_by_field_name("declarator")
    if declarator is not None:
        return _field_declarator_identifiers(declarator)
    result = []
    for child in node.children:
        result.extend(_field_declarator_identifiers(child))
    return result


def _struct_field_type_table(source: bytes, tree) -> dict[str, dict[str, str]]:
    structs = {}
    for node in _walk(tree.root_node):
        if node.type != "struct_specifier":
            continue

        name_node = next((child for child in node.children if child.type == "type_identifier"), None)
        if name_node is None:
            continue

        struct_name = _normalise_type_name(_node_text(source, name_node))
        fields = structs.setdefault(struct_name, {})
        for field in _walk(node):
            if field.type != "field_declaration":
                continue

            type_name = _declaration_type_text(source, field)
            if not type_name:
                continue

            for identifier in _field_declarator_identifiers(field):
                symbol_type_name = type_name
                current = identifier.parent
                while current is not None:
                    if current == field:
                        break
                    if current.type == "pointer_declarator":
                        symbol_type_name = f"{symbol_type_name} *"
                        break
                    current = current.parent
                fields[_node_text(source, identifier)] = symbol_type_name

    return structs


def _struct_like_type_names(source_text: str, tree) -> set[str]:
    source = source_text.encode("utf-8")
    protected_types = _protected_collection_types(source_text)
    names = {
        _normalise_type_name(type_name)
        for type_name in protected_types
        if not _normalise_type_name(type_name).endswith("Ptr")
    }

    for node in _walk(tree.root_node):
        if node.type != "type_definition":
            continue

        has_struct_type = False
        for child in node.children:
            if child.type == "struct_specifier":
                has_struct_type = True
                break
            if child.type in {"type_identifier", "macro_type_specifier"}:
                type_name = _normalise_type_name(_node_text(source, child))
                if _is_protected_collection_type(type_name, protected_types) or _is_optional_result_type(type_name):
                    has_struct_type = True
                    break

        if not has_struct_type:
            continue

        for child in node.children:
            if child.type == "type_identifier":
                type_name = _normalise_type_name(_node_text(source, child))
                if not type_name.endswith("Ptr"):
                    names.add(type_name)

    return names


def _field_expression_parts(source: bytes, node):
    field = None
    base = None
    for child in node.children:
        if child.type == "field_identifier":
            field = child
            continue
        if child.type not in {".", "->"} and base is None:
            base = child
    return base, field


def _base_symbol_name(source: bytes, node) -> str | None:
    if node is None:
        return None
    if node.type == "identifier":
        return _node_text(source, node)
    if node.type in {"parenthesized_expression", "pointer_expression"}:
        for child in node.children:
            value = _base_symbol_name(source, child)
            if value is not None:
                return value
    return None


def _field_name(source: bytes, node) -> str | None:
    if node.type != "field_expression":
        return None
    _, field = _field_expression_parts(source, node)
    if field is None:
        return None
    return _node_text(source, field)


def _is_payload_internal_access(source: bytes, node, field_name: str, nested_field_name: str | None) -> bool:
    if nested_field_name in {"some", "success"} and field_name == "val":
        return True

    parent = node.parent
    if field_name not in {"some", "success"} or parent is None or parent.type != "field_expression":
        return False

    parent_base, parent_field = _field_expression_parts(source, parent)
    return parent_base == node and parent_field is not None and _node_text(source, parent_field) == "val"


def _function_definition_is_static(node) -> bool:
    return any(
        child.type == "storage_class_specifier" and any(grandchild.type == "static" for grandchild in child.children)
        for child in node.children
    )


def _function_declarator_identifier(node):
    if node.type == "identifier":
        return node
    declarator = node.child_by_field_name("declarator")
    if declarator is not None:
        found = _function_declarator_identifier(declarator)
        if found is not None:
            return found
    for child in node.children:
        found = _function_declarator_identifier(child)
        if found is not None:
            return found
    return None


def _function_declarator_parameter_list(node):
    if node.type == "parameter_list":
        return node
    for child in node.children:
        found = _function_declarator_parameter_list(child)
        if found is not None:
            return found
    return None


def _first_parameter_identifier(node):
    parameter_list = _function_declarator_parameter_list(node)
    if parameter_list is None:
        return None

    for child in parameter_list.children:
        if child.type != "parameter_declaration":
            continue
        identifiers = _declarator_identifiers(child)
        if identifiers:
            return identifiers[-1]
        return None
    return None


def _first_parameter_declaration(node):
    parameter_list = _function_declarator_parameter_list(node)
    if parameter_list is None:
        return None

    for child in parameter_list.children:
        if child.type == "parameter_declaration":
            return child
    return None


def _first_parameter_is_member_type(source: bytes, declarator, function_name: str) -> bool:
    member_type = function_name.split("__", 1)[0]
    parameter = _first_parameter_declaration(declarator)
    if parameter is None:
        return False

    raw_type_name = _declaration_type_text(source, parameter)
    type_name = _normalise_type_name(raw_type_name)
    member_type_names = {
        f"{member_type}Ptr",
        f"Const{member_type}Ptr",
    }
    pointer_type_names = {member_type, f"Const{member_type}"}
    return type_name in member_type_names or ("*" in raw_type_name and type_name in pointer_type_names)


def _lint_member_first_arg_self(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(MEMBER_FIRST_ARG_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    findings = []

    for node in _walk(tree.root_node):
        if node.type != "function_definition" or _function_definition_is_static(node):
            continue

        declarator = node.child_by_field_name("declarator")
        if declarator is None:
            continue

        function_identifier = _function_declarator_identifier(declarator)
        if function_identifier is None:
            continue

        function_name = _node_text(source, function_identifier)
        if "__" not in function_name or function_name.startswith("__"):
            continue

        if not _first_parameter_is_member_type(source, declarator, function_name):
            continue

        first_parameter = _first_parameter_identifier(declarator)
        if first_parameter is None or _node_text(source, first_parameter) == "self":
            continue

        line_number, column = _line_column_for_offset(line_starts, first_parameter.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if MEMBER_FIRST_ARG_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=MEMBER_FIRST_ARG_RULE.name,
                message=MEMBER_FIRST_ARG_RULE.message,
            )
        )

    return findings


def _lint_conditional_compilation_in_function(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(CONDITIONAL_COMPILATION_RULE, path):
        return []

    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    findings = []

    for node in _walk(tree.root_node):
        if node.type not in {"preproc_if", "preproc_ifdef", "preproc_ifndef"}:
            continue
        if _enclosing_function(node) is None:
            continue

        line_number, column = _line_column_for_offset(line_starts, node.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if CONDITIONAL_COMPILATION_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=CONDITIONAL_COMPILATION_RULE.name,
                message=CONDITIONAL_COMPILATION_RULE.message,
            )
        )

    return findings


def _parameter_uses_struct_by_value(source: bytes, node, struct_like_types: set[str], protected_types: set[str]) -> bool:
    if node.type != "parameter_declaration":
        return False
    if _is_parameter_pointer_like(node):
        return False

    type_text = _declaration_type_text(source, node)
    if not type_text:
        return False
    if _is_pointer_type(type_text):
        return False

    type_name = _normalise_type_name(type_text)
    if type_name == "void":
        return False
    if _is_protected_collection_type(type_text, protected_types):
        return True
    if _is_optional_result_type(type_text):
        return True
    if any(child.type == "struct_specifier" for child in node.children):
        return True

    return type_name in struct_like_types


def _parameter_uses_raw_optional_result_pointer(source: bytes, node) -> bool:
    if node.type != "parameter_declaration":
        return False
    if not _is_parameter_pointer_like(node):
        return False

    type_text = _declaration_type_text(source, node)
    if not type_text:
        return False

    return re.search(r"\b(?:_?Optional|Result)\s*\(", type_text) is not None


def _declaration_uses_raw_struct_pointer(source: bytes, node, struct_like_types: set[str]) -> bool:
    if node.type not in {"declaration", "parameter_declaration"}:
        return False

    type_text = _declaration_type_text(source, node)
    if not type_text:
        return False

    type_name = _normalise_type_name(type_text)
    if type_name.endswith("Ptr"):
        return False
    if type_name not in struct_like_types:
        return False

    declarator = node.child_by_field_name("declarator")
    if declarator is None:
        return False
    if node.type == "declaration" and declarator.type == "function_declarator":
        return False

    return _declarator_uses_pointer(declarator)


def _declarator_uses_pointer(node) -> bool:
    if node is None:
        return False
    if node.type in {"pointer_declarator", "abstract_pointer_declarator"}:
        return True
    if node.type == "init_declarator":
        return _declarator_uses_pointer(node.child_by_field_name("declarator"))

    return any(_declarator_uses_pointer(child) for child in node.children)


def _lint_optional_result_pointer_params(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(OPTIONAL_RESULT_POINTER_PARAM_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    findings = []

    for node in _walk(tree.root_node):
        if not _is_function_parameter(node):
            continue
        if not _parameter_uses_raw_optional_result_pointer(source, node):
            continue

        line_number, column = _line_column_for_offset(line_starts, node.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if OPTIONAL_RESULT_POINTER_PARAM_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=OPTIONAL_RESULT_POINTER_PARAM_RULE.name,
                message=OPTIONAL_RESULT_POINTER_PARAM_RULE.message,
            )
        )

    return findings


def _lint_raw_struct_pointers(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(RAW_STRUCT_POINTER_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    struct_like_types = _struct_like_type_names(source_text, tree)
    findings = []

    for node in _walk(tree.root_node):
        if not _declaration_uses_raw_struct_pointer(source, node, struct_like_types):
            continue

        line_number, column = _line_column_for_offset(line_starts, node.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if RAW_STRUCT_POINTER_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=RAW_STRUCT_POINTER_RULE.name,
                message=RAW_STRUCT_POINTER_RULE.message,
            )
        )

    return findings


def _lint_struct_params_by_value(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(STRUCT_PARAM_BY_VALUE_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    protected_types = _protected_collection_types(source_text)
    struct_like_types = _struct_like_type_names(source_text, tree)
    findings = []

    for node in _walk(tree.root_node):
        if not _is_function_parameter(node):
            continue
        if not _parameter_uses_struct_by_value(source, node, struct_like_types, protected_types):
            continue

        line_number, column = _line_column_for_offset(line_starts, node.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if STRUCT_PARAM_BY_VALUE_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=STRUCT_PARAM_BY_VALUE_RULE.name,
                message=STRUCT_PARAM_BY_VALUE_RULE.message,
            )
        )

    return findings


def _init_declarator_value(node):
    value = node.child_by_field_name("value")
    if value is not None:
        return value

    saw_equals = False
    for child in node.children:
        if child.type == "=":
            saw_equals = True
            continue
        if saw_equals:
            return child
    return None


def _is_zero_initializer_list(source: bytes, node) -> bool:
    if node.type != "initializer_list":
        return False

    values = [
        _node_text(source, child)
        for child in node.children
        if child.type not in {"{", "}", ","}
    ]
    return values == ["0"]


def _is_zero_compound_literal(source: bytes, node) -> bool:
    if node is None or node.type != "compound_literal_expression":
        return False

    return any(_is_zero_initializer_list(source, child) for child in node.children)


def _compound_literal_type_name(source: bytes, node) -> str:
    if node.type != "compound_literal_expression":
        return ""

    for child in node.children:
        if child.type == "type_descriptor":
            return _normalise_type_name(_node_text(source, child))

    return ""


def _compound_literal_requires_constructor(source: bytes, node) -> bool:
    if node.type != "compound_literal_expression":
        return False

    for child in node.children:
        if child.type != "type_descriptor":
            continue

        type_text = _node_text(source, child)
        if "[" in type_text or "]" in type_text:
            return False

        return any(grandchild.type in {"type_identifier", "struct_specifier"} for grandchild in child.children)

    return False


def _enclosing_function_name(source: bytes, node) -> str:
    ancestor = node.parent
    while ancestor is not None and ancestor.type != "function_definition":
        ancestor = ancestor.parent

    if ancestor is None:
        return ""

    declarator = ancestor.child_by_field_name("declarator")
    if declarator is None:
        return ""

    identifier = _function_declarator_identifier(declarator)
    if identifier is None:
        return ""

    return _node_text(source, identifier)


def _is_in_constructor_function(source: bytes, node) -> bool:
    name = _enclosing_function_name(source, node)
    if "__" not in name:
        return False
    method_name = name.split("__", 1)[1]
    return "new" in method_name.split("_")


def _assignment_writes_field_on_symbol(source: bytes, node, symbol: str) -> bool:
    if node.type != "expression_statement":
        return False

    expression = next((child for child in node.children if child.type == "assignment_expression"), None)
    if expression is None:
        return False

    left = expression.child_by_field_name("left")
    if left is None:
        left = next((child for child in expression.children if child.type == "field_expression"), None)
    if left is None or left.type != "field_expression":
        return False

    base, field = _field_expression_parts(source, left)
    return field is not None and _base_symbol_name(source, base) == symbol


def _declaration_zero_initialized_symbols(source: bytes, node) -> list:
    if node.type != "declaration":
        return []

    result = []
    for child in node.children:
        if child.type != "init_declarator":
            continue

        value = _init_declarator_value(child)
        if not _is_zero_compound_literal(source, value):
            continue

        declarator = child.child_by_field_name("declarator")
        if declarator is None:
            continue

        identifiers = _declarator_identifiers(declarator)
        if len(identifiers) == 1:
            result.append(identifiers[0])

    return result


def _declaration_uses_direct_struct_initializer(source: bytes, node) -> bool:
    if node.type != "declaration":
        return False

    type_name = _normalise_type_name(_declaration_type_text(source, node))
    if not type_name:
        return False

    primitive_names = {
        "bool",
        "char",
        "double",
        "float",
        "int",
        "int8_t",
        "int16_t",
        "int32_t",
        "int64_t",
        "long",
        "short",
        "size_t",
        "uint8_t",
        "uint16_t",
        "uint32_t",
        "uint64_t",
        "unsigned",
        "void",
    }
    if type_name in primitive_names:
        return False

    for child in node.children:
        if child.type != "init_declarator":
            continue

        declarator = child.child_by_field_name("declarator")
        if declarator is None or any(descendant.type == "array_declarator" for descendant in _walk(declarator)):
            continue

        value = _init_declarator_value(child)
        if value is not None and value.type == "initializer_list":
            return True

    return False


def _lint_constructor_required(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(CONSTRUCTOR_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    findings = []

    for node in _walk(tree.root_node):
        if node.type == "compound_literal_expression":
            if _compound_literal_requires_constructor(source, node) and not _is_in_constructor_function(source, node):
                line_number, column = _line_column_for_offset(line_starts, node.start_byte)
                raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
                if CONSTRUCTOR_RULE.name in _line_ignores(raw_line):
                    continue

                findings.append(
                    Finding(
                        path=path,
                        line=line_number,
                        column=column,
                        rule=CONSTRUCTOR_RULE.name,
                        message=CONSTRUCTOR_RULE.message,
                    )
                )
            continue

        if node.type == "declaration" and _declaration_uses_direct_struct_initializer(source, node):
            if _is_in_constructor_function(source, node):
                continue

            line_number, column = _line_column_for_offset(line_starts, node.start_byte)
            raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
            if CONSTRUCTOR_RULE.name in _line_ignores(raw_line):
                continue

            findings.append(
                Finding(
                    path=path,
                    line=line_number,
                    column=column,
                    rule=CONSTRUCTOR_RULE.name,
                    message=CONSTRUCTOR_RULE.message,
                )
            )
            continue

        if node.type != "compound_statement":
            continue

        children = node.children
        for index, child in enumerate(children):
            zero_initialized = _declaration_zero_initialized_symbols(source, child)
            if not zero_initialized:
                continue

            for identifier in zero_initialized:
                symbol = _node_text(source, identifier)
                has_field_assignment = any(
                    _assignment_writes_field_on_symbol(source, following, symbol)
                    for following in children[index + 1:]
                )
                if not has_field_assignment:
                    continue

                line_number, column = _line_column_for_offset(line_starts, identifier.start_byte)
                raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
                if CONSTRUCTOR_RULE.name in _line_ignores(raw_line):
                    continue

                findings.append(
                    Finding(
                        path=path,
                        line=line_number,
                        column=column,
                        rule=CONSTRUCTOR_RULE.name,
                        message=CONSTRUCTOR_RULE.message,
                    )
                )

    return findings


def _lint_collection_internals(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(COLLECTION_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    protected_types = _protected_collection_types(source_text)
    symbols = _symbol_table(
        source,
        tree,
        lambda type_name: _is_protected_collection_type(type_name, protected_types),
    )
    findings = []

    for node in _walk(tree.root_node):
        if node.type != "field_expression":
            continue

        base, field = _field_expression_parts(source, node)
        if field is None:
            continue

        field_name = _node_text(source, field)
        if field_name not in PROTECTED_COLLECTION_FIELDS:
            continue

        base_name = _base_symbol_name(source, base)
        if base_name is None or base_name not in symbols:
            continue

        line_number, column = _line_column_for_offset(line_starts, field.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if COLLECTION_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=COLLECTION_RULE.name,
                message=COLLECTION_RULE.message,
            )
        )

    return findings


def _lint_optional_result_internals(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(OPTIONAL_RESULT_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    symbols = _symbol_table(source, tree, _is_optional_result_type)
    findings = []

    for node in _walk(tree.root_node):
        if node.type != "field_expression":
            continue

        base, field = _field_expression_parts(source, node)
        if field is None:
            continue

        field_name = _node_text(source, field)
        if field_name not in PROTECTED_OPTIONAL_RESULT_FIELDS:
            continue

        base_name = _base_symbol_name(source, base)
        nested_field_name = None
        if base_name is None and base is not None and base.type == "field_expression":
            nested_base, _ = _field_expression_parts(source, base)
            base_name = _base_symbol_name(source, nested_base)
            nested_field_name = _field_name(source, base)

        direct_internal = field_name in {"present", "some", "none", "ok", "success", "error"}
        nested_internal = nested_field_name in {"some", "success", "error"} and field_name in {"val", "code", "error"}
        payload_internal = _is_payload_internal_access(source, node, field_name, nested_field_name)
        if not payload_internal and (base_name is None or base_name not in symbols):
            continue
        if not direct_internal and not nested_internal:
            continue

        line_number, column = _line_column_for_offset(line_starts, field.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if OPTIONAL_RESULT_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=OPTIONAL_RESULT_RULE.name,
                message=OPTIONAL_RESULT_RULE.message,
            )
        )

    return findings


def _lint_pointer_zero_comparisons(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(NO_NULL_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    findings = []

    for node in _walk(tree.root_node):
        if node.type != "binary_expression":
            continue

        children = [child for child in node.children if child.type not in {"==", "!="}]
        operator = next((child for child in node.children if child.type in {"==", "!="}), None)
        if operator is None or len(children) != 2:
            continue

        left, right = children
        pointer_side = None
        if _is_zero_literal(source, left):
            pointer_side = right
        elif _is_zero_literal(source, right):
            pointer_side = left
        else:
            continue

        symbols = _scoped_symbol_table(source, tree, node, lambda type_name: True)
        identifier = _comparison_identifier_name(source, pointer_side)
        if identifier is None or identifier not in symbols or not _is_pointer_type(symbols[identifier]):
            continue

        line_number, column = _line_column_for_offset(line_starts, node.start_byte)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if NO_NULL_RULE.name in _line_ignores(raw_line):
            continue

        findings.append(
            Finding(
                path=path,
                line=line_number,
                column=column,
                rule=NO_NULL_RULE.name,
                message=NO_NULL_RULE.message,
            )
        )

    return findings


def _initializer_pair_parts(source: bytes, node):
    field_name = None
    value = None

    for child in node.children:
        if child.type == "field_designator":
            field_identifier = next((grandchild for grandchild in child.children if grandchild.type == "field_identifier"), None)
            if field_identifier is not None:
                field_name = _node_text(source, field_identifier)
            continue
        if child.type not in {"=", ","} and field_name is not None:
            value = child

    return field_name, value


def _lint_zero_null_initializers(path: Path, source_text: str, tree) -> list[Finding]:
    if _is_allowed(NO_NULL_RULE, path):
        return []

    source = source_text.encode("utf-8")
    raw_lines = source_text.splitlines()
    line_starts = _line_starts(source_text)
    struct_fields = _struct_field_type_table(source, tree)
    findings = []

    for node in _walk(tree.root_node):
        if node.type != "compound_literal_expression":
            continue

        type_name = _compound_literal_type_name(source, node)
        fields = struct_fields.get(type_name)
        if fields is None:
            continue

        for initializer_pair in _walk(node):
            if initializer_pair.type != "initializer_pair":
                continue

            field_name, value = _initializer_pair_parts(source, initializer_pair)
            if field_name is None or value is None:
                continue
            if not _is_zero_literal(source, value):
                continue
            if not _is_nullish_storage_type(fields.get(field_name, "")):
                continue

            line_number, column = _line_column_for_offset(line_starts, initializer_pair.start_byte)
            raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
            if NO_NULL_RULE.name in _line_ignores(raw_line):
                continue

            findings.append(
                Finding(
                    path=path,
                    line=line_number,
                    column=column,
                    rule=NO_NULL_RULE.name,
                    message=NO_NULL_RULE.message,
                )
            )

    return findings


def _sanitise_source(source: str) -> list[str]:
    result = []
    in_block_comment = False
    in_string = False
    in_char = False
    escaped = False
    index = 0

    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""

        if in_block_comment:
            if char == "*" and next_char == "/":
                result.extend((" ", " "))
                index += 2
                in_block_comment = False
                continue
            result.append("\n" if char == "\n" else " ")
            index += 1
            continue

        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            result.append("\n" if char == "\n" else " ")
            index += 1
            continue

        if in_char:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == "'":
                in_char = False
            result.append("\n" if char == "\n" else " ")
            index += 1
            continue

        if char == "/" and next_char == "/":
            while index < len(source) and source[index] != "\n":
                result.append(" ")
                index += 1
            continue

        if char == "/" and next_char == "*":
            result.extend((" ", " "))
            index += 2
            in_block_comment = True
            continue

        if char == '"':
            result.append(" ")
            in_string = True
            index += 1
            continue

        if char == "'":
            result.append(" ")
            in_char = True
            index += 1
            continue

        result.append(char)
        index += 1

    return "".join(result).splitlines()


def _split_macro_args(args_text: str) -> list[str]:
    return [arg for arg, _start in _split_macro_args_with_offsets(args_text)]


def _split_macro_args_with_offsets(args_text: str) -> list[tuple[str, int]]:
    args = []
    start = 0
    token_start = 0
    paren_depth = 0
    brace_depth = 0
    bracket_depth = 0

    for index, char in enumerate(args_text):
        if start == index and char.isspace():
            start += 1
            token_start = start
            continue
        if char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth -= 1
        elif char == "{":
            brace_depth += 1
        elif char == "}":
            brace_depth -= 1
        elif char == "[":
            bracket_depth += 1
        elif char == "]":
            bracket_depth -= 1
        elif char == "," and paren_depth == 0 and brace_depth == 0 and bracket_depth == 0:
            raw_arg = args_text[start:index]
            stripped = raw_arg.strip()
            args.append((stripped, token_start))
            start = index + 1
            token_start = start

    while start < len(args_text) and args_text[start].isspace():
        start += 1
    args.append((args_text[start:].strip(), start))
    return args


def _iter_macro_call_ranges(source: str, macro_name: str):
    search = f"{macro_name}("
    position = 0
    while True:
        start = source.find(search, position)
        if start == -1:
            return

        if start > 0 and (source[start - 1].isalnum() or source[start - 1] == "_"):
            position = start + 1
            continue

        open_paren = start + len(macro_name)
        depth = 0
        index = open_paren
        while index < len(source):
            char = source[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    yield start, source[open_paren + 1:index], index + 1
                    position = start + 1
                    break
            index += 1
        else:
            return


def _iter_macro_calls(source: str, macro_name: str):
    for start, args_text, _end in _iter_macro_call_ranges(source, macro_name):
        yield start, args_text


def _mask_macro_calls(source: str, macro_name: str) -> str:
    masked = list(source)
    for offset, args_text in _iter_macro_calls(source, macro_name):
        end = offset + len(macro_name) + len(args_text) + 2
        for index in range(offset, min(end, len(masked))):
            if masked[index] != "\n":
                masked[index] = " "
    return "".join(masked)


def _construct_encloses_offset(source: str, construct_start: int, offset: int) -> bool:
    brace_start = source.find("{", construct_start, offset)
    if brace_start == -1:
        return False

    depth = 1
    for char in source[brace_start + 1:offset]:
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return False
    return depth > 0


def _control_flow_has_enclosing_construct(block_text: str, offset: int, keyword: str) -> bool:
    allowed = ("for", "while", "do")
    if keyword == "break":
        allowed = allowed + ("switch",)

    for match in re.finditer(r"\b(?:for|while|do|switch)\b", block_text[:offset]):
        if match.group(0) in allowed and _construct_encloses_offset(block_text, match.start(), offset):
            return True
    return False


def _critical_section_control_flow_findings(block_text: str) -> list[tuple[int, str]]:
    try:
        parser = _load_tree_sitter()
    except RuntimeError:
        return [(match.start(), match.group(0)) for match in re.finditer(r"\breturn\b", block_text)]

    prefix = "void __hundelint(void) "
    added_block = not block_text.lstrip().startswith("{")
    leading_whitespace = len(block_text) - len(block_text.lstrip())
    if added_block:
        wrapped = f"{prefix}{{\n{block_text}\n}}"
        block_offset = len(prefix) + 2
    else:
        wrapped = prefix + block_text
        block_offset = len(prefix)

    tree = _parse_c(parser, wrapped.encode("utf-8"))
    findings = []
    for node in _walk(tree.root_node):
        if node.type == "return_statement":
            findings.append((node.start_byte - block_offset, "return"))
            continue

        if node.type not in {"break_statement", "continue_statement"}:
            continue

        keyword = node.type.removesuffix("_statement")
        source_offset = node.start_byte - block_offset
        if _control_flow_has_enclosing_construct(block_text, source_offset, keyword):
            continue

        allowed_ancestors = {"for_statement", "while_statement", "do_statement"}
        if node.type == "break_statement":
            allowed_ancestors.add("switch_statement")

        ancestor = node.parent
        allowed = False
        while ancestor is not None and ancestor.type != "function_definition":
            if ancestor.type in allowed_ancestors:
                allowed = True
                break
            ancestor = ancestor.parent

        if not allowed:
            findings.append((source_offset, keyword))

    if added_block:
        return findings

    return [(max(0, offset + leading_whitespace), keyword) for offset, keyword in findings]


def _lint_critical_section_control_flow(path: Path, source: str) -> list[Finding]:
    if _is_allowed(CRITICAL_SECTION_CONTROL_FLOW_RULE, path):
        return []

    clean_source = "\n".join(_sanitise_source(source))
    raw_lines = source.splitlines()
    line_starts = _line_starts(clean_source)
    findings = []

    for offset, args_text in _iter_macro_calls(clean_source, "critical_section"):
        args_with_offsets = _split_macro_args_with_offsets(args_text)
        if len(args_with_offsets) != 2:
            continue

        line_number, column = _line_column_for_offset(line_starts, offset)
        raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
        if CRITICAL_SECTION_CONTROL_FLOW_RULE.name in _line_ignores(raw_line):
            continue

        block_text, block_start = args_with_offsets[1]
        block_text = _mask_macro_calls(block_text, "cs_exit_then")
        block_absolute_start = offset + len("critical_section") + 1 + block_start
        for finding_offset, _keyword in _critical_section_control_flow_findings(block_text):
            match_line, match_column = _line_column_for_offset(line_starts, block_absolute_start + finding_offset)
            findings.append(
                Finding(
                    path=path,
                    line=match_line,
                    column=match_column,
                    rule=CRITICAL_SECTION_CONTROL_FLOW_RULE.name,
                    message=CRITICAL_SECTION_CONTROL_FLOW_RULE.message,
                )
            )

    return findings


def _function_at_offset_returns_result(source: str, tree, offset: int) -> bool:
    if tree is None:
        return False

    source_bytes = source.encode("utf-8")
    node = tree.root_node.descendant_for_byte_range(offset, offset + 1)
    while node is not None and node.type != "function_definition":
        node = node.parent
    if node is None:
        return False

    declarator = node.child_by_field_name("declarator")
    if declarator is None:
        return False
    function_prefix = source_bytes[node.start_byte:declarator.start_byte].decode("utf-8")
    return re.search(r"\bResult\s*\(", function_prefix) is not None


def _result_success_copy_is_redundant(source: str, tree, offset: int, macro_end: int, target: str) -> bool:
    if tree is None:
        return False

    node = tree.root_node.descendant_for_byte_range(offset, offset + 1)
    while node is not None and node.type != "function_definition":
        node = node.parent
    if node is None:
        return False

    function_node = node
    function_prefix = source[function_node.start_byte:offset]
    local_declaration = re.search(
        rf"(?m)^[ \t]*(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*"
        rf"(?:\s*\([^;\n]*\))?(?:\s*\*)?\s+{re.escape(target)}\b"
        rf"(?:\s*=\s*[^;]+)?\s*;[ \t]*$",
        function_prefix,
    )
    if local_declaration is None:
        return False

    node = tree.root_node.descendant_for_byte_range(offset, offset + 1)
    while node is not None and node != function_node:
        if node.type in {"for_statement", "while_statement", "do_statement"}:
            loop_prefix = source[node.start_byte:offset]
            if re.search(rf"\b{re.escape(target)}\b", loop_prefix) is not None:
                return False
        node = node.parent

    function_remainder = source[macro_end:function_node.end_byte]
    return re.search(rf"\b{re.escape(target)}\b", function_remainder) is None


def _lint_match_branch_macros(path: Path, source: str, tree=None) -> list[Finding]:
    branch_allowed = _is_allowed(MATCH_BRANCH_RULE, path)
    format_allowed = _is_allowed(MATCH_BRANCH_FORMAT_RULE, path)
    block_format_allowed = _is_allowed(MATCH_BRANCH_BLOCK_FORMAT_RULE, path)
    trivial_error_allowed = _is_allowed(TRIVIAL_ERROR_BRANCH_RULE, path)
    success_value_escape_allowed = _is_allowed(RESULT_SUCCESS_VALUE_ESCAPE_RULE, path)
    if (
        branch_allowed
        and format_allowed
        and block_format_allowed
        and trivial_error_allowed
        and success_value_escape_allowed
    ):
        return []

    clean_source = "\n".join(_sanitise_source(source))
    raw_lines = source.splitlines()
    line_starts = _line_starts(clean_source)
    findings = []

    success_result_branches = ("success(", "success_do_nothing(")
    error_result_branches = ("error(",)
    some_optional_branches = ("some(", "some_do_nothing(")
    none_optional_branches = ("none(", "none_do_nothing(")
    if _is_test_path(path):
        success_result_branches += ("success_test_fail(",)
        error_result_branches += ("error_test_fail(", "error_test_ok(")
        some_optional_branches += ("some_test_fail(",)
        none_optional_branches += ("none_test_fail(",)

    checks = (
        ("match_result", success_result_branches, error_result_branches),
        ("match_optional", some_optional_branches, none_optional_branches),
        ("match_find_result", some_optional_branches, none_optional_branches),
        ("match_find_result_ptr", some_optional_branches, none_optional_branches),
    )
    for macro_name, success_branches, error_branches in checks:
        for offset, args_text in _iter_macro_calls(clean_source, macro_name):
            args_with_offsets = _split_macro_args_with_offsets(args_text)
            args = [arg for arg, _start in args_with_offsets]
            branch_index = 1
            line_number, column = _line_column_for_offset(line_starts, offset)
            raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
            if (
                len(args) == 4
                and not _is_allowed(TYPED_MATCH_ARGUMENT_RULE, path)
                and TYPED_MATCH_ARGUMENT_RULE.name not in _line_ignores(raw_line)
            ):
                findings.append(
                    Finding(
                        path=path,
                        line=line_number,
                        column=column,
                        rule=TYPED_MATCH_ARGUMENT_RULE.name,
                        message=TYPED_MATCH_ARGUMENT_RULE.message,
                    )
                )
                continue
            valid = (
                len(args) == branch_index + 2
                and args[branch_index].lstrip().startswith(success_branches)
                and args[branch_index + 1].lstrip().startswith(error_branches)
            )

            if not valid and not branch_allowed and MATCH_BRANCH_RULE.name not in _line_ignores(raw_line):
                findings.append(
                    Finding(
                        path=path,
                        line=line_number,
                        column=column,
                        rule=MATCH_BRANCH_RULE.name,
                        message=MATCH_BRANCH_RULE.message,
                    )
                )

            if valid and not format_allowed and MATCH_BRANCH_FORMAT_RULE.name not in _line_ignores(raw_line):
                open_paren_offset = offset + len(macro_name)
                open_line, _open_column = _line_column_for_offset(line_starts, open_paren_offset)
                branch_lines = [
                    _line_column_for_offset(line_starts, open_paren_offset + 1 + args_with_offsets[index][1])[0]
                    for index in (branch_index, branch_index + 1)
                ]
                if branch_lines[0] <= open_line or branch_lines[1] <= open_line or branch_lines[0] == branch_lines[1]:
                    findings.append(
                        Finding(
                            path=path,
                            line=line_number,
                            column=column,
                            rule=MATCH_BRANCH_FORMAT_RULE.name,
                            message=MATCH_BRANCH_FORMAT_RULE.message,
                        )
                    )

            if valid and not block_format_allowed and MATCH_BRANCH_BLOCK_FORMAT_RULE.name not in _line_ignores(raw_line):
                for index in (branch_index, branch_index + 1):
                    branch = args[index]
                    first_line = branch.split("\n", 1)[0]
                    if "\n" in branch and "{" in first_line:
                        findings.append(
                            Finding(
                                path=path,
                                line=line_number,
                                column=column,
                                rule=MATCH_BRANCH_BLOCK_FORMAT_RULE.name,
                                message=MATCH_BRANCH_BLOCK_FORMAT_RULE.message,
                            )
                        )

            if (
                valid
                and macro_name == "match_result"
                and not trivial_error_allowed
                and TRIVIAL_ERROR_BRANCH_RULE.name not in _line_ignores(raw_line)
            ):
                error_arg = args[branch_index + 1].strip()
                if error_arg.startswith("error(") and re.search(r"\)\s*\{\s*(?:(?:return|break|continue)\s*;)?\s*\}\s*$", error_arg):
                    findings.append(
                        Finding(
                            path=path,
                            line=line_number,
                            column=column,
                            rule=TRIVIAL_ERROR_BRANCH_RULE.name,
                            message=TRIVIAL_ERROR_BRANCH_RULE.message,
                        )
                    )

            if (
                valid
                and macro_name == "match_result"
                and not success_value_escape_allowed
                and RESULT_SUCCESS_VALUE_ESCAPE_RULE.name not in _line_ignores(raw_line)
                and _function_at_offset_returns_result(source, tree, offset)
            ):
                success_arg = args[branch_index].strip()
                escaped_value = re.match(
                    r"success\s*\(\s*(?P<value>[A-Za-z_][A-Za-z0-9_]*)\s*\)\s*"
                    r"\{\s*(?P<target>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*"
                    r"(?:\([^;]+\)\s*)?(?P=value)\s*;",
                    success_arg,
                    re.DOTALL,
                )
                macro_end = offset + len(macro_name) + len(args_text) + 2
                entire_branch_is_copy = escaped_value is not None and re.fullmatch(
                    r"success\s*\(\s*(?P<value>[A-Za-z_][A-Za-z0-9_]*)\s*\)\s*"
                    r"\{\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*"
                    r"(?:\([^;]+\)\s*)?(?P=value)\s*;\s*\}",
                    success_arg,
                    re.DOTALL,
                )
                redundant_copy = escaped_value is not None and _result_success_copy_is_redundant(
                    source, tree, offset, macro_end, escaped_value.group("target")
                )
                if redundant_copy or (entire_branch_is_copy and _function_at_offset_returns_result(source, tree, offset)):
                    findings.append(
                        Finding(
                            path=path,
                            line=line_number,
                            column=column,
                            rule=RESULT_SUCCESS_VALUE_ESCAPE_RULE.name,
                            message=RESULT_SUCCESS_VALUE_ESCAPE_RULE.message,
                        )
                    )

    return findings


def _lint_unwrap_macros(path: Path, source: str) -> list[Finding]:
    clean_source = "\n".join(_sanitise_source(source))
    raw_lines = source.splitlines()
    line_starts = _line_starts(clean_source)
    findings = []

    for macro_name in ("unwrap_optional", "unwrap_result"):
        for offset, args_text in _iter_macro_calls(clean_source, macro_name):
            if len(_split_macro_args(args_text)) != 3:
                continue

            line_number, column = _line_column_for_offset(line_starts, offset)
            raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
            if TYPED_UNWRAP_ARGUMENT_RULE.name in _line_ignores(raw_line):
                continue
            findings.append(
                Finding(
                    path=path,
                    line=line_number,
                    column=column,
                    rule=TYPED_UNWRAP_ARGUMENT_RULE.name,
                    message=TYPED_UNWRAP_ARGUMENT_RULE.message,
                )
            )

    return findings


def _lint_block_macro_semicolons(path: Path, source: str) -> list[Finding]:
    if _is_allowed(BLOCK_MACRO_SEMICOLON_RULE, path):
        return []

    clean_source = "\n".join(_sanitise_source(source))
    raw_lines = source.splitlines()
    line_starts = _line_starts(clean_source)
    findings = []

    for macro_name in BLOCK_STATEMENT_MACROS:
        for offset, _args_text, end_offset in _iter_macro_call_ranges(clean_source, macro_name):
            line_number, column = _line_column_for_offset(line_starts, offset)
            raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
            if raw_line.lstrip().startswith("#"):
                continue
            if BLOCK_MACRO_SEMICOLON_RULE.name in _line_ignores(raw_line):
                continue

            next_offset = end_offset
            while next_offset < len(clean_source) and clean_source[next_offset].isspace():
                next_offset += 1
            if next_offset < len(clean_source) and clean_source[next_offset] == ";":
                continue

            findings.append(
                Finding(
                    path=path,
                    line=line_number,
                    column=column,
                    rule=BLOCK_MACRO_SEMICOLON_RULE.name,
                    message=BLOCK_MACRO_SEMICOLON_RULE.message,
                )
            )

    return findings


def lint_source(path: Path, source: str, tree=None) -> list[Finding]:
    raw_lines = source.splitlines()
    clean_lines = _sanitise_source(source)
    findings = []

    for rule in TEXT_RULES:
        if rule.name == "legacy-user-print" and not _is_user_path(path):
            continue
        if _is_allowed(rule, path):
            continue
        for line_number, clean_line in enumerate(clean_lines, start=1):
            raw_line = raw_lines[line_number - 1] if line_number <= len(raw_lines) else ""
            ignores = _line_ignores(raw_line)
            if rule.name in ignores:
                continue
            for match in rule.pattern.finditer(clean_line):
                if rule.name == "no-raw-char-buffer" and _is_const_char_match(clean_line, match.start()):
                    continue
                if rule.name == "no-raw-char-buffer" and _is_argv_match(clean_line, match.start(), match.end()):
                    continue
                findings.append(
                    Finding(
                        path=path,
                        line=line_number,
                        column=match.start() + 1,
                        rule=rule.name,
                        message=rule.message,
                    )
                )

    findings.extend(_lint_match_branch_macros(path, source, tree))
    findings.extend(_lint_unwrap_macros(path, source))
    findings.extend(_lint_critical_section_control_flow(path, source))
    findings.extend(_lint_block_macro_semicolons(path, source))

    if tree is not None:
        findings.extend(_lint_constructor_required(path, source, tree))
        findings.extend(_lint_member_first_arg_self(path, source, tree))
        findings.extend(_lint_conditional_compilation_in_function(path, source, tree))
        findings.extend(_lint_optional_result_pointer_params(path, source, tree))
        findings.extend(_lint_raw_struct_pointers(path, source, tree))
        findings.extend(_lint_struct_params_by_value(path, source, tree))
        findings.extend(_lint_pointer_zero_comparisons(path, source, tree))
        findings.extend(_lint_zero_null_initializers(path, source, tree))
        findings.extend(_lint_optional_result_internals(path, source, tree))
        findings.extend(_lint_collection_internals(path, source, tree))

    return findings


def iter_c_files(paths: list[Path]) -> list[Path]:
    files = []
    for path in paths:
        if path.is_file() and path.suffix in CHECKED_SUFFIXES:
            if not _is_ignored_path(path):
                files.append(path)
            continue
        if not path.is_dir():
            continue
        for child in path.rglob("*"):
            if any(part in IGNORED_DIRS for part in child.parts):
                continue
            if _is_ignored_path(child):
                continue
            if child.is_file() and child.suffix in CHECKED_SUFFIXES:
                files.append(child)
    return sorted(set(files))


def lint_files(
    paths: list[Path],
    use_tree_sitter: bool = True,
    require_tree_sitter: bool = False,
) -> list[Finding]:
    parser = None
    if use_tree_sitter:
        try:
            parser = _load_tree_sitter()
        except RuntimeError:
            if require_tree_sitter:
                raise
            print(
                "hundelint: tree-sitter unavailable; running text rules only",
                file=sys.stderr,
            )
    findings = []
    for path in iter_c_files(paths):
        source = path.read_text(encoding="utf-8")
        tree = None
        if parser is not None:
            tree = _parse_c(parser, source.encode("utf-8"))
        findings.extend(lint_source(path, source, tree))
    return findings


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="HundeOS C style linter")
    parser.add_argument("paths", nargs="*", default=list(DEFAULT_ROOTS))
    parser.add_argument("--strict", action="store_true", help="return non-zero when findings exist")
    parser.add_argument(
        "--no-tree-sitter",
        action="store_true",
        help="run text rules without parsing first; intended for unit tests",
    )
    parser.add_argument(
        "--require-tree-sitter",
        action="store_true",
        help="fail if tree-sitter is unavailable",
    )
    args = parser.parse_args(argv)

    try:
        findings = lint_files(
            [Path(path) for path in args.paths],
            not args.no_tree_sitter,
            args.require_tree_sitter,
        )
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 2

    for finding in findings:
        print(
            f"{finding.path}:{finding.line}:{finding.column}: "
            f"{finding.rule}: {finding.message}"
        )

    if findings:
        print(f"hundelint: {len(findings)} finding(s)")
    else:
        print("hundelint: clean")

    return 1 if args.strict and findings else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
