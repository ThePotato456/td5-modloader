# Documentation wiki

Welcome to BTD5 Mod Loader. Start with the path that matches what you want to do.
These pages describe the implemented API on **Steam Win32 4.8**, with **loader
API 1** and **Lua 5.4**. The project is pre-alpha; see
[compatibility and limits](compatibility.md) before use. You do not need C++ or
a loader build to write Lua mods.

![Manager screenshot showing installed mods, the selected profile, package details, and launch controls](assets/manager-preview.png)

*Captured manager window; the package versions shown may differ from your
release. The guides use Mermaid diagrams, Markdown tables, and real sample artwork.*

## Play with mods

1. [Install the loader and your first mod](getting-started.md).
2. [Manage packages, profiles, settings, and updates](manager-revamp.md).
3. [Find a problem and recover](troubleshooting.md).
4. [Check compatibility and save behavior](compatibility.md).

## Write a mod

1. [Create, package, validate, and run your first mod](mod-authoring.md).
2. [Choose from ten runnable examples](../samples/README.md).
3. [Learn lifecycle, events, configuration, storage, and timers](../lua-api/README.md).
4. [Look up API signatures](../lua-api/reference.md) and
   [event payloads, cancellation, and object lifetimes](gameplay-events.md).
5. [Add a custom tower, weapons, upgrades, art, and sounds](custom-towers.md).
6. [Check manifest fields, dependencies, and archive rules](mod-packages.md).
7. [Prepare a mod for sharing](mod-authoring.md#share-a-mod).

## Develop the loader

| Guide | Use it for |
| --- | --- |
| [Contributing](../CONTRIBUTING.md) | Workflow, review expectations, and validation |
| [Build and test](building.md) | Toolchain, staging, offline tests, and local acceptance |
| [Architecture](architecture.md) | Bootstrap, manager, runtime, hooks, and source map |
| [Native internals](native-internals.md) | Build-specific pointers, object layouts, calling conventions, and code flow |
| [Storage and ownership](manager-storage.md) | Files, installation records, launch tickets, and private data |
| [Release process](releasing.md) | Release gates, checksums, and publishing |
| [Symbol maps](../symbols/README.md) | Supported-build evidence and compatibility data |
| [Third-party notices](../THIRD_PARTY_NOTICES.md) | Dependency licenses and provenance |
| [Upstream research](upstream-research.md) and [code provenance](upstream-code-provenance.md) | Research sources, reuse permissions, and source attribution |
| [Visual assets](assets/README.md) | Screenshot provenance, PNG/GIF artwork, and Markdown charts |

## What is available

The loader supports isolated Lua mods, deterministic dependency ordering,
per-profile configuration, private storage, English localization, text resources,
frame timers, 28 gameplay events, validated object methods, custom towers and
in-match sound effects. See [compatibility and limits](compatibility.md) for
supported operations and restrictions.

General asset caching, expanded localization, network enforcement, automatic
save backups, and crash recovery are not implemented. Steam Offline Mode and
the manager acknowledgement do not implement technical network blocking.
