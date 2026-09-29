# GNU Bash Sources

The files under `src/` are GNU Bash 5.3 with the official patches 001 through
020 applied in order. The unmodified official patch files are retained under
`patches/`.

Upstream sources:

- `https://ftp.gnu.org/gnu/bash/bash-5.3.tar.gz`
- `https://ftp.gnu.org/gnu/bash/bash-5.3-patches/bash53-001` through
  `bash53-020`

`SHA256SUMS` records the downloaded archive and patch checksums. The Bash source
and bundled Readline are distributed under the GNU General Public License,
version 3 or later. The upstream `COPYING` files and source notices are retained.

HundeOS-specific patches and build configuration live under `user/ports/bash`.
The build copies this source into the architecture-qualified build directory
before applying those patches, leaving the vendored upstream tree unchanged.
