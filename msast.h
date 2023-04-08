#include "PetitFAT/pff.h"

#define DISKMODE_LINEAR 0
#define DISKMODE_MSA 1

typedef struct stEntryList
{
	DIR dir;
	FILINFO fno;
	char fileWPath[MAX_PATH + 1];
	stEntryList* next;
	stEntryList* prev;
} stEntryList;

typedef struct
{
	char archname[MAX_PATH];
	FATFS fatfs;
	stEntryList* currentEntry;
	stEntryList* lastEntry;
	int mode;
	FILE* fp;
	BYTE* unpackedMsa;

	tChangeVolProc pLocChangeVol;
	tProcessDataProc pLocProcessData;
}  tArchive;