# Upstream code provenance

This ledger records every source-level adaptation from another project. A
design idea learned during research does not require a ledger entry, but copied
or translated code, signatures, constants, layouts, or substantial structure
does.

## Required procedure

1. Confirm the exact upstream revision and license before copying anything.
2. Never copy or adapt code from a repository without an applicable license.
3. Add `SPDX-License-Identifier: GPL-3.0-only` to each newly adapted source
   file where its syntax permits comments.
4. Add a ledger row before committing the adaptation.
5. Describe local modifications sufficiently for a reviewer to distinguish
   this implementation from the upstream version.
6. Preserve upstream copyright and attribution notices.
7. Run the relevant implementation gate before merging the adaptation.

## Approved upstream

| Project | Repository | Audited commit | License | Allowed use |
| --- | --- | --- | --- | --- |
| NKHook5 | <https://github.com/NKHook/NKHook5> | `6bcac69de5b76bf2bed49e5db600841bfb42ccb2` | GPL-3.0 | Copy or adapt with attribution and ledger entry |
| BTD5-Decomp | <https://github.com/NKHook/BTD5-Decomp> | `b647016ea07c57d939db05555506cb88160ace9f` | None found | Research only; no copying or adaptation |

## Adaptation ledger

No upstream source has been copied or adapted yet.

When the first adaptation is made, replace the sentence above with rows using
this format:

| Local file | Upstream project/path | Commit | Upstream copyright | What was adapted | Local modifications |
| --- | --- | --- | --- | --- | --- |
| `path/to/local.cpp` | `NKHook5/path/to/source.cpp` | full commit hash | named holder or `NKHook5 contributors` | concise description | concise description |
