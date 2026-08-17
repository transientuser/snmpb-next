# MIB Inventory and Profiles V1

The MIB Library workspace separates two concepts:

- **Inventory** describes locally installed and authoritative-catalog modules.
- **MIB Profiles** describe which modules are visible in the working OID tree.

The Inventory table is read-only and deliberately compact. Its Origin labels map packaged
content to **Built-in**, authoritative IANA content to **IANA**, and other locally supplied
content to **Imported**. Download and failure state remains session state presented through
row icons, tooltips, the Info tab, and the status line rather than a permanent table column.

The Info tab snapshots parsed libsmi metadata into value records. Module Information shows
the declared module identity, numeric MODULE-IDENTITY OID, latest parsed revision timestamp,
organization, description, contact information, optional reference, and compact revision
history before the separate File Information/provenance section. Multiline source text is
shown by wrapping plain-text labels without flattening line breaks. Module scanning retains
descriptive clauses instead of using libsmi's `SMI_FLAG_NODESCR` discard option.
The two sections share a horizontal 3:2 stretch-weighted splitter so descriptive module text
receives more width while both panes resize with the window. Built-in file information shows
Origin, catalog revision (or `—`), filename, and local path; provider, URL, download time,
SHA-256, and routine state are hidden when they do not apply. IANA downloads retain all
available provenance fields. State is shown only for transient or exceptional conditions.

Profiles are stored in `mibs/profiles-v1.json` using schema version 2 and atomic `QSaveFile`
writes. A Custom record contains a stable UUID, display name, declared module identities,
and the standard-base option. Missing identities remain in the file so installing a module
later restores it automatically. Reading does not create or rewrite the file.

`All MIBs` and `Standards / MIB-II` are synthesized permanent profiles. `All MIBs` means
every locally available, usable declared module identity. The maintainable V1 standards
base is:

    SNMPv2-SMI, SNMPv2-TC, SNMPv2-CONF, SNMPv2-MIB,
    IF-MIB, IP-MIB, TCP-MIB, UDP-MIB, ENTITY-MIB, HOST-RESOURCES-MIB,
    BRIDGE-MIB, Q-BRIDGE-MIB, LLDP-MIB, INET-ADDRESS-MIB

Custom profiles store only intentional top-level members. Effective membership adds the
optional standards base and recursively resolved IMPORTS. Cycles and shared dependencies
use the existing dependency resolver. Required dependencies are keyed by declared module
identity before presentation, with one row carrying its Available/Missing state and whether
it came from the standards base or an imported dependency. Missing references are preserved
and reported. **Download Missing** is enabled only when the configured authoritative catalog
has an entry; it uses the existing HTTPS, validation, atomic-install, and provenance pipeline.

The browser selector changes visibility through the existing tree proxy. It does not unload
libsmi modules or rebuild parser state. A module not already loaded is loaded once when first
needed; subsequent profile switches filter the retained snapshot and avoid parser teardown.
Connection-to-profile association and device support detection are intentionally deferred.

Automatic profiles answer which MIBs apply to a product line. Beneath the configured
user-visible root, `Standards/**` and `Unassigned/**` are recursive global library material
and never create profiles. A `<Vendor>/<Product>` directory creates one Automatic profile
named `<Vendor> <Product>`; everything below Product belongs to that one profile. A
first-level folder with no child directories is a simple-product fallback only when it has
a supported MIB/PIB file directly. Automatic explicit membership comes from declared module
identities and is read-only. Dependencies augment its effective set through the same global
graph. Equivalent copies may belong to several products; different-content provider
conflicts remain global. Selecting any profile changes visibility only and never changes
runtime Wanted or actual libsmi Loaded state.
