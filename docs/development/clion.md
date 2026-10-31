# CLion code indexing

Generate the compilation database from the repository root:

```sh
make compile-commands
```

This uses the project's pinned compiledb dependency through uv and checked,
forced Make dry runs. It does not compile, configure/install Newlib, regenerate
Bash configuration, launch QEMU, or clean build artifacts. The first invocation
requires network access to install the Python tool dependencies.

Bash commands come from its existing configured Makefiles for each architecture.
Run `make bash-configure-all` first if those files are missing or compiler flags
have changed; that is a separate, normal build-preparation operation. Capture
keeps commands for generated C sources even when their generated files are not
currently present. Rust indexing uses `user/Cargo.toml`, not this C database.

`tools/compile_commands.py` checks Make's exit status before parsing or publishing
anything. It parses only compiler commands and Make directory markers, without
executing shell substitutions in build recipes. Compiler commands that still
require shell evaluation cause an explicit failure rather than being silently
omitted. The database is replaced atomically only after successful capture;
failures leave the previous database intact.

In CLion, select File > Open, choose the root `compile_commands.json`, and
select Open as Project in the current window. Opening it as a text file does
not load the build model. Existing Makefile run configurations can still be
used; the database supplies code analysis, not a replacement build system.

The database records AArch64 and RISC-V kernel/userspace builds, PIC and static
library variants, configured Bash commands, and the board-specific capture
targets listed in `compile-commands-capture`. Each variant retains its working
directory, cross-compiler's absolute path, flags, and output arguments. Only
exact duplicate commands are removed; a later architecture does not replace
earlier entries for the same source file. Kernel sources use `kernel`, `common`,
and `common_includes`; userspace
sources use `user/lib`, `common`, `common_includes`, and the bundled newlib
headers. Headers such as `lifecycle.h` get their context from the source files
that include them. Source-root markings alone do not supply these flags.

Regenerate after adding source files or changing build flags. Then use
Tools > Compilation Database > Reload Compilation Database Project if CLion
does not reload automatically. The generated JSON is machine-local and ignored
by Git. This target does not include the separately generated host unit-test
builds.

Reference: [CLion compilation databases](https://www.jetbrains.com/help/clion/compilation-database.html).
