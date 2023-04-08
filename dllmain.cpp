//TODO: "If PK_CAPS_HIDE is set, the plugin will not show the file type as a packer. This is useful for plugins which are mainly used for creating files, e.g. to create batch files, avi files etc. The file needs to be opened with Ctrl+PgDn in this case, because Enter will launch the associated application."
//    ==>altho this would require a second build with different filenames etc - the "gibberish extension"-solution is clumsy, but easier \o/
#include <stdio.h>
#include <cstdlib>
#include "wcxhead.h"
#include "PetitFAT/pff.h"
#include "msast.h"

stEntryList entryList;

tArchive* pCurrentArchive;

typedef tArchive* myHANDLE;

stEntryList* findLastEntry() {
	stEntryList* entry = &entryList;
	while (entry->next != NULL) {
		entry = entry->next;
	}
	return entry;
}

FRESULT scan_files(char* path)
{
	FRESULT res;
	int i;
	DIR dir;
	stEntryList* lastEntry;
	res = pf_opendir(&dir, path);
	if (res == FR_OK) {
		i = strlen(path);
		for (;;) {
			lastEntry = findLastEntry();
			res = pf_readdir(&dir, &(*lastEntry).fno);
			if (res != FR_OK || lastEntry->fno.fname[0] == 0) break;
			if (lastEntry->fno.fattrib & AM_DIR) {
				//if we exceed MAX_PATH this image has a serious problem, so better bail out
				if (strlen(path) + strlen(lastEntry->fno.fname) + 1 >= MAX_PATH ||
					sprintf_s(lastEntry->fileWPath, MAX_PATH, "%s\\%s", path, lastEntry->fno.fname)==-1) {
					return FR_NO_FILESYSTEM;
				}
				if (i + strlen(lastEntry->fno.fname) + 1 >= MAX_PATH ||
					sprintf_s(&path[i], MAX_PATH - i, "\\%s", lastEntry->fno.fname)==-1) {
					return FR_NO_FILESYSTEM;
				}
				lastEntry->next = new stEntryList();
				lastEntry->next->prev = lastEntry;
				lastEntry->next->next = NULL;
				lastEntry->next->dir = dir;
				lastEntry = lastEntry->next;
				res = scan_files(path);
				if (res != FR_OK) break;
				path[i] = 0;
			}
			else {
				//printf("%s/%s\n", path, lastEntry->fno.fname);
				//if we exceed MAX_PATH this image has a serious problem, so better bail out
				if ( strlen(path)+strlen(lastEntry->fno.fname)+1>=MAX_PATH ||
					 sprintf_s(lastEntry->fileWPath, MAX_PATH, "%s\\%s", path, lastEntry->fno.fname) == -1) {
					return FR_NO_FILESYSTEM;
				}
				lastEntry->next = new stEntryList();
				lastEntry->next->prev = lastEntry;
				lastEntry->next->next = NULL;
				lastEntry->next->dir = dir;
				lastEntry = lastEntry->next;
			}
		}
	}

	return res;
}

//unpack MSA into a newly created buffer
BYTE* unpack_msa(const char* file) {
	FILE* fp;
	fopen_s(&fp,file, "rb");
	if (fp == NULL) {
		return NULL;
	}
	fseek(fp, 0, SEEK_END);
	int packedSize = ftell(fp);
	if (packedSize <= 0) {
		return NULL;
	}
	fseek(fp, 0, SEEK_SET);

	BYTE* packedMsa = (BYTE*)malloc(packedSize);
	size_t ret = fread(packedMsa, 1, packedSize, fp);
	fclose(fp);

	if (packedMsa[0] != 0x0e || packedMsa[1] != 0x0f) {
		free(packedMsa);
		return NULL;
	}
	int sectors = ((int)packedMsa[2] << 8) | ((int)packedMsa[3]);
	int sides = (((int)packedMsa[4] << 8) | ((int)packedMsa[5])) + 1;
	int startTrack = ((int)packedMsa[6] << 8) | ((int)packedMsa[7]);
	int endTrack = ((int)packedMsa[8] << 8) | ((int)packedMsa[9]);
	//just ignore partial disk images, skipping tracks would skip bpb/fat, too
	if (startTrack != 0 || endTrack == 0) {
		free(packedMsa);
		return NULL;
	}
	int unpackedSize = sectors * 512 * sides * (endTrack + 1);
	BYTE* unpackedData = (BYTE*)malloc(unpackedSize);

	int offset = 10;
	int out = 0;
	for (int i = 0; i < (endTrack + 1) * sides; i++) {
		int trackLen = packedMsa[offset++];
		trackLen <<= 8;
		trackLen += packedMsa[offset++];
		if (trackLen != 512 * sectors) {
			for (; trackLen > 0; trackLen--) {
				unpackedData[out++] = packedMsa[offset++];
				if (unpackedData[out - 1] == 0xe5) {
					BYTE data = packedMsa[offset++];
					unsigned int runLen = packedMsa[offset++];
					runLen <<= 8;
					runLen += packedMsa[offset++];
					trackLen -= 3;
					out--;
					for (int ii = 0; ii < runLen && out < unpackedSize; ii++) {
						unpackedData[out++] = data;
					}
				}
			}
		}
		else {
			for (; trackLen > 0; trackLen--) {
				unpackedData[out++] = packedMsa[offset++];
			}
		}
	}
	free(packedMsa);
	return unpackedData;
}

