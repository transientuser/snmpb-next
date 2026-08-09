# SnmpB Next Development Instructions

## Project Goal

Modernize the existing open-source SnmpB application into a modern, free,
cross-platform graphical SNMP/MIB browser while preserving the features that
make SnmpB particularly useful for network engineers.

Primary goals include:

- Preserve SnmpB's tolerant and useful MIB parsing/compiler behavior.
- Modernize the application to Qt 6.
- Replace the old build system with CMake.
- Keep Windows, Linux, and macOS portability as an architectural goal.
- Improve the user interface substantially.
- Evolve the existing Agent Profile system into a much better device/address
  management experience similar to mRemoteNG or MobaXterm.
- Maintain SNMPv1, SNMPv2c, and SNMPv3 functionality.

## Current Development Environment

Primary current development platform:

- Windows x64
- Qt 6.11.1
- MSVC v143 / compiler 19.44
- CMake
- VS Code
- GitHub repository
- Working branch: `modernize/qt6-cmake`

Do not unnecessarily require newer compiler-specific behavior that harms
cross-platform portability.

## Build Architecture

The desired long-term dependency structure is approximately:

```text
SnmpB application
    |
    +-- Qt 6 UI
    |
    +-- SnmpB application/core layer
    |
    +-- patched libsmi
    |
    +-- SNMP++
    |
    +-- required crypto/platform dependencies
```

Third-party libraries should become independent CMake targets rather than
having all source files compiled directly into the application target.

## libsmi Rules

The repository contains a patched version of libsmi.

This code is especially important because SnmpB handles real-world and
occasionally imperfect vendor MIBs better than many competing MIB browsers.

Do NOT:

- Replace libsmi casually.
- Upgrade libsmi casually.
- Regenerate parser/scanner sources without a specific reason.
- "Clean up" parser behavior merely because something looks old.
- Change MIB diagnostics without regression testing.

Preserving existing MIB behavior is more important than stylistic
modernization.

The current CMake build successfully builds:

```text
snmpb_libsmi
```

The current regression suite validates:

- SNMPv2-MIB
- IF-MIB
- MAU-MIB
- RMON2-MIB
- BRIDGE-MIB

Current expected result:

```text
100% tests passed, 0 tests failed out of 5
```

Run the complete regression suite after changes that could affect libsmi or
MIB handling.

If `ctest` is not on `PATH`, locate the CTest executable from the CMake
installation recorded by the current build configuration rather than treating
that as a project failure.

## MIB Compiler / Browser Direction

MIB handling is a core differentiating feature of SnmpB Next.

Future UI improvements should eventually make MIB problems easier to
understand, including:

- Dependency visualization
- Missing dependency identification
- Useful error/warning severity presentation
- Source line navigation
- Search
- Module load status
- Easy access to compiler diagnostics

Do not sacrifice parser tolerance simply to use a newer parser library.

## Agent / Device Management

SnmpB ALREADY contains an Agent Profile system:

- `AgentProfile`
- `AgentProfileManager`
- `agents.conf`

Do not create a second competing "address book" subsystem.

The goal is to evolve the existing Agent Profile functionality.

Desired future capabilities include:

- Hierarchical folders/groups
- Persistent device tree
- Reusable credential profiles
- Inherited SNMP settings where appropriate
- Tags
- Notes
- Optional MIB profiles
- Search/filter
- Import/export
- Easier device selection

Preserve compatibility with existing Agent Profile data where practical.

Before major UI redesign, decouple Agent Profile data from direct Qt widget
ownership so the underlying data model can evolve independently.

## UI Modernization Strategy

Do NOT rewrite the application directly into QML at this stage.

The existing application is heavily coupled to Qt Widgets.

Preferred sequence:

1. Build existing components cleanly with CMake.
2. Port existing application behavior to Qt 6 Widgets.
3. Separate application/core logic from UI ownership.
4. Modernize the interface.
5. Reevaluate whether QML provides enough benefit to justify further
   migration.

Qt Widgets is acceptable for the modernized application.

## Modernization Philosophy

Modernize incrementally.

Prefer:

- Small changes
- Independently buildable components
- Regression tests
- Behavior-preserving refactors
- Clear CMake targets
- Frequent working checkpoints

Avoid simultaneously changing:

- Compiler
- Parser behavior
- SNMP library behavior
- UI architecture
- Persistence format

when those changes can be separated.

## Testing Rules

After meaningful backend changes:

1. Build the affected target.
2. Run relevant tests.
3. Run the complete existing CTest suite when practical.
4. Report warnings separately from failures.

Do not silently suppress compiler warnings merely to make builds look clean.

Warnings involving 32-bit/64-bit assumptions should be recorded for later
review even when they do not currently break the build.

## Git Rules

Current working branch:

```text
modernize/qt6-cmake
```

Do not commit automatically unless explicitly instructed.

Do not push automatically unless explicitly instructed.

Do not merge into `main` automatically.

Keep commits focused around tested milestones.

Before beginning a substantial change, verify the working tree is clean.

## Source Preservation

Do not delete old qmake/project files merely because CMake replacements exist.

They remain useful references while the migration is underway.

Avoid large-scale formatting changes to legacy source files because they make
behavioral changes harder to review.

## Current State

Completed:

- Git/GitHub development setup
- Qt 6.11.1 environment verification
- MSVC v143 environment verification
- CMake baseline
- Qt compile/link/runtime smoke test
- Patched libsmi standalone CMake target
- Modernization of obsolete MSVC compatibility macros in libsmi
- Standalone smilint target
- Automated historical MIB parser regression suite
- 5/5 regression tests currently passing

Next planned technical milestone:

Create SNMP++ as an independent CMake target while preserving existing SnmpB
behavior and keeping its platform/crypto dependencies explicit.
