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

## Compatibility gates and limitations

The unchanged smilint golden regressions for SNMPv2-MIB, IF-MIB, MAU-MIB,
RMON2-MIB, and BRIDGE-MIB remain authoritative. Parser sources and golden files
must not be regenerated or updated as part of application modernization.

Query, table, Discovery, TrapPresenter, and the editor may continue transient
OID-based libsmi lookup. The disabled Qwt graph path remains untouched. The
main-window MIB tree has not yet been visually redesigned, and editor navigation
from a diagnostic remains future work.