tArchive* Open(tOpenArchiveData* ArchiveData)
{
	tArchive* arch = NULL;
	DWORD result;

	ArchiveData->CmtBuf = 0;
	ArchiveData->CmtBufSize = 0;
	ArchiveData->CmtSize = 0;
	ArchiveData->CmtState = 0;

	ArchiveData->OpenResult = E_NO_MEMORY;// default error type
	if ((arch = new tArchive) == NULL)
	{
		return NULL;
	}

	// trying to open
	memset(arch, 0, sizeof(tArchive));
	strcpy_s(arch->archname,MAX_PATH, ArchiveData->ArcName);

	pCurrentArchive = arch;

	arch->mode = DISKMODE_LINEAR;
	if (strlen(arch->archname) > 4) {
		if (_strcmpi(arch->archname + strlen(arch->archname) - 4, ".msa") == 0) {
			arch->mode = DISKMODE_MSA;
			arch->unpackedMsa = unpack_msa(arch->archname);
		}
	}

	//we don't _need_ to do that during MSA mode, but, hell...
	fopen_s(&arch->fp,arch->archname, "rb");

	if (pf_mount(&arch->fatfs) != FR_OK)
	{
		ArchiveData->OpenResult = E_BAD_ARCHIVE;
		goto error;
	}
	entryList.next = NULL;
	entryList.prev = NULL;

	char path[MAX_PATH + 1];
	path[0] = 0;
	if (scan_files((char*)&path) != FR_OK) {
		arch->currentEntry = &entryList;
		arch->lastEntry = NULL;
		ArchiveData->OpenResult = E_BAD_DATA;
		return arch;
	}
	arch->currentEntry = &entryList;
	arch->lastEntry = NULL;
	ArchiveData->OpenResult = 0;// ok
	return arch;

error:
	// memory must be freed
	delete arch;
	return NULL;
};

int NextItem(tArchive* hArcData, tHeaderData* HeaderData)
{
	tArchive* arch = (tArchive*)(hArcData);
	if (arch->currentEntry->next == NULL) {
		return E_BAD_ARCHIVE;
	}
	
	strcpy_s(HeaderData->ArcName,MAX_PATH, arch->archname);
	strcpy_s(HeaderData->FileName, MAX_PATH, arch->currentEntry->fileWPath);
	/*
	0x1 Read-only file
	0x2 Hidden file
	0x4 System file
	0x8 Volume ID file
	0x10 Directory
	0x20 Archive file
	*/
	HeaderData->FileAttr = arch->currentEntry->fno.fattrib;
	//FileTime = (year - 1980) << 25 | month << 21 | day << 16 | hour << 11 | minute << 5 | second / 2;
	HeaderData->FileTime = arch->currentEntry->fno.ftime+(arch->currentEntry->fno.fdate<<16);
	HeaderData->PackSize = arch->currentEntry->fno.fsize;
	HeaderData->UnpSize = arch->currentEntry->fno.fsize;
	HeaderData->CmtBuf = 0;
	HeaderData->CmtBufSize = 0;
	HeaderData->CmtSize = 0;
	HeaderData->CmtState = 0;
	HeaderData->UnpVer = 0;
	HeaderData->Method = 0;
	HeaderData->FileCRC = 0;

	arch->lastEntry = arch->currentEntry;

	//if (arch->currentEntry->next != NULL && arch->currentEntry->next->next == NULL) {
	if (arch->currentEntry->next == NULL) {
		return E_END_ARCHIVE;
	}
	arch->currentEntry = arch->currentEntry->next;
	return 0;//ok
};

