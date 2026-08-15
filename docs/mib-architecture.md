# MIB browser and loader architecture

Patched libsmi remains the authoritative parser, loader, semantic database,
and diagnostic source. Its tolerant handling and historical diagnostic output
are compatibility requirements; application adapters must not reinterpret or
filter parser output.

## Lifetime and serialization

libsmi is process-global and is not treated as thread-safe. Initialization,
module loading, path changes, diagnostic-handler changes, and shutdown remain
serialized on the application thread. A legitimate Preferences refresh may
save the current path, call `smiExit()` / `smiInit()`, restore the path and
handler, reload configured preloads, then rebuild snapshots and projections.
Incremental preferred-MIB loading does not perform that global reset and does
not modify preload settings.

`MibNode` widgets retain a canonical numeric OID rather than an `SmiNode *`.
Module presentation retains `MibModuleRecord`, and `MibViewLoader` retains
canonical module names rather than `SmiModule *` arrays. Any libsmi pointer is
therefore confined to a synchronous lookup or snapshot traversal and cannot
survive a reload.

## Value records and services

`MibModuleRecord` identifies a module by libsmi's canonical module name and
captures only presentation metadata. `MibTreeNodeRecord` retains canonical
numeric OID identity and the metadata used by the active browser. Neither
contains libsmi or widget pointers.

`MibService` is the widget-independent boundary for configured search paths,
explicit/preload loading, per-operation results, loaded-module inventory, and
tree snapshots. It does not persist settings. Existing Preferences remains
the sole owner of `mibpaths`, `mibpreloads`, and `misc/automaticloading`.
Directory order and the existing nonrecursive path scan are unchanged.

`MibLoadResult` distinguishes newly loaded, already loaded, and unavailable
modules. Warnings do not change a successful libsmi load into a failure. A
mixed request is partial only when at least one requested module is unavailable.

## Diagnostics

`MibDiagnosticCollector` temporarily installs a libsmi error callback for one
serialized load operation and records callback fields in arrival order. It
retains severity, tag, source path, line, requested module, exact message, and
operation identity. No column is invented because the callback supplies none.
Callers must restore their owning callback after capture; libsmi exposes no
API for reading the previously installed callback.

`MibDiagnosticModel` is a presentation-ready table model for severity,
module/file, line, and original diagnostic wording. The legacy editor list is
retained for this incremental milestone; it still uses error level 9 and the
existing severity colors and wording.

## Tree model, selection, and filtering

`MibTreeModel` owns a value snapshot and exposes display, numeric OID, module,
node kind, access, status, type, and searchable-text roles. Selection identity
is the numeric OID and can be restored after reload if that OID remains.
`MibTreeFilterModel` uses Qt's recursive filtering so matching descendants keep
their ancestors visible. Existing `QTreeWidget` browser behavior remains as a
compatibility projection, but it no longer owns durable libsmi pointers.

Snapshots copy descriptions because current details presentation needs them.
They are rebuilt only after load/reload, not during filtering or selection.
This avoids repeated libsmi traversal at interaction time, at the cost of one
description copy per visible node.

Table queries use libsmi node kinds rather than object names. Selecting either
a TABLE container or its ROW/entry enables **Query Table**; a TABLE selection
is resolved to its ROW definition before the existing asynchronous table runner
derives and queries the columns. COLUMN selections keep their normal object and
instance operations and are not treated as full-table queries by default.

## Compatibility gates and limitations

The unchanged smilint golden regressions for SNMPv2-MIB, IF-MIB, MAU-MIB,
RMON2-MIB, and BRIDGE-MIB remain authoritative. Parser sources and golden files
must not be regenerated or updated as part of application modernization.

Query, table, Discovery, TrapPresenter, and the editor may continue transient
OID-based libsmi lookup. The disabled Qwt graph path remains untouched. The
main-window MIB tree has not yet been visually redesigned, and editor navigation
from a diagnostic remains future work.

Normal startup must not semantically compile every configured MIB/PIB file.
It reads the persistent dependency index, performs size/mtime candidate
inspection, and loads only requested runtime modules through the indexed
identity-to-provider resolver. Full hashing, declaration refresh, dependency
closure verification, and bounded retry remain explicit **Check Dependencies**
work. A missing index produces a stale/needs-checking state, never a legacy
compile-all fallback.

## User-facing responsibilities

The legacy Modules screen is now the top-level **MIB Library**. It preserves its
Available MIB modules, Loaded MIB modules, and runtime module-information views
while owning collection-wide dependency validation. Its explicit **Check
Dependencies** action updates the shared persisted graph and refreshes the
Inventory and Profile projections; merely opening the Library never starts
that work.

**MIB Profiles** are inexpensive selections over identities already known to
the Library. Their effective membership combines explicit selections, the
optional Standards/MIB-II base, and dependency closure from the shared graph.
Editing or switching a Profile does not rescan or revalidate the collection.

**Loaded MIB modules**, within MIB Library, reports actual process-local libsmi
runtime state. It is distinct from the identity-centric Inventory and is not
reduced when a Profile hides modules from the browser.

Runtime preload requests are stored internally as declared module identities.
Legacy filename-based `mibpreloads` remain readable: each candidate filename is
projected through the dependency index to every declaration provided by that
physical file, then deduplicated with identity-based entries. Selecting one
Available file explicitly requests all its declarations. Removing a Loaded row
removes that explicit identity and reconstructs libsmi from the remaining
explicit requests, so shared dependencies remain when still required.

The explicit request set is intent, while the Loaded table is always rebuilt
from actual libsmi module enumeration. Available physical candidates are hidden
only when at least one declaration from their provider file is actually loaded.
Arrow operations reconstruct and verify the requested runtime transactionally;
failed attempts reconstruct the prior known-good loaded identity set and do not
persist the attempted request change.

Library dependency checks reuse the persisted profile signature and per-provider
`verified` state when candidate hashes and index generation are unchanged. A
no-change check performs metadata inspection and projection refresh only. New or
changed provider records lose their verification state, while unchanged records
remain reusable; runtime reconstruction occurs only after semantic work actually
loaded temporary providers.

Structured application diagnostics use local ISO-8601 timestamps with
milliseconds and an explicit numeric UTC offset. Backlog: normalize or wrap
raw libsmi callbacks and SNMP++ messages so every displayed entry ultimately
uses that same timestamp form. This remains a dedicated future logging task.
