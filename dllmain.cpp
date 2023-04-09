//TODO: "If PK_CAPS_HIDE is set, the plugin will not show the file type as a packer. This is useful for plugins which are mainly used for creating files, e.g. to create batch files, avi files etc. The file needs to be opened with Ctrl+PgDn in this case, because Enter will launch the associated application."
//    ==>altho this would require a second build with different filenames etc - the "gibberish extension"-solution is clumsy, but easier \o/
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <cstdlib>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define PATH_SEPERATOR "\\"
#else
#define PATH_SEPERATOR "/"
#define __stdcall
#define sprintf_s(a,b,...) sprintf(a,__VA_ARGS__)
#define strcpy_s(a,b,c) strcpy(a,c)
#define fopen_s(a,b,c) fopen(b,c)
#define _strcmpi strcasecmp
#define BYTE unsigned char
#define UINT unsigned int
#define BOOL bool
#endif

#include "wcxhead.h"

#define ATARI_ST_BPB
#include "dosfs-1.03/dosfs.h"
#include "dosfs-1.03/hostemu.h"
#include "jacknife.h"

uint32_t DFS_ReadSector(uint8_t unit, uint8_t *buffer, uint32_t sector, uint32_t count)
{
	return DFS_HostReadSector(buffer, sector, count);
}
uint32_t DFS_WriteSector(uint8_t unit, uint8_t *buffer, uint32_t sector, uint32_t count)
{
	return DFS_HostWriteSector(buffer, sector, count);
}

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

void DirToCanonical(char *dest, uint8_t *src)
{
	bool added_dot = false;
	for (int i = 0; i < 11; i++)
	{
		if (*src == ' ')
		{
			do
			{
				src++;
				i++;
				if (i == 11)
				{
					*dest = 0;
					return;
				}
			} while (*src == ' ');
			*dest++ = '.';
			added_dot = true;
		}
		*dest++ = *src++;
		if (i == 7 && !added_dot && *src!=' ') *dest++ = '.';
	}
	*dest = 0;
}

uint32_t scan_files(char* path, VOLINFO *vi)
{
	uint32_t res;
	int i;
	DIRINFO di;

	uint8_t *scratch_sector=(uint8_t *)malloc(SECTOR_SIZE);
	if (!scratch_sector) return DFS_ERRMISC;

	di.scratch = scratch_sector;
	stEntryList *lastEntry;

	res = DFS_OpenDir(vi, (uint8_t *)path, &di);
	if (res == DFS_OK) {
		i = (int)strlen(path);
		for (;;) {
			lastEntry = findLastEntry();
			res = DFS_GetNext(vi, &di, &(*lastEntry).de);
			if (res != DFS_OK) break;
			if (lastEntry->de.name[0] == 0) continue;
			if (strcmp((char *)lastEntry->de.name, ".          \x10") == 0) continue;
			if (strcmp((char *)lastEntry->de.name, "..         \x10") == 0) continue;
			DirToCanonical(lastEntry->filename_canonical, lastEntry->de.name);
			if (lastEntry->de.attr & ATTR_VOLUME_ID) {
				strcpy((char *)vi->label, lastEntry->filename_canonical);
				continue;
			}
			if (lastEntry->de.attr & ATTR_DIRECTORY) {
				//if we exceed MAX_PATH this image has a serious problem, so better bail out
				if (strlen(path)+strlen(lastEntry->filename_canonical) +1 >= MAX_PATH ||
					sprintf_s((char *)lastEntry->fileWPath, MAX_PATH, "%s%s", path, lastEntry->filename_canonical) == -1) {
					free(scratch_sector);
					return DFS_ERRMISC;
				}
				if (i + strlen((char *)lastEntry->de.name) + 1 >= MAX_PATH ||
					sprintf_s(&path[i], MAX_PATH - i, "%s" PATH_SEPERATOR, lastEntry->filename_canonical)==-1) {
					free(scratch_sector);
					return DFS_ERRMISC;
				}
				lastEntry->next = new stEntryList();
				lastEntry->next->prev = lastEntry;
				lastEntry->next->next = NULL;
				lastEntry->next->di = di;
				lastEntry = lastEntry->next;
				res = scan_files(path, vi);
				di.scratch = scratch_sector;
				if (res != DFS_OK && res != DFS_EOF) break;
				path[i] = 0;
			}
			else {
				//if we exceed MAX_PATH this image has a serious problem, so better bail out
				if ( strlen(path)+strlen(lastEntry->filename_canonical)+1>=MAX_PATH ||
					 sprintf_s(lastEntry->fileWPath, MAX_PATH, "%s%s", path, lastEntry->filename_canonical) == -1) {
					free(scratch_sector);
					return DFS_ERRMISC;
				}
				lastEntry->next = new stEntryList();
				lastEntry->next->prev = lastEntry;
				lastEntry->next->next = NULL;
				lastEntry->next->di = di;
				lastEntry = lastEntry->next;
			}
		}
	}

	free(scratch_sector);

	return res;
}

