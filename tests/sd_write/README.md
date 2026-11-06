# Writable SD regression tests

`run.sh` builds headless tests for CUzeBox's host filesystem and virtual FAT16 SD implementation.

Coverage includes:

- FAT16 volume boot-sector jump, parameter block and `55 AA` signature
- existing-file writes with `SdAllowNewFiles=0`
- blocked creation/growth while the option is disabled
- create, grow, shrink, rename, delete, and root directory-slot reuse
- 4 GiB aggregate writable-SD cap, including recursive host-directory accounting
- traversal and symlink/reparse-point containment checks
- import of existing nested host directory trees as FAT16 subdirectories
- FAT16 `.` / `..` directory entries and nested file reads
- appended UZE-sized data streamed across FAT clusters, including reads after channel close/reopen
- guest-created directories where their cluster is initialized before the parent entry is linked
- guest file data written before its directory entry is linked
- nested create, rename, delete, and descendant remapping after a parent-directory rename
- a `uzedisk` folder beside the ROM as the card, writable; the ROM's folder and the user's setting without one
- variable-length VFAT savestate serialization/restoration of sparse directory sectors and nested host mappings

Regular file contents are not copied into VFAT RAM. They continue to stream directly to host files. Only FAT system metadata plus sparse guest-written/unmapped sectors and directory sectors are retained by the VFAT layer.

`python3 tools/test_sd_reinit.py` additionally covers SD command CRC checking
across CMD0 reset, including CRC-enabled bootloader handoff and rejection of a
reset packet with an invalid CRC.
