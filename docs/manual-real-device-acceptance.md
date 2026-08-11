# Manual real-device acceptance checklist

Record the application version, OS, device model/firmware, profile, and test
time. Mark every applicable item PASS, FAIL, or N/A. Use only authorized lab
devices and a deliberately safe writable object for SET.

## SNMP operations

- [ ] v1: GET returns expected value and type.
- [ ] v1: GET-NEXT advances once; WALK terminates at subtree boundary.
- [ ] v1: table view completes with correct rows and ordering.
- [ ] v1: a numeric graph series starts, samples, stops, and clears.
- [ ] v2c: GET and GET-NEXT return expected values.
- [ ] v2c: GET-BULK honors non-repeaters/max-repetitions.
- [ ] v2c: WALK and table view terminate correctly.
- [ ] v2c: authorized safe SET succeeds and reread confirms the value.
- [ ] v2c: one-series and multiple-series graphs sample correctly.
- [ ] v3 noAuthNoPriv: Query succeeds.
- [ ] v3 authNoPriv: Query succeeds and bad authentication fails safely.
- [ ] v3 authPriv: Query succeeds and bad privacy credentials fail safely.
- [ ] v3: table and Graph operate with each applicable security level.

## Device Manager and Discovery

- [ ] Selecting a device updates Query without changing its stored profile.
- [ ] Reusable community and USM credentials resolve correctly.
- [ ] Rename preserves identity/references; duplicate receives independent identity.
- [ ] Folder creation, move, persistence, and restart behavior are correct.
- [ ] Discovery scans a known small range and Stop halts further work.
- [ ] Discovery places profiles in the selected destination folder.
- [ ] Discovery-created profiles retain the intended credential binding.

## Traps, MIBs, and graphs

- [ ] Receive and render an authorized v1 trap.
- [ ] Receive and render a v2c notification.
- [ ] Receive and render a v3 notification when a test sender is available.
- [ ] Trap history limit is enforced; Clear History empties the view.
- [ ] Known notification/varbind OIDs use MIB-aware formatting.
- [ ] Bundled module loads; vendor module loads from an added path.
- [ ] Malformed module produces attributable diagnostics without a crash.
- [ ] Preferred MIB association loads the expected module.
- [ ] Graph one and multiple series, TimeTicks, Counter, and Gauge values.
- [ ] Graph Start/Stop/Clear and restart persistence work.
- [ ] Missing/unreachable agent reports failure and does not overlap polls.

## Lifecycle

- [ ] Close while a graph is running; restart shows consistent stored state.
- [ ] Close while Discovery is running; no work remains after exit.
- [ ] Close with Trap receiver active; restart can bind normally.
- [ ] No credentials, device inventory, or logs appear in the install folder.