int Process(tArchive* hArcData, int Operation, char* DestPath, char* DestName)
{
	if (Operation == PK_SKIP || Operation == PK_TEST) return 0;
	tArchive* arch = (tArchive*)(hArcData);
	if (Operation == PK_EXTRACT && arch->lastEntry != NULL) {
		FRESULT res;
		res = pf_open(arch->lastEntry->fileWPath);
		if (res != FR_OK) {
			return E_EREAD;
		}
		unsigned int readLen;
		unsigned int len;
		len = arch->lastEntry->fno.fsize;
		unsigned char* buf = (BYTE*)malloc(len);
		res = pf_read(buf, len, &readLen);
		if (res != FR_OK) {
			free(buf);
			return E_EREAD;
		}
		if (DestPath == NULL) {
			//DestName contains the full pathand file nameand DestPath is NULL
			FILE* f;
			fopen_s(&f,DestName, "wb");
			if (f == NULL) {
				free(buf);
				return E_EWRITE;
			}
			int wlen = fwrite(buf, 1, len, f);
			if (wlen != len) {
				free(buf);
				fclose(f);
				return E_EWRITE;
			}
			fclose(f);
		}
		else {
			//DestName contains only the file name and DestPath the file path
			char file[MAX_PATH];
			sprintf_s(file,MAX_PATH, "%s\\%s", DestPath, DestName);
			FILE* f;
			fopen_s(&f,file, "wb");
			if (f == NULL) {
				free(buf);
				return E_EWRITE;
			}
			int wlen = fwrite(buf, 1, len, f);
			if (wlen != len) {
				free(buf);
				fclose(f);
				return E_EWRITE;
			}
			fclose(f);
		}
		free(buf);
		return 0;
	}

	return E_BAD_DATA;//ok
};

int Close(tArchive* hArcData)
{
	tArchive* arch = (tArchive*)(hArcData);
	pCurrentArchive = NULL;
	fclose(arch->fp);
	if (arch->mode == DISKMODE_MSA && arch->unpackedMsa != NULL) {
		free(arch->unpackedMsa);
	}
	//kill filelist
	stEntryList* entry = &entryList;
	while (entry->next != NULL) {
		if (entry->prev != NULL && entry->prev != &entryList) {
			delete entry->prev;
		}
		entry = entry->next;
	}
	delete arch;

	return 0;// ok
};


// OpenArchive should perform all necessary operations when an archive is to be opened
myHANDLE __stdcall OpenArchive(tOpenArchiveData* ArchiveData)
{
	return Open(ArchiveData);
}

// WinCmd calls ReadHeader to find out what files are in the archive
int __stdcall ReadHeader(myHANDLE hArcData, tHeaderData* HeaderData)
{
	return NextItem(hArcData, HeaderData);
}

// ProcessFile should unpack the specified file or test the integrity of the archive
int __stdcall ProcessFile(myHANDLE hArcData, int Operation, char* DestPath, char* DestName)
{
	return Process(hArcData, Operation, DestPath, DestName);
}

// CloseArchive should perform all necessary operations when an archive is about to be closed
int __stdcall CloseArchive(myHANDLE hArcData)
{
	return Close(hArcData);
}


// This function allows you to notify user about changing a volume when packing files
void __stdcall SetChangeVolProc(myHANDLE hArcData, tChangeVolProc pChangeVolProc)
{
//	IMG_SetCallBackVol(hArcData, pChangeVolProc);
}

// This function allows you to notify user about the progress when you un/pack files
void __stdcall SetProcessDataProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc)
{
//	IMG_SetCallBackProc(hArcData, pProcessDataProc);
}

int __stdcall GetPackerCaps() {
	return PK_CAPS_BY_CONTENT;
}

BOOL __stdcall CanYouHandleThisFile(char* FileName) {
	//we simply can't check .ST files by contents, so fake checking it by actually opening it
	//do the same for .MSA for good measure
	if ((strlen(FileName) > 3 && _strcmpi(FileName + strlen(FileName) - 3, ".st") == 0) || 
		(strlen(FileName) > 4 && _strcmpi(FileName + strlen(FileName) - 4, ".msa") == 0)) {
		tOpenArchiveData oad;
		oad.ArcName = FileName;
		tArchive* pa = Open(&oad);
		if (pa == NULL) {
			return false;
		}
		if (oad.OpenResult != 0) {
			return false;
		}
		Close(pa);
		return true;
	}
	return false;
}

// The DLL entry point
BOOL APIENTRY DllMain(HANDLE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    return TRUE;
}