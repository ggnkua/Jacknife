#ifndef JACKNIFE_H
#define JACKNIFE_H

#ifdef _WIN32
#else
#define __stdcall
#endif

#define DISKMODE_LINEAR						0
#define DISKMODE_MSA						1
#define DISKMODE_FCOPY_CONF_ALL_SECTORS		2
#define DISKMODE_FCOPY_CONF_USED_SECTORS	3
#define DISKMODE_FCOPY_NO_CONF				4

typedef struct stEntryList
{
	DIRENT de;
	char fileWPath[MAX_PATH + 1];
	char filename_canonical[13];
	stEntryList* next;
	stEntryList* prev;
} stEntryList;

typedef struct
{
	char archname[MAX_PATH];
	VOLINFO vi;
	stEntryList* currentEntry;
	stEntryList* lastEntry;
	int mode;
	FILE* fp;
	bool volume_dirty;
	bool pack_msa;						// Unused for now, in the future this will be a user setting to enable or disable msa packing

	tChangeVolProc pLocChangeVol;
	tProcessDataProc pLocProcessData;
}  tArchive;

typedef struct DISK_IMAGE_INFO_
{
	FILE	*file_handle;				// references host-side image file
	uint8_t *buffer;					// Buffer for the above
	int		file_size;					// Size of the above buffer
	bool	cached_into_ram;			// Should we just load the whole thing into RAM?
	bool	disk_geometry_does_not_match_bpb;			// See dosfs.cpp for an explanation why this even exists
	int		bpb_sectors_per_track;		// Only required if the bool above is true
	int		bpb_sides;					// Same
	int		image_sectors;				// Derived value from image
	int		image_sides;				// Derived value from image
	int		image_tracks;				// Derived value from image
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

#endif
