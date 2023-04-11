#ifndef JACKNIFE_H
#define JACKNIFE_H

#ifdef _WIN32
#else
#define __stdcall
#endif

#define DISKMODE_LINEAR 0
#define DISKMODE_MSA 1

// TODO: copypasta from wxchead.h, fix include ordering in hostemu.cpp or move entire file to dllmain.cpp
/* Definition of callback functions called by the DLL
Ask to swap disk for multi-volume archive */
typedef int(__stdcall *tChangeVolProc)(char *ArcName, int Mode);
/* Notify that data is processed - used for progress dialog */
typedef int(__stdcall *tProcessDataProc)(char *FileName, int Size);

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
	uint8_t *image_buffer;				// Buffer for the above
	int		file_size;					// Size of the above buffer
	bool	cached_into_ram;			// Should we just load the whole thing into RAM?
	bool	disk_geometry_does_not_match_bpb;			// See dosfs.cpp for an explanation why this even exists
	//int		image_sectors_per_track;	// Only required if the bool above is true
	int		bpb_sectors_per_track;		// Only required if the bool above is true
	int bpb_sides;
	int		unpackedMsaSize;
	int		unpackedMsaSectors;
	int		unpackedMsaSides;
	int		unpackedMsaEndTrack;
} DISK_IMAGE_INFO;

extern DISK_IMAGE_INFO disk_image;

#endif
