# Bash Port

The build copies the pinned upstream tree from `vendor/bash/src` into
`build/<arch>/bash/source`, applies the patches in `patches/`, and configures an
out-of-tree target build in `build/<arch>/bash/build`.

The initial configuration keeps bundled Readline and job control enabled. NLS,
network redirections, multibyte support, and Bash's allocator are disabled until
their HundeOS dependencies are deliberately implemented. `bash-configure`
completes today; `bash-build` is expected to expose the next missing compatibility
interface recorded in `docs/TODO.md`.
