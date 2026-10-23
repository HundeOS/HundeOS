# Rust I/O host

The service implementation and Rust `main` entry point live in `src/lib.rs`.
Its Cargo package belongs to the workspace at `user/Cargo.toml`, and Make links
the service at its existing installed path. There is no C host implementation.

Reusable Rust OS bindings and C layout/protocol adapters live in `user/lib/hunde`.
Component policy and dispatch belong in the Rust service, not in those adapters.
This service inherits the workspace's forbidden-unsafe lint, including its tests.
