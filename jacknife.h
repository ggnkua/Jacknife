#ifndef JACKNIFE_H
#define JACKNIFE_H

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
	DIRINFO di;
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
	int unpackedMsaSize;
	int unpackedMsaSectors;
	int unpackedMsaSides;
	int unpackedMsaEndTrack;
	bool pack_msa;

	tChangeVolProc pLocChangeVol;
	tProcessDataProc pLocProcessData;
}  tArchive;

#endif
