v1.04 modifications by George Nakos. Changelist:

- Added specific support for Atari ST BPB (which should be the same as MS-DOS 3 BPB, but not tested)
- Fixed bug that would write outside the FAT area when creating a new file on an empty disk
- Added code to detect if a floppy disk image is wrongly imaged (for example, a disk could be imaged as 82 track 10 sector and in reality be 82 track 9 sectors)
- Added BPB sanity checks and corrections
- Ignore high byte of BPB's reserved sector count in floppy disks to match GEMDOS
- Can handle floppy disks that have a non multiple of 16 root entry count
- Added optional timestamping of new files
- Fixed various bugs having to do with cross cluster boundary access in FAT12
- Added AHDI v3 and ICD partitioning scheme support (up to 4 partitions for AHDI, up to 10 partitions for ICD)
- Can create folders
- Can delete files and folders
- Supports high density disk images (18+ sectors per track)
- Fixed bug when adding more then 32 files on a folder
- Added volume label creation
- Fixed bug when opening a directory that has the same name as the volume label, and the volume label entry existed before the directory
- Improved above bug fix for some cases
- Forced initialisation of a temporary variable to zero in order for DFS_GetFAT to work more correctly