//unpack MSA into a newly created buffer
BYTE* unpack_msa(tArchive *arch, uint8_t *packedMsa, int packedSize) {
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
	arch->unpackedMsaSize = unpackedSize;
	arch->unpackedMsaSectors = sectors;
	arch->unpackedMsaSides = sides;
	arch->unpackedMsaEndTrack = endTrack;
	BYTE* unpackedData = (BYTE*)malloc(unpackedSize);
	if (!unpackedData) return 0;

	int offset = 10;
	int out = 0;
	for (int i = 0; i < (endTrack + 1) * sides; i++) {
		int trackLen = packedMsa[offset++];
		trackLen <<= 8;
		trackLen += packedMsa[offset++];
		if (trackLen != 512 * sectors) {
			for (; trackLen > 0; trackLen--) {
				unpackedData[out++] = packedMsa[offset++];
				// Bounds check against corrupt MSA images
				if (out > unpackedSize || offset >= packedSize)
				{
					free(unpackedData);
					return 0;
				}
				if (unpackedData[out - 1] == 0xe5) {
					// Bounds check against corrupt MSA images
					if (offset + 4 - 1 > packedSize)
					{
						free(unpackedData);
						return 0;
					}
					BYTE data = packedMsa[offset++];
					unsigned int runLen = packedMsa[offset++];
					runLen <<= 8;
					runLen += packedMsa[offset++];
					trackLen -= 3;
					out--;
					for (unsigned int ii = 0; ii < runLen && out < unpackedSize; ii++) {
						unpackedData[out++] = data;
					}
				}
			}
		}
		else {
			// Bounds check against corrupt MSA images
			if (out + trackLen > unpackedSize || offset + trackLen >= packedSize)
			{
				free(unpackedData);
				return 0;
			}
			for (; trackLen > 0; trackLen--) {
				unpackedData[out++] = packedMsa[offset++];
			}
		}
	}
	return unpackedData;
}

