SnmpB Next portable package
===========================

Extract the complete archive to a writable or read-only application folder and
run bin/snmpb.exe. Keep bin, plugins, share, and the documentation files
together. User preferences, profiles, credentials, graphs, logs, and other
runtime state are stored in the current user's Qt configuration location; they
are not written into this package.

The package contains bundled MIBs and PIBs under bin/mibs and bin/pibs. License
materials are under share/snmpb/licenses. See release-candidate-validation.md
for platform, configuration, compatibility, and known-limitation details.

This release is intended for release-candidate evaluation. Back up existing
SnmpB configuration before testing an upgrade, and use only lab-safe objects
for SET testing.
