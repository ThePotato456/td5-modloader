# Third-party notices

Dependencies are pinned in `third_party/dependencies.lock.json`. Their source
archives are downloaded into ignored build directories and are not committed to
this repository.

| Dependency | Version | License | Intended use |
| --- | --- | --- | --- |
| Lua | 5.4.9 | MIT | Sandboxed mod scripting runtime |
| MinHook | 1.3.4 | BSD-2-Clause | Transactional x86 game hooks |
| JSON for Modern C++ | 3.12.0 | MIT | Native manifests, profiles, and symbol maps |
| miniz | 3.1.2 | MIT or Unlicense | `.btd5mod` ZIP reading and validation |
| Catch2 | 3.15.3 | BSL-1.0 | Native automated tests |

Release packaging must reproduce each dependency's required copyright and
license text. This summary does not replace the upstream license files.

## Adapted upstream code

Code adapted from NKHook5 is permitted because this project is distributed
under GPL-3.0-only. Every adaptation must be recorded in
`docs/upstream-code-provenance.md`, including the upstream path and commit.
NKHook5 copyright remains with its respective contributors.

BTD5-Decomp is not a dependency and contributes no copied or adapted source.
It had no license at the audited revision and is restricted to research and
independent validation.