tArchive* Open(tOpenArchiveData* ArchiveData)
{
	uint32_t result;
	tArchive* arch = NULL;

	ArchiveData->CmtBuf = 0;
	ArchiveData->CmtBufSize = 0;
	ArchiveData->CmtSize = 0;
	ArchiveData->CmtState = 0;

	ArchiveData->OpenResult = E_NO_MEMORY;// default error type
	if ((arch = new tArchive) == NULL)
	{
		return NULL;
	}

	arch->volume_dirty = false;

	// trying to open
	memset(arch, 0, sizeof(tArchive));
	strcpy_s(arch->archname,MAX_PATH, ArchiveData->ArcName);

	pCurrentArchive = arch;

	if (DFS_HostAttach(arch) != DFS_OK)
	{
		ArchiveData->OpenResult = E_BAD_ARCHIVE;
		goto error;
	}

	uint32_t partition_start_sector /*, partition_size*/;
	uint8_t scratch_sector[SECTOR_SIZE];
	// Obtain pointer to first partition on first (only) unit
	// TODO: this will more interesting when we add hard disk image support
	// for now we'll hardcode all the things
	// 
	//pstart = DFS_GetPtnStart(0, sector, 0, &pactive, &ptype, &psize);
	//if (pstart == 0xffffffff) {
	//	printf("Cannot find first partition\n");
	//	return -1;
	//}
	partition_start_sector = 0;

	if (DFS_GetVolInfo(0, scratch_sector, partition_start_sector, &arch->vi)) {
		//printf("Error getting volume information\n");
		return NULL;
	}

	entryList.next = NULL;
	entryList.prev = NULL;

	char path[MAX_PATH + 1];
	path[0] = 0;
	result = scan_files((char *)&path, &arch->vi);
	if (result != DFS_OK && result != DFS_EOF) {
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
	HeaderData->FileAttr = arch->currentEntry->de.attr;
	//FileTime = (year - 1980) << 25 | month << 21 | day << 16 | hour << 11 | minute << 5 | second / 2;
	HeaderData->FileTime = (arch->currentEntry->de.crttime_h << 8) | (arch->currentEntry->de.crttime_h) | (arch->currentEntry->de.crtdate_h << 24) | (arch->currentEntry->de.crtdate_h << 16);
	HeaderData->PackSize = arch->currentEntry->de.filesize_0 | (arch->currentEntry->de.filesize_1 << 8) | (arch->currentEntry->de.filesize_2 << 16) | (arch->currentEntry->de.filesize_3 << 24);
	HeaderData->UnpSize = HeaderData->PackSize;
	HeaderData->CmtBuf = 0;
	HeaderData->CmtBufSize = 0;
	HeaderData->CmtSize = 0;
	HeaderData->CmtState = 0;
	HeaderData->UnpVer = 0;
	HeaderData->Method = 0;
	HeaderData->FileCRC = 0;

	arch->lastEntry = arch->currentEntry;

	if (arch->currentEntry->next == NULL) {
		return E_END_ARCHIVE;
	}
	arch->currentEntry = arch->currentEntry->next;
	return 0;//ok
};

int Process(tArchive* hArcData, int Operation, char* DestPath, char* DestName)
{
	uint8_t scratch_sector[SECTOR_SIZE];
	if (Operation == PK_SKIP || Operation == PK_TEST) return 0;
	tArchive* arch = (tArchive*)(hArcData);
	if (Operation == PK_EXTRACT && arch->lastEntry != NULL) {
		uint32_t res;
		FILEINFO fi;
		res = DFS_OpenFile(&arch->vi, (uint8_t *)arch->lastEntry->fileWPath, DFS_READ, scratch_sector, &fi);
		if (res != DFS_OK) {
			return E_EREAD;
		}
		unsigned int readLen;
		unsigned int len;
		len = fi.filelen;
		unsigned char *buf = (BYTE *)calloc(1, len + 1024); // Allocate some extra RAM and wipe it so we don't write undefined values to the file
		res = DFS_ReadFile(&fi, scratch_sector, buf, &readLen, len);
		if (res != DFS_OK) {
			free(buf);
			return E_EREAD;
		}
		if (DestPath == NULL) {
			// DestName contains the full path and file nameand DestPath is NULL
			FILE* f;
			fopen_s(&f,DestName, "wb");
			if (f == NULL) {
				free(buf);
				return E_EWRITE;
			}
			size_t wlen = fwrite(buf, 1, len, f);
			if (wlen != len) {
				free(buf);
				fclose(f);
				return E_EWRITE;
			}
			fclose(f);
		}
		else {
			// DestName contains only the file name and DestPath the file path
			char file[MAX_PATH];
			sprintf_s(file,MAX_PATH, "%s" PATH_SEPERATOR "%s", DestPath, DestName);
			FILE* f;
			fopen_s(&f,file, "wb");
			if (f == NULL) {
				free(buf);
				return E_EWRITE;
			}
			size_t wlen = fwrite(buf, 1, len, f);
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
	DFS_HostDetach(arch);
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

int Pack(char *PackedFile, char *SubPath, char *SrcPath, char *AddList, int Flags)
{
	if (!AddList || *AddList == 0) return E_NO_FILES;
	tOpenArchiveData archive_data = { 0 };
	archive_data.ArcName = PackedFile;
	tArchive *archive_handle;
	archive_handle = Open(&archive_data);
	if (!archive_handle)
	{
		// This is what Open() returns if it reaches an error (archive_handle=NULL).
		// However, since the return sctruct is deleted before returned, we pass this error manually here
		return E_BAD_ARCHIVE;
	}
	if (archive_data.OpenResult)
	{
		return archive_data.OpenResult;
	}

	uint32_t res;
	FILEINFO fi;
	uint8_t scratch_sector[SECTOR_SIZE];
	char filename_source[MAX_PATH];
	char filename_dest[MAX_PATH];
	if (Flags & PK_PACK_SAVE_PATHS)
	{

	}
	if (SubPath)
	{
		strcpy(filename_dest, SubPath);
		strcat(filename_dest, PATH_SEPERATOR);
	}
	else
	{
		strcpy(filename_dest, PATH_SEPERATOR);
	}
	strcpy(filename_source, SrcPath);
	char *filename_subpath = filename_source + strlen(filename_source);
	char *current_file = AddList;
	while (*current_file) // Each string in AddList is zero-delimited (ends in zero), and the AddList string ends with an extra zero byte, i.e. there are two zero bytes at the end of AddList.
	{
		strcpy(filename_subpath, current_file);
		FILE *handle_to_add=fopen(filename_source, "rb");
		if (!handle_to_add)
		{
			return E_NO_FILES;
		}
		fseek(handle_to_add, 0, SEEK_END);
		int file_size = ftell(handle_to_add);
		if (file_size < 0) {
			return E_NO_FILES;
		}
		unsigned char *read_buf = (unsigned char *)calloc(1, file_size + 1024); // Allocate some extra RAM and wipe it so we don't write undefined values to the file
		if (!read_buf)
		{
			Close(archive_handle);
			return E_NO_MEMORY;
		}
		fseek(handle_to_add, 0, SEEK_SET);
		size_t items_read = fread(read_buf, file_size, 1, handle_to_add);
		if (!items_read)
		{
			fclose(handle_to_add);
			free(read_buf);
			Close(archive_handle);
			return E_EREAD;
		}

		fclose(handle_to_add);
		strcpy(&filename_dest[1], current_file);
		res = DFS_OpenFile(&archive_handle->vi, (uint8_t *)filename_dest, DFS_WRITE, scratch_sector, &fi);
		if (res != DFS_OK) {
			free(read_buf);
			Close(archive_handle);
			return E_ECREATE;
		}
		UINT bytes_written;
		res = DFS_WriteFile(&fi, scratch_sector, read_buf, &bytes_written, file_size);
		if (res != DFS_OK) {
			free(read_buf);
			Close(archive_handle);
			return E_EWRITE;
		}
		if (bytes_written != file_size)
		{
			// Out of disk space - unsure what error to return here
			free(read_buf);
			Close(archive_handle);
			return E_TOO_MANY_FILES;
		}
		free(read_buf);
		// Point to next file (or NULL termination)
		current_file += strlen(current_file) + 1;
	}
	archive_handle->volume_dirty = true;
	Close(archive_handle);
	if (Flags & PK_PACK_MOVE_FILES)
	{
		//LPCSTR *temp_list = (LPCSTR)AddList;

		//while (*temp_list)
		//{
		//	DeleteFile(temp_list);
		//	temp_list += strlen(temp_list) + 1;
		//}
	}
	return 0; // All ok
}

// TODO: Delete folder(s)
int Delete(char *PackedFile, char *DeleteList)
{
	if (!DeleteList || !*DeleteList) return E_NO_FILES;

	tOpenArchiveData archive_data = { 0 };
	archive_data.ArcName = PackedFile;
	tArchive *archive_handle;
	archive_handle = Open(&archive_data);
	if (!archive_handle)
	{
		// This is what Open() returns if it reaches an error (archive_handle=NULL).
		// However, since the return sctruct is deleted before returned, we pass this error manually here
		return E_BAD_ARCHIVE;
	}
	if (archive_data.OpenResult)
	{
		return archive_data.OpenResult;
	}

	uint32_t res;
	uint8_t scratch_sector[SECTOR_SIZE];

	while (*DeleteList) // Each string in AddList is zero-delimited (ends in zero), and the AddList string ends with an extra zero byte, i.e. there are two zero bytes at the end of AddList.
	{
		res = DFS_UnlinkFile(&archive_handle->vi, (uint8_t *)DeleteList, scratch_sector);

		if (res != DFS_OK)
		{
			Close(archive_handle);
			return E_ECLOSE;
		}

		DeleteList += strlen(DeleteList) + 1;
	}
	archive_handle->volume_dirty = true;
	Close(archive_handle);

	return 0; // All ok
}
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

// Add/Move files to image
int __stdcall PackFiles(char *PackedFile, char *SubPath, char *SrcPath, char *AddList, int Flags)
{
	return Pack(PackedFile, SubPath, SrcPath, AddList, Flags);
}

// Delete files from image
int __stdcall DeleteFiles(char *PackedFile, char *DeleteList)
{
	return Delete(PackedFile, DeleteList);
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
	return PK_CAPS_BY_CONTENT | PK_CAPS_MODIFY | PK_CAPS_MULTIPLE | PK_CAPS_DELETE | PK_CAPS_NEW;
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

#ifdef _WIN32
// The DLL entry point
BOOL APIENTRY DllMain(HANDLE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    return TRUE;
}
#endif
