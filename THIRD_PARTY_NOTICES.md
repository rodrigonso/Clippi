# Third-party notices

Clippi's original contributions are licensed under the root `LICENSE` (MIT).
Upstream material retains its own copyright and license. These notices do not
claim ownership of upstream projects or their contributors' work.

## Source-derived material

| Project | Material and scope | License notice |
|---|---|---|
| [pi](https://github.com/earendil-works/pi) | Behavioral reference for the independent C++ implementation, including reused prompt-visible strings and tool guidance. No JavaScript runtime or pi package is embedded. | [MIT, copyright 2025 Mario Zechner](licenses/pi/LICENSE) |

The retained notice was checked against pi's
[`LICENSE` at a74c5da](https://github.com/earendil-works/pi/blob/a74c5da112c29466f182a03108337a488c785d76/LICENSE).
This identifies the license text, not a claim that all compatibility work
targets that old source revision.

## Statically linked CLI dependencies

| Component | Pin | License | Notice in runtime archives |
|---|---|---|---|
| [libcurl](https://curl.se/) | 8.11.1; archive SHA256 in `cmake/curl.cmake` | curl license (MIT/X-derived) | `licenses/curl/COPYING` |
| [PCRE2](https://github.com/PCRE2Project/pcre2) | 10.48; archive SHA256 in `cmake/pcre2.cmake` | BSD-3-Clause WITH PCRE2-exception | `licenses/pcre2/LICENCE.md` |

The build copies these notices from the exact hash-pinned dependency sources,
not from a separate, potentially mismatched download. PCRE2's JIT is disabled;
the separately licensed SLJIT compiler is not compiled into Clippi.

Schannel, SecureTransport, and the operating system's certificate stores are
provided by Windows or macOS, not redistributed in the archives. Release
Windows binaries use the static Microsoft C runtime; building and distributing
that runtime remains subject to the applicable Microsoft toolchain license.

## Distribution

CLI archives and installed SDKs carry this file under `share/clippi`, the
project license under `share/clippi/licenses/clippi`, and the retained pi
notice under `share/clippi/licenses`. Only CLI distributions contain libcurl
and PCRE2; the core SDK has neither.
