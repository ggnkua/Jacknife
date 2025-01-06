#ifndef JACKNIFE_H
#define JACKNIFE_H

#ifdef _WIN32
#else
#define __stdcall
#endif

#define MAX_PARTITIONS 10
#define BYTE_SWAP_LONG(a) ((unsigned short)(a>>8)|(unsigned short)(a<<8))

typedef enum
{
	DISKMODE_HARD_DISK					= 0,
	DISKMODE_LINEAR						= 1,
	DISKMODE_MSA						= 2,
	DISKMODE_FCOPY_CONF_ALL_SECTORS		= 3,
	DISKMODE_FCOPY_CONF_USED_SECTORS	= 4,
	DISKMODE_FCOPY_NO_CONF				= 5,
} disk_modes;

typedef enum
{
	J_OK                       = 0,	// These items mirror DFS_ codes
	J_EOF                      = 1, // 
	J_WRITE_PROTECTED          = 2, // 
	J_FILE_NOT_FOUND           = 3, // 
	J_PATHNAME_TOO_LARGE       = 4, // 
	J_MUST_ALLOCTE_NEW_ITEM    = 5, // 
	J_ITEM_IS_DIRECTORY        = 6, // 
	J_MALLOC_ERROR             = 7,
	J_INVALID_MSA              = 8,
	J_INVALID_DIM               = 9,
	J_INVALID_HARD_DISK_IMAGE  = 10,
	J_FAIL                     = 11,
	J_BOOTSECTOR_FILE_NOT_FOUND= 12,
	J_INVALID_BOOTSECTOR_SIZE  = 13,
	J_READ_ERROR               = 14,
	J_CANNOT_CREATE_FILE       = 15,
	J_MEMORY_ALLOCATION_ERROR  = 16,
	J_OUT_OF_DISK_SPACE        = 17,
} return_codes;

typedef struct stEntryList_
{
	DIRENT de;
	char fileWPath[MAX_PATH + 1];
	struct stEntryList_ *next;
	struct stEntryList_ *prev;
} stEntryList;

typedef struct
{
	char archname[MAX_PATH];
	VOLINFO vi[MAX_PARTITIONS];
	stEntryList* currentEntry;
	stEntryList* lastEntry;
	FILE* fp;
	BOOL volume_dirty;
	BOOL pack_msa;						// Unused for now, in the future this will be a user setting to enable or disable msa packing

	tChangeVolProc pLocChangeVol;
	tProcessDataProc pLocProcessData;
}  tArchive;

typedef struct part_info
{
	uint8_t		active;
	char		type[4];
	uint32_t	start_sector;
	uint32_t	total_sectors;
	BOOL		partition_defined;
} PART_INFO;

typedef struct DISK_IMAGE_INFO_
{
	disk_modes mode;
	FILE	*file_handle;				// references host-side image file
	uint8_t *buffer;					// Buffer for the above
	int64_t	file_size;					// Size of the above buffer
	BOOL	disk_geometry_does_not_match_bpb;			// See dosfs.cpp for an explanation why this even exists
	int		bpb_sectors_per_track;		// Only required if the bool above is true
	int		bpb_sides;					// Same
	int		image_sectors;				// Derived value from image
	int		image_sides;				// Derived value from image
	int		image_tracks;				// Derived value from image
	BOOL	image_opened_read_only;
	PART_INFO partition_info[MAX_PARTITIONS];
} DISK_IMAGE_INFO;

typedef struct FCOPY_HEADER_
{
	unsigned short magic_value;
	unsigned char  disk_configuration_present;	// 0=no, non zero=yes
	unsigned char  get_sectors;					// 0=all sectors, non zero=used sectors only
	unsigned short unknown;
	unsigned short sides;
	unsigned short sectors;
	unsigned short start_track;
	unsigned short end_track;
	unsigned short sector_size;
	unsigned short sectors_per_cluster;
	unsigned short cluster_size;
	unsigned short root_size;
	unsigned short fat1_size;
	unsigned short fat_plus_boot_size;
	unsigned short total_filesystem_sectors;
	unsigned short total_clusters;
} FCOPY_HEADER;

extern DISK_IMAGE_INFO disk_image;

typedef struct
{
	int tracks;
	int sectors;
	int sides;
} NEW_DISK_GEOMETRY;

#endif
