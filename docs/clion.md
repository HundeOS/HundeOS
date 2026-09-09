# CLion code indexing

Generate the compilation database from the repository root:

```sh
make compile-commands
```

This uses a pinned version of compiledb through uv and a forced Make dry run.
It does not compile the kernel, launch QEMU, or clean build artifacts. The first
invocation requires network access to download compiledb.

In CLion, select File > Open, choose the root `compile_commands.json`, and
select Open as Project in the current window. Opening it as a text file does
not load the build model. Existing Makefile run configurations can still be
used; the database supplies code analysis, not a replacement build system.

The database records the cross-compiler's absolute path and each compilation's
actual flags. Kernel sources use `kernel`, `common`, and `common_includes`;
userspace sources use `user/lib`, `common`, `common_includes`, and the bundled
newlib headers. Headers such as `lifecycle.h` get their context from the source
files that include them. Source-root markings alone do not supply these flags.

Regenerate after adding source files or changing build flags. Then use
Tools > Compilation Database > Reload Compilation Database Project if CLion
does not reload automatically. The generated JSON is machine-local and ignored
by Git. This target covers the selected kernel platform and userspace binaries,
not the separately generated host unit-test builds.

Reference: [CLion compilation databases](https://www.jetbrains.com/help/clion/compilation-database.html).
