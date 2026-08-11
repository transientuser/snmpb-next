# Graphing v2 architecture

The disabled graph code persisted a list of graphs in `graphs.conf`. Each graph
stored a name and polling interval; numbered child groups stored up to ten
series with a display name, Agent Profile display name, protocol, pen color,
width/style, and an OID string. The Qwt prototype combined those records with
widgets, mutable `AgentProfile` lookup, synchronous polling, a timer, and Qwt
curves. Its intended history was 30 raw numeric samples. Rate, delta, counter
wrap/reset, units, and durable telemetry were not implemented.

Graphing v2 separates this into value types (`GraphDefinition`,
`GraphSeriesDefinition`, `GraphSample`, and bounded `GraphSeriesState`), a
`GraphRepository`, `GraphService`, numeric conversion, synchronous sampling
operation, and a cooperative `GraphAsyncRunner`. Definitions use stable graph,
series, and Agent Profile IDs plus canonical numeric OIDs. No graph value owns a
widget, credential store, SNMP session, `SmiNode`, or Qwt object.

The repository reads the legacy graphs array and numbered series groups. A
legacy Agent Profile name is resolved only when exactly one profile matches.
Missing or ambiguous references remain loaded and diagnosable. Loading never
rewrites the file; a service mutation writes schema version 2 and stable IDs,
while retaining the legacy name/style fields for compatibility. `agents.conf`
and credential persistence are untouched.

Each sampling plan captures a `SnmpRequestContext`. One GET is issued per
legacy series through `ISnmpTransport`; results cross the asynchronous boundary
as value-based samples. INTEGER, Gauge32, Counter32, Counter64, and TimeTicks
are plotted as raw numbers. Exceptions, nonnumeric values, timeout, SNMP error,
transport failure, missing values, and cancellation are explicit statuses.
Cancellation is cooperative: an in-flight blocking transport call may finish.
History defaults to and is bounded at 30 samples, evicting the oldest first.

Symbolic labels are display-only and may be resolved transiently from the MIB
service; the numeric OID remains authoritative, so MIB reload/unload cannot
invalidate a definition. Profile renames are harmless because profile IDs are
authoritative. Deleted profiles leave unresolved definitions. Credential
changes affect later request captures, never a request already in progress.

No supported plotting component is installed with the current Qt 6.11.1
environment. Qt Charts, Qt Graphs, and Qt Data Visualization are absent. The
legacy bundled Qwt 6.1.2 remains disabled. UI restoration therefore stops at
the presentation-backend dependency decision; no dependency was installed or
added.

Ordinary Query operations still use the established SNMP++ callback path.
Their captured request-context work already removes the most serious mutable
selection risk. Moving presentation/result construction behind structured
results would improve isolated testing, but is not required for Graphing v2 and
should be a separate behavior-preserving workstream.
