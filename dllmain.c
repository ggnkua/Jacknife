//TODO: "If PK_CAPS_HIDE is set, the plugin will not show the file type as a packer. This is useful for plugins which are mainly used for creating files, e.g. to create batch files, avi files etc. The file needs to be opened with Ctrl+PgDn in this case, because Enter will launch the associated application."
//    ==>altho this would require a second build with different filenames etc - the "gibberish extension"-solution is clumsy, but easier \o/
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define DIR_SEPARATOR_STRING "\\"
#define FOPEN_S(a,b,c) fopen_s(&a,b,c)
#else
#include <unistd.h>
#define DIR_SEPARATOR_STRING "/"
#define __stdcall
#define sprintf_s(a,b,...) sprintf(a,__VA_ARGS__)
#define strcpy_s(a,b,c) strcpy(a,c)
#define FOPEN_S(a,b,c) a=fopen(b,c)
#define _strcmpi strcasecmp
typedef char *LPCSTR;
#define _stat stat
#define _ftelli64 ftello
#include <signal.h>
#define DebugBreak() raise(SIGTRAP);
#include <ctype.h>
#define TRUE true
#define FALSE false
#define BOOL bool
#include <stdbool.h>
#include <stdlib.h>
#endif

#if _MSC_VER
#define INLINE __forceinline
#elif defined(__APPLE__)
#define INLINE
#else
#define INLINE static inline
#endif

#include "wcxhead.h"
#include "dosfs-1.03/dosfs.h"
#include "jacknife.h"

#define STORAGE_BYTES 65536 - sizeof(void *) - sizeof(unsigned short)
typedef struct entrylist_storage_
{
	struct entrylist_storage_ *prev;
	unsigned short current_offset;
	unsigned char data[STORAGE_BYTES];
} ENTRYLIST_STORAGE;

stEntryList entryList;
tArchive* pCurrentArchive;
typedef tArchive* myHANDLE;
DISK_IMAGE_INFO disk_image;
ENTRYLIST_STORAGE *current_storage = NULL;
tProcessDataProc ProcessDataProc;
int current_partition; // TODO: terrible, find some better way!

// The following 2 routines were lifted from https://github.com/greiman/SdFat
// tweaked a bit to suit our needs

typedef struct FatLfn
{
	/** UTF-16 length of Long File Name */
	//size_t len;
	/** Position for sequence number. */
	uint8_t seqPos;
	/** Flags for base and extension character case and LFN. */
	uint8_t flags;
	/** Short File Name */
	uint8_t sfn[11];

	const char *lfn;
} FatLfn_t;


#define FAT_NAME_FREE 0X00;		/** name[0] value for entry that is free and no allocated entries follow */
#define FAT_NAME_DELETED 0XE5;	/** name[0] value for entry that is free after being "deleted" */
// Directory attribute of volume label.
#define FAT_ATTRIB_LABEL 0x08;
#define FAT_ATTRIB_LONG_NAME 0X0F;
#define FAT_CASE_LC_BASE 0X08;	/** Filename base-name is all lower case */
#define FAT_CASE_LC_EXT 0X10;	/** Filename extension is all lower case.*/

#define FNAME_FLAG_LOST_CHARS 0X01											/** Derived from a LFN with loss or conversion of characters. */
#define FNAME_FLAG_MIXED_CASE 0X02											/** Base-name or extension has mixed case. */
#define FNAME_FLAG_NEED_LFN (FNAME_FLAG_LOST_CHARS | FNAME_FLAG_MIXED_CASE)	/** LFN entries are required for file name. */
#define FNAME_FLAG_LC_BASE FAT_CASE_LC_BASE									/** Filename base-name is all lower case */
#define FNAME_FLAG_LC_EXT FAT_CASE_LC_EXT									/** Filename extension is all lower case. */

#define DBG_HALT_IF(b) \
  if (b) {             \
    DebugBreak() ; \
  }

#define DBG_HALT_MACRO DebugBreak()

#define DBG_WARN_IF(b) \
  if (b) {             \
    printf("%d\n", __LINE__); \
  }

//------------------------------------------------------------------------------
// Reserved characters for FAT short 8.3 names.
INLINE BOOL sfnReservedChar(uint8_t c) {
	if (c == '"' || c == '|' || c == '[' || c == '\\' || c == ']') {
		return TRUE;
	}
	//  *+,./ or :;<=>?
	if ((0X2A <= c && c <= 0X2F && c != 0X2D) || (0X3A <= c && c <= 0X3F)) {
		return TRUE;
	}
	// Reserved if not in range (0X20, 0X7F).
	return !(0X20 < c && c < 0X7F);
}
BOOL makeSFN(FatLfn_t* fname) {
	BOOL is83;
	//  char c;
	uint8_t c;
	uint8_t bit = FAT_CASE_LC_BASE;
	uint8_t lc = 0;
	uint8_t uc = 0;
	uint8_t i = 0;
	uint8_t in = 7;
	const char* dot;
	const char *end = fname->lfn + strlen(fname->lfn);
	const char* ptr = fname->lfn;

	// Assume not zero length.
	//DBG_HALT_IF(end == ptr);
	if (end == ptr) return FALSE;
	// Assume blanks removed from start and end.
	DBG_HALT_IF(*ptr == ' ' || *(end - 1) == ' ' || *(end - 1) == '.');

	// Blank file short name.
	for (uint8_t k = 0; k < 11; k++) {
		fname->sfn[k] = ' ';
	}
	// Not 8.3 if starts with dot.
	is83 = *ptr == '.' ? FALSE : TRUE;
	// Skip leading dots.
	for (; *ptr == '.'; ptr++) {
	}
	// Find last dot.
	for (dot = end - 1; dot > ptr && *dot != '.'; dot--) {
	}

	for (; ptr < end; ptr++) {
		c = *ptr;
		if (c == '.' && ptr == dot) {
			in = 10;                // Max index for full 8.3 name.
			i = 8;                  // Place for extension.
			bit = FAT_CASE_LC_EXT;  // bit for extension.
		} else {
			if (sfnReservedChar(c)) {
				is83 = FALSE;
				// Skip UTF-8 trailing characters.
				if ((c & 0XC0) == 0X80) {
					continue;
				}
				c = '_';
			}
			if (i > in) {
				is83 = FALSE;
				if (in == 10 || ptr > dot) {
					// Done - extension longer than three characters or no extension.
					break;
				}
				// Skip to dot.
				ptr = dot - 1;
				continue;
			}
			//if (isLower(c)) {
			if (islower(c)) {
				c += 'A' - 'a';
				lc |= bit;
			//} else if (isUpper(c)) {
			} else if (isupper(c)) {
				uc |= bit;
			}
			fname->sfn[i++] = c;
			if (i < 7) {
				fname->seqPos = i;
			}
		}
	}
	if (fname->sfn[0] == ' ') {
		DBG_HALT_MACRO;
		{
			int debug_halt = 0;
		}
		goto fail;
	}
	if (is83) {
		fname->flags = (lc & uc) ? FNAME_FLAG_MIXED_CASE : lc;
	} else {
		fname->flags = FNAME_FLAG_LOST_CHARS;
		fname->sfn[fname->seqPos] = '~';
		fname->sfn[fname->seqPos + 1] = '1';
	}
	return TRUE;

	fail:
		return FALSE;
}

// ggn: Unsure if we'll go so deep into this. Generally, people shouldn't use long filenames.
//      For now we'll probably hammer a "~1" in the filename and hope people don't try to abuse this too much
//      (another reason for punting on this for now is that we'll have to (re)scan the current directory listing
//      to ensure no collision happens

//typedef struct {
//	uint8_t name[11];
//	uint8_t attributes;
//	uint8_t caseFlags;
//	uint8_t createTimeMs;
//	uint8_t createTime[2];
//	uint8_t createDate[2];
//	uint8_t accessDate[2];
//	uint8_t firstClusterHigh[2];
//	uint8_t modifyTime[2];
//	uint8_t modifyDate[2];
//	uint8_t firstClusterLow[2];
//	uint8_t fileSize[4];
//} DirFat_t;

//BOOL makeUniqueSfn(FatLfn_t* fname) {
//	const uint8_t FIRST_HASH_SEQ = 2;  // min value is 2
//	uint8_t pos = fname->seqPos;
//	DirFat_t* dir;
//	uint16_t hex = 0;
//
//	DBG_HALT_IF(!(fname->flags & FNAME_FLAG_LOST_CHARS));
//	DBG_HALT_IF(fname->sfn[pos] != '~' && fname->sfn[pos + 1] != '1');
//
//	for (uint8_t seq = FIRST_HASH_SEQ; seq < 100; seq++) {
//		DBG_WARN_IF(seq > FIRST_HASH_SEQ);
//		hex += millis();
//		if (pos > 3) {
//			// Make space in name for ~HHHH.
//			pos = 3;
//		}
//		for (uint8_t i = pos + 4; i > pos; i--) {
//			uint8_t h = hex & 0XF;
//			fname->sfn[i] = h < 10 ? h + '0' : h + 'A' - 10;
//			hex >>= 4;
//		}
//		fname->sfn[pos] = '~';
//		rewind();
//		while (1) {
//			dir = readDirCache(TRUE);
//			if (!dir) {
//				if (!getError()) {
//					// At EOF and name not found if no error.
//					goto done;
//				}
//				DBG_FAIL_MACRO;
//				goto fail;
//			}
//			if (dir->name[0] == FAT_NAME_FREE) {
//				goto done;
//			}
//			if (isFatFileOrSubdir(dir) && !memcmp(fname->sfn, dir->name, 11)) {
//				// Name found - try another.
//				break;
//			}
//		}
//	}
//	// fall inti fail - too many tries.
//	DBG_FAIL_MACRO;
//
//	fail:
//		return FALSE;
//
//	done:
//		return TRUE;
//}

//unpack MSA into a newly created buffer
uint8_t *unpack_msa(tArchive *arch, uint8_t *packedMsa, int packedSize)
{
	int sectors 	=  ((int)packedMsa[2] << 8) | ((int)packedMsa[3]);
	int sides 		= (((int)packedMsa[4] << 8) | ((int)packedMsa[5])) + 1;
	int startTrack 	=  ((int)packedMsa[6] << 8) | ((int)packedMsa[7]);
	int endTrack 	= (((int)packedMsa[8] << 8) | ((int)packedMsa[9])) + 1;
	
	//just ignore partial disk images, skipping tracks would skip bpb/fat, too
	if (startTrack != 0 || endTrack == 0) {
		return NULL;
	}
	
	int unpackedSize = sectors * 512 * sides * endTrack;
	disk_image.image_sectors = sectors;
	disk_image.image_sides = sides;
	disk_image.image_tracks = endTrack;
	uint8_t *unpackedData = (uint8_t *)malloc(unpackedSize);
	if (!unpackedData) return 0;

	int offset = 10;
	int out = 0;
	for (int i = 0; i < endTrack * sides; i++) {
		int trackLen = packedMsa[offset++];
		trackLen <<= 8;
		trackLen += packedMsa[offset++];
		if (trackLen != 512 * sectors) {
			for (; trackLen > 0; trackLen--) {
				unpackedData[out++] = packedMsa[offset++];
				// Bounds check against corrupt MSA images
				if (out > unpackedSize || offset > packedSize)
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
					uint8_t data = packedMsa[offset++];
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
			if (out + trackLen > unpackedSize || offset + trackLen > packedSize)
			{
				free(unpackedData);
				return 0;
			}
			for (; trackLen > 0; trackLen--) {
				unpackedData[out++] = packedMsa[offset++];
			}
		}
	}
	disk_image.file_size = unpackedSize;
	return unpackedData;
}

BOOL guess_size(int size)
{
	if (size % 512) {
		return FALSE;
	}
	int tracks, sectors;
	for (tracks = 86; tracks > 0; tracks--) {
		for (sectors = 11; sectors >= 9; sectors--) {
			if (!(size % tracks)) {
				if ((size % (tracks * sectors * 2 * 512)) == 0) {
					disk_image.image_tracks = tracks;
					disk_image.image_sides = 2;
					disk_image.image_sectors = sectors;
					return TRUE;
				}
				else if ((size % (tracks * sectors * 1 * 512)) == 0) {
					disk_image.image_tracks = tracks;
					disk_image.image_sides = 1;
					disk_image.image_sectors = sectors;
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}

#define BYTE_SWAP_WORD(a) ((unsigned short)(a>>8)|(unsigned short)(a<<8))

BOOL dim_copy_sector_or_fill_with_blank(int fat_entry, void *s, void *d, int cluster_size, int bytes_left)
{
	if (fat_entry == 0 || (fat_entry >= 0xff0 && fat_entry <= 0xff7) || bytes_left <= 0)
	{
		return FALSE;
	}
	if (bytes_left >= cluster_size)
	{
		memcpy(d, s, cluster_size);
	}
	else
	{
		// Yes, there are .dim files that are truncated at the end
		memcpy(d, s, bytes_left);
	}
	return TRUE;
}

unsigned char *expand_dim(BOOL fastcopy_header)
{ 
	unsigned char *buf = (unsigned char *)calloc(1, disk_image.image_tracks * disk_image.image_sectors * disk_image.image_sides * 512);
	unsigned char *d = buf;
	if (!d) return 0;
	unsigned char *s = disk_image.buffer;

	FCOPY_HEADER *h = (FCOPY_HEADER *)s;
	s += 32;

	int total_filesystem_sectors = BYTE_SWAP_WORD(h->total_filesystem_sectors);
	int bytes_left = (int)(disk_image.file_size - total_filesystem_sectors * 512);
	if (fastcopy_header)
		bytes_left -= 32;

	memcpy(d, s, total_filesystem_sectors * 512);
	s += total_filesystem_sectors * 512;
	d += total_filesystem_sectors * 512;

	unsigned char *fat1 = disk_image.buffer + 512+32 + 3; // TODO: A bit hardcoded, but eh
	int cluster_size = BYTE_SWAP_WORD(h->cluster_size);

	for (int i = 0; i < BYTE_SWAP_WORD(h->total_clusters) / 2; i++)
	{
		BOOL ret;

		// Check "even" entry in a FAT12 record
		int fat_entry = ((fat1[1] & 0xf) << 8) | fat1[0];
		ret = dim_copy_sector_or_fill_with_blank(fat_entry, s, d, cluster_size, bytes_left);
		d += cluster_size;
		if (ret)
		{
			s += cluster_size;
			bytes_left -= cluster_size;
		}

		// Check "odd" entry in a FAT12 record
		fat_entry = (fat1[2] << 4) | (fat1[1] >> 4);
		ret = dim_copy_sector_or_fill_with_blank(fat_entry, s, d, cluster_size, bytes_left);
		d += cluster_size;
		if (ret)
		{
			s += cluster_size;
			bytes_left -= cluster_size;
		}

		// Advance through 2 FAT12 entries
		fat1 += 3;
	}

	return buf; 
}

/*
	Attach emulation to a host-side disk image file
	Returns 0 OK, nonzero for any error
*/
uint32_t DFS_HostAttach(tArchive *arch)
{
	disk_image.file_handle = fopen(arch->archname, "r+b");
	if (disk_image.file_handle == NULL)
		return J_FILE_NOT_FOUND;

	fseek(disk_image.file_handle, 0, SEEK_END);
	disk_image.file_size = _ftelli64(disk_image.file_handle);
	fseek(disk_image.file_handle, 0, SEEK_SET);

	disk_image.disk_geometry_does_not_match_bpb = FALSE;
	disk_image.mode = DISKMODE_HARD_DISK;

	if (disk_image.file_size > 2880 * 1024)
	{
		// Hard disk image, we'll do everything in-place inside the file
		return J_OK;
	}

	// Definitely a floppy disk image, let's cache it into RAM
	disk_image.buffer = (uint8_t *)malloc((size_t)disk_image.file_size);
	if (!disk_image.buffer)
	{
		return J_MALLOC_ERROR;
	}
	if (!fread(disk_image.buffer, (size_t)disk_image.file_size, 1, disk_image.file_handle))
	{
		fclose(disk_image.file_handle);
		return -1;
	}
	fclose(disk_image.file_handle);
	
	// Try to figure out what kind of image we got here. We first assume it's just a sector dump (.ST)
	// and then we scan for MSA and FastCopy DIM headers. If we detect either, go do some extra stuff
	disk_image.mode = DISKMODE_LINEAR;
	if ((disk_image.buffer[0] == 0xe && disk_image.buffer[1] == 0xf) ||
		(disk_image.buffer[0] == 0x0 && disk_image.buffer[1] == 0x0 && strlen(arch->archname) > 4 && _strcmpi(arch->archname + strlen(arch->archname) - 4, ".msa") == 0))
	{
		// MSA image, unpack it to a flat buffer
		disk_image.mode = DISKMODE_MSA;
		uint8_t *unpacked_msa = unpack_msa(arch, disk_image.buffer, (int)disk_image.file_size);
		free(disk_image.buffer);
		if (!unpacked_msa)
		{
			return J_INVALID_MSA;
		}
		disk_image.buffer = unpacked_msa;
	}
	else if (*(unsigned short *)disk_image.buffer == 0x4242)
	{
		// Fastcopy DIM image, unpack it to flat buffer if needed
		FCOPY_HEADER *h = (FCOPY_HEADER *)disk_image.buffer;
		if (h->start_track)
		{
			// Nope, we don't support partial images
			return J_INALID_DIM;
		}
		if (h->disk_configuration_present)
		{
			disk_image.image_sectors = h->sectors;
			disk_image.image_sides = h->sides + 1;
			disk_image.image_tracks = h->end_track + 1;
			if (h->get_sectors)
			{
				// Disk was imaged with "Get sectors" on, so we need to 
				// expand the image to fill in the non-imaged sectors with blanks
				disk_image.mode = DISKMODE_FCOPY_CONF_USED_SECTORS;
				uint8_t *expanded = expand_dim(TRUE);
				if (!expanded)
				{
					return J_INALID_DIM;
				}
				disk_image.buffer = expanded;
			}
			else
			{
				// No problem, just skip past the FCopy header and treat it as a normal .ST disk
				disk_image.mode = DISKMODE_FCOPY_CONF_ALL_SECTORS;
				disk_image.buffer += 32;
			}
		}
		else
		{
			disk_image.mode = DISKMODE_FCOPY_NO_CONF;
			disk_image.buffer += 32;
		}
	}
	else
	{
		if (!guess_size((int)disk_image.file_size))
		{
			free(disk_image.buffer);
			return -1;
		}
	}
	return J_OK;
}

// For reasons explained in DFS_GetVolInfo, the image's geometry can be different than what
// the BPB reports. When we detect such a case we need to translate the requested sector
// from the "logical" geometry to the "physical" one. Basically we convert the sector to
// track/sector/side and then convert it back to a sector count using the detected disk geometry.
uint32_t recalculate_sector(uint32_t sector)
{
	uint32_t requested_track = sector / disk_image.bpb_sectors_per_track / disk_image.bpb_sides;
	uint32_t requested_side = (sector % (disk_image.bpb_sectors_per_track * disk_image.bpb_sides)) / disk_image.bpb_sectors_per_track;
	uint32_t requested_sector = sector % disk_image.bpb_sectors_per_track;
	return requested_track * disk_image.image_sectors * disk_image.image_sides +
		requested_side * disk_image.image_sectors +
		requested_sector;
}

/*
	Read sector from image
	Returns 0 OK, nonzero for any error
*/
int DFS_HostReadSector(uint8_t *buffer, uint32_t sector, uint32_t count)
{
	if (disk_image.disk_geometry_does_not_match_bpb)
	{
		// Wonky disk image detected, let's recalculate the requested sector
		sector = recalculate_sector(sector);
		assert(count == 1);	// Leave this here just to remind us that anything if count>1 it could mean Very Bad Things(tm)
	}

	// fseek into an opened for writing file can extend the file on Windows and won't fail, so let's check bounds
	if ((int)(sector * SECTOR_SIZE) > disk_image.file_size)
	{
		return -1;
	}
	
	if (disk_image.mode != DISKMODE_HARD_DISK)
	{
		// It's cached in ram, so let's not hit the disk
		memcpy(buffer, &disk_image.buffer[sector * SECTOR_SIZE], SECTOR_SIZE);
		return 0;
	}
	else
	{
		// TODO: what in the world is this abomination of a test? Simplify
		if (current_partition!=-1 && disk_image.partition_info[current_partition].partition_defined && (sector<disk_image.partition_info[current_partition].start_sector || sector>disk_image.partition_info[current_partition].start_sector + disk_image.partition_info[current_partition].total_sectors))
		{
			return -1;
		}

		if (fseek(disk_image.file_handle, sector * SECTOR_SIZE, SEEK_SET))
		{
			return -1;
		}
		
		fread(buffer, SECTOR_SIZE, count, disk_image.file_handle);
		return 0;
	}
}

uint32_t DFS_ReadSector(uint8_t unit, uint8_t *buffer, uint32_t sector, uint32_t count)
{
	return DFS_HostReadSector(buffer, sector, count);
}

/*
	Write sector to image
	Returns 0 OK, nonzero for any error
*/
int DFS_HostWriteSector(uint8_t *buffer, uint32_t sector, uint32_t count)
{
	if (disk_image.disk_geometry_does_not_match_bpb)
	{
		// Wonky disk image detected, let's recalculate the requested sector
		sector = recalculate_sector(sector);
		assert(count == 1);	// Leave this here just to remind us that anything if count>1 it could mean Very Bad Things(tm)
	}

	// fseek into an opened for writing file can extend the file on Windows and won't fail, so let's check bounds
	if ((int)(sector * SECTOR_SIZE) > disk_image.file_size)
	{
		return -1;
	}
	
	// It's cached in ram, so let's not hit the disk
	if (disk_image.mode != DISKMODE_HARD_DISK)
	{
		memcpy(&disk_image.buffer[sector * SECTOR_SIZE], buffer, SECTOR_SIZE);
		return 0;
	}
	else
	{
		// TODO: what in the world is this abomination of a test? Simplify
		if (current_partition != -1 && disk_image.partition_info[current_partition].partition_defined && (sector<disk_image.partition_info[current_partition].start_sector || sector>disk_image.partition_info[current_partition].start_sector + disk_image.partition_info[current_partition].total_sectors))
		{
			return -1;
		}

		if (fseek(disk_image.file_handle, sector * SECTOR_SIZE, SEEK_SET))
		{
			return -1;
		}

		fwrite(buffer, SECTOR_SIZE, count, disk_image.file_handle);
		fflush(disk_image.file_handle);
		return 0;
	}
}

uint32_t DFS_WriteSector(uint8_t unit, uint8_t *buffer, uint32_t sector, uint32_t count)
{
	return DFS_HostWriteSector(buffer, sector, count);
}

// try to pack a chunk of data in MSA RLE format
// returns packed size or -1 if packing was unsuccessful
static int pack_track(unsigned char *dest, const unsigned char *src, int len) {
	int pklen = 0;
	const unsigned char *p = (const unsigned char *)src, *src_end = (const unsigned char *)src + len;

	while (p < src_end) {
		const unsigned char *prev = p;
		unsigned int pkv = *p++;
		while (p < src_end && *p == pkv) p++;
		int n = (int)(p - prev);
		if ((n >= 4 || pkv == 0xE5) && pklen + 4 < len) {
			*dest++ = 0xE5;
			*dest++ = pkv;
			*dest++ = n >> 8;
			*dest++ = n;
			pklen += 4;
		}
		else if (pklen + n < len) {
			int i;
			for (i = 0; i < n; ++i) *dest++ = pkv;
			pklen += n;
		}
		else {
			return -1;
		}
	}
	return pklen;
}

uint8_t *make_msa(tArchive *arch)
{
	// Write MSA header
	int sectors 	= disk_image.image_sectors;
	int sides 		= disk_image.image_sides;
	int start_track = 0;
	int end_track 	= disk_image.image_tracks - 1;

	unsigned char *packed_buffer = (unsigned char *)malloc(10 + end_track * (sectors * SECTOR_SIZE + 2) * sides + 100000); // 10=header size, +2 bytes per track for writing the track size
	if (!packed_buffer) return 0;
	unsigned char *pack = packed_buffer;
	
	*(unsigned short *)(pack + 0) = 0x0f0e;
	*(unsigned short *)(pack + 2) = ((unsigned short)(    sectors << 8)) | ((unsigned short)(    sectors >> 8));
	*(unsigned short *)(pack + 4) = ((unsigned short)((sides - 1) << 8)) | ((unsigned short)((sides - 1) >> 8));
	*(unsigned short *)(pack + 6) = 0; // Start track will always be 0
	*(unsigned short *)(pack + 8) = ((unsigned short)(  end_track << 8)) | ((unsigned short)(  end_track >> 8));
	pack += 10;

	int track;
	unsigned char *p = disk_image.buffer;
	for (track = 0; track < end_track + 1; ++track) {
		int side;
		for (side = 0; side < sides; ++side) {
			// try to compress the track
			int pklen = pack_track(pack + 2, p, sectors * 512);
			if (pklen < 0) {
				// compression failed, writing uncompressed
				*(unsigned short *)(pack) = (unsigned short)((sectors * SECTOR_SIZE) >> 8) | (unsigned short)((sectors * SECTOR_SIZE) << 8);
				memcpy(pack + 2, p, sectors * 512);
				pack += 2 + SECTOR_SIZE * sectors;
			}
			else {
				// write the compressed data
				*(unsigned short *)(pack) = (unsigned short)(pklen >> 8) | (unsigned short)(pklen << 8);
				pack += 2 + pklen;
			}
			p += sectors * 512;
		}
	}
	disk_image.file_size = (int)(pack - packed_buffer);
	return packed_buffer;
}

int DFS_HostDetach(tArchive *arch)
{
	if (disk_image.mode == DISKMODE_HARD_DISK)
	{
		// Just close the file, we're done
		if (!disk_image.file_handle) return -1;
		return fclose(disk_image.file_handle);
	}		
	
	if (disk_image.mode == DISKMODE_FCOPY_CONF_ALL_SECTORS || disk_image.mode == DISKMODE_FCOPY_NO_CONF)
	{
		// Rewind pointer here because we might want to free it
		disk_image.buffer -= 32;
	}

	// Floppy image, we have some work to do
	if (!arch->volume_dirty)
	{
		// Only try to save the image to disk if we actually modified it
		// If we performed a file operation (add files, new folder, etc)
		// and it failed, then we don't update the image to disk
		free(disk_image.buffer);
		return 0;
	}
	
	if (disk_image.mode == DISKMODE_MSA)
	{
		uint8_t *packed_msa = make_msa(arch);
		if (!packed_msa)
		{
			free(disk_image.buffer);
			return -1;
		}
		free(disk_image.buffer);
		disk_image.buffer = packed_msa;
	}
	
	if (disk_image.mode == DISKMODE_FCOPY_CONF_ALL_SECTORS || disk_image.mode == DISKMODE_FCOPY_CONF_USED_SECTORS)
	{
		// Eventually here we'll just add some code to write a .dim file using "all sectors",
		// unless someone reeeeeeeeally asks for "used sectors" format it's not happening
	}
	
	if (disk_image.mode == DISKMODE_FCOPY_NO_CONF)
	{
		// Unsure if the implementation is different here, it might be merged with the above block
	}
	
	disk_image.file_handle = fopen(arch->archname, "wb");
	if (!disk_image.file_handle)
	{
		return -1;
	}
	fwrite(disk_image.buffer, (size_t)disk_image.file_size, 1, disk_image.file_handle);
	free(disk_image.buffer);
	fclose(disk_image.file_handle);
	return 0;	
}

stEntryList* findLastEntry() {
	stEntryList* entry = &entryList;
	while (entry->next != NULL) {
		entry = entry->next;
	}
	return entry;
}

// In some parts of the code we get a filename in "directory format", i.e.
// something like "FILE    EXT". This routine converts this to a "canonical"
// filename, i.e. "FILE.EXT"
void dir_to_canonical(char dest[13], uint8_t *src)
{
	BOOL added_dot = FALSE;
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
			added_dot = TRUE;
		}
		*dest++ = *src++;
		if (i == 7 && !added_dot && *src!=' ') *dest++ = '.';
	}
	*dest = 0;
}

stEntryList *new_EntryList()
{
	if (!current_storage)
	{
		// First time run, allocate a chunk
		current_storage = (ENTRYLIST_STORAGE *)malloc(65536);
		if (!current_storage)
		{
			return NULL;
		}
		current_storage->prev = NULL;
		current_storage->current_offset = 0;
	}
	
	if (current_storage->current_offset + sizeof(stEntryList)>STORAGE_BYTES)
	{
		ENTRYLIST_STORAGE *temp = current_storage;
		current_storage = (ENTRYLIST_STORAGE *)malloc(65536);
		if (!current_storage)
		{
			return NULL;
		}
		current_storage->prev = temp;
		current_storage->current_offset = 0;
	}
	
	uint16_t offset = current_storage->current_offset;
	current_storage->current_offset += sizeof(stEntryList);
	
	return (stEntryList *)&current_storage->data[offset];
}

uint32_t scan_files(char* path, VOLINFO *vi, char *partition_prefix)
{
	uint32_t ret;
	int i;
	DIRINFO di;
	char filename_canonical[13];
	
	uint8_t *scratch_sector=(uint8_t *)malloc(SECTOR_SIZE);
	if (!scratch_sector) return DFS_ERRMISC;

	di.scratch = scratch_sector;
	stEntryList *lastEntry;

	ret = DFS_OpenDir(vi, (uint8_t *)path, &di);
	if (ret == DFS_OK) {
		i = (int)strlen(path);
		for (;;) {
			lastEntry = findLastEntry();
			ret = DFS_GetNext(vi, &di, &(*lastEntry).de);
			if (lastEntry->de.name[0] == 'S' && lastEntry->de.name[1] == 'R' && lastEntry->de.name[2] == 'S')
			{
				int k = 42;
			}
			if (ret != DFS_OK) break;
			if (lastEntry->de.name[0] == 0) continue;
			if (strcmp((char *)lastEntry->de.name, ".          \x10") == 0) continue;
			if (strcmp((char *)lastEntry->de.name, "..         \x10") == 0) continue;
			dir_to_canonical(filename_canonical, lastEntry->de.name);
			if (lastEntry->de.attr & ATTR_VOLUME_ID) {
				strcpy((char *)vi->label, filename_canonical);
				continue;
			}
			if (lastEntry->de.attr & ATTR_DIRECTORY) {
				//if we exceed MAX_PATH this image has a serious problem, so better bail out
				if (strlen(path) + strlen(filename_canonical) + 1 >= MAX_PATH ||
					sprintf_s((char *)lastEntry->fileWPath, MAX_PATH, "%s%s%s" DIR_SEPARATOR_STRING, partition_prefix, path, filename_canonical) == -1) {
					ret = DFS_ERRMISC;
					break;
				}
				if (i + strlen((char *)lastEntry->de.name) + 1 >= MAX_PATH ||
					sprintf_s(&path[i], MAX_PATH - i, "%s" DIR_SEPARATOR_STRING, filename_canonical) == -1) {
					ret = DFS_ERRMISC;
					break;
				}
				stEntryList *new_item = new_EntryList();
				if (!new_item)
				{
					ret = DFS_ERRMISC;
					break;
				}
				lastEntry->next = new_item;
				lastEntry->next->prev = lastEntry;
				lastEntry->next->next = NULL;
				lastEntry = new_item;
				ret = scan_files(path, vi, partition_prefix);
				if (ret != DFS_OK && ret != DFS_EOF)
				{
					break;
				}
				path[i] = 0;
			}
			else {
				//if we exceed MAX_PATH this image has a serious problem, so better bail out
				if (strlen(path) + strlen(filename_canonical) + 1 >= MAX_PATH ||
					sprintf_s(lastEntry->fileWPath, MAX_PATH, "%s%s%s", partition_prefix, path, filename_canonical) == -1)
				{
					ret = DFS_ERRMISC;
					break;
				}
				stEntryList *new_item = new_EntryList();
				if (!new_item)
				{
					ret = DFS_ERRMISC;
					break;
				}
				lastEntry->next = new_item;
				lastEntry->next->prev = lastEntry;
				lastEntry->next->next = NULL;
				lastEntry = new_item;
			}
		}
	}
	else
	{
		int k = 42;
	}
	free(scratch_sector);

	return ret;
}

uint32_t OpenImage(tOpenArchiveData *wcx_archive, tArchive *arch)
{
	wcx_archive->CmtBuf = 0;
	wcx_archive->CmtBufSize = 0;
	wcx_archive->CmtSize = 0;
	wcx_archive->CmtState = 0;
	wcx_archive->OpenResult = E_NO_MEMORY;// default error type

	uint32_t ret= DFS_HostAttach(arch);
	if (ret != J_OK)
	{
		wcx_archive->OpenResult = E_BAD_ARCHIVE;
		return ret;
	}

	uint8_t scratch_sector[SECTOR_SIZE];

	// Obtain pointer to first partition on first (only) unit
	if (disk_image.mode == DISKMODE_HARD_DISK)
	{
		PART_INFO *p = disk_image.partition_info;
		VOLINFO *a = arch->vi;
		current_partition = -1; // Skip partition limit checks for this
		for (int i = 0; i < MAX_PARTITIONS; i++)
		{
			p->partition_defined = TRUE;
			p->start_sector = DFS_GetPtnStart(0, scratch_sector, i, &p->active, (uint8_t *)p->type, &p->total_sectors);
			if (p->start_sector == 0xffffffff)
			{
				// Do nothing for now, as other partitions might be ok
				//printf("Cannot find first partition\n");
				//return false;
				// TODO: check if all partitions are bad and error out
				p->partition_defined = FALSE;
			}
			DFS_GetVolInfo(0, scratch_sector, p->start_sector, a);
			if (p->start_sector == 0)
			{
				// Not a partition, mark it as such
				// TODO better checks?
				p->partition_defined = FALSE;
			}
			p++;
			a++;
		}
	}
	else
	{
		if (DFS_GetVolInfo(0, scratch_sector, 0, arch->vi)) {
			//printf("Error getting volume information\n");
			wcx_archive->OpenResult = E_BAD_DATA;
			return J_INVALID_HARD_DISK_IMAGE;
		}
	}

	wcx_archive->OpenResult = 0;// ok

	return J_OK;
}

tArchive* Open(tOpenArchiveData* wcx_archive)
{
	int partitions = 1;
	
	tArchive *arch = (tArchive *)calloc(1, sizeof(tArchive));
	if (!arch)
	{
		return NULL;
	}
	arch->volume_dirty = FALSE;
	strcpy_s(arch->archname,MAX_PATH, wcx_archive->ArcName);
	pCurrentArchive = arch;

	// trying to open
	if (OpenImage(wcx_archive, arch) != J_OK)
		goto error;

	entryList.next = NULL;
	entryList.prev = NULL;

	char path[MAX_PATH + 1];

	uint32_t ret;
	if (disk_image.mode == DISKMODE_HARD_DISK)
	{
		partitions = MAX_PARTITIONS;
	}

	for (int i = 0; i < partitions; i++)
	{
		current_partition = i;
		path[0] = 0;

		// This is a prefix path that is prepended in hard disk partitions.
		// Otherwise we'd return a list with all directories of all partitions mixed into one.
		// The current format is "0\", "1\", etc
		char partition_prefix[16] = { 0 };
		if (disk_image.mode == DISKMODE_HARD_DISK)
		{
			sprintf(partition_prefix, "%i" DIR_SEPARATOR_STRING, i);
		}

		ret = scan_files((char *)&path, &arch->vi[i], partition_prefix);
		if (ret != DFS_OK && ret != DFS_EOF) {
			arch->currentEntry = &entryList;
			arch->lastEntry = NULL;
			wcx_archive->OpenResult = E_BAD_DATA;
			return arch;
		}
	}
	arch->currentEntry = &entryList;
	arch->lastEntry = NULL;
	wcx_archive->OpenResult = 0;// ok
	return arch;

error:
	// memory must be freed
	free(arch);
	return NULL;
};

int NextItem(tArchive* hArcData, tHeaderData* HeaderData)
{
	tArchive* arch = hArcData;
	if (arch->currentEntry->next == NULL) {
		return E_BAD_ARCHIVE;
	}
	
	strcpy_s(HeaderData->ArcName, MAX_PATH, arch->archname);
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
	// TC format: FileTime = (year - 1980) << 25 | month << 21 | day << 16 | hour << 11 | minute << 5 | second / 2;
	// FAT entries: 
	// Date: F E D C B A 9 8 7 6 5 4 3 2 1 0    Time: F E D C B A 9 8 7 6 5 4 3 2 1 0
	//      | Year        | Month | Day     |        | Hour    | Minute    | Second  |
	HeaderData->FileTime = (arch->currentEntry->de.wrttime_h << 8) | (arch->currentEntry->de.wrttime_l) | (arch->currentEntry->de.wrtdate_h << 24) | (arch->currentEntry->de.wrtdate_l << 16);
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
	tArchive *arch = hArcData;
	BOOL abort = FALSE;

	int filename_offset = 0;
	int partition = 0;
	if (disk_image.mode == DISKMODE_HARD_DISK)
	{
		partition = *arch->lastEntry->fileWPath - '0';
		// Strip out the partition path prefix (for now it's "0\", "1\", etc depending on partition)
		filename_offset = 2;
	}
	current_partition = partition;

	if (Operation == PK_EXTRACT && arch->lastEntry != NULL) {
		uint32_t res;
		FILEINFO fi;
		res = DFS_OpenFile(&arch->vi[partition], (uint8_t *)arch->lastEntry->fileWPath + filename_offset, DFS_READ, scratch_sector, &fi, 0);
		if (res != DFS_OK) {
			return E_EREAD;
		}
		unsigned int readLen;
		unsigned int len;
		len = fi.filelen;
		unsigned char *buf = (uint8_t *)calloc(1, len + 1024); // Allocate some extra RAM and wipe it so we don't write undefined values to the file
		res = DFS_ReadFile(&fi, scratch_sector, buf, &readLen, len);
		if (res != DFS_OK) {
			free(buf);
			return E_EREAD;
		}

		// Assuming that DestName contains only the file name and DestPath the file path
		char *filename = DestName;
		char file[MAX_PATH];
		if (DestPath != NULL)
		{
			// DestName contains the full path and file name and DestPath is NULL
			sprintf_s(file, MAX_PATH, "%s" DIR_SEPARATOR_STRING "%s", DestPath, DestName);
			filename = file;
		}

		FILE *f;
		FOPEN_S(f, filename, "wb");
		if (f == NULL) {
			free(buf);
			return E_EWRITE;
		}
		size_t wlen = fwrite(buf, len, 1, f);
		if (ProcessDataProc)
		{
			abort = !ProcessDataProc(DestName, len);
		}
		if (wlen != 1 || abort) {
			free(buf);
			fclose(f);
			return E_EWRITE;
		}
		fclose(f);
		free(buf);
		return 0;
	}

	return E_BAD_DATA;//ok
};


int Close(tArchive* hArcData)
{
	tArchive* arch = hArcData;
	pCurrentArchive = NULL;
	DFS_HostDetach(arch);
	ENTRYLIST_STORAGE *entry = current_storage;
	while (entry->prev != NULL) {
		ENTRYLIST_STORAGE *temp = entry->prev;
		free(entry);
		entry = temp;
	}
	free(entry);
	current_storage = NULL;
	free(arch);

	return 0;// ok
};

// Make Visual Studio shut up about warnings in the following code.
// The author knows what they're doing, dear, so kindly go play somewhere else
#pragma warning(disable:4146)
#pragma warning(disable:4244)

// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

typedef struct { uint64_t state;  uint64_t inc; } pcg32_random_t;

uint32_t pcg32_random_r(pcg32_random_t *rng)
{
	uint64_t oldstate = rng->state;
	// Advance internal state
	rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
	// Calculate output function (XSH RR), uses old state for max ILP
	uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
	uint32_t rot = oldstate >> 59u;
	return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

// pcg32_srandom(initstate, initseq)
// pcg32_srandom_r(rng, initstate, initseq):
//     Seed the rng.  Specified in two parts, state initializer and a
//     sequence selection constant (a.k.a. stream id)

void pcg32_srandom_r(pcg32_random_t *rng, uint64_t initstate, uint64_t initseq)
{
	rng->state = 0U;
	rng->inc = (initseq << 1u) | 1u;
	pcg32_random_r(rng);
	rng->state += initstate;
	pcg32_random_r(rng);
}

void convert_pathname_to_dos_path(char *src, char *dst)
{
	char *s = src;
	char *e = src + strlen(src);
	char *f = dst;
	if (*s == DIR_SEPARATOR)
	{
		// Eat leading dir separator as we will end up with a duff folder name
		s++;
	}
	// sanitise any subfolders in the path
	while (s != e)
	{
		char lfn[MAX_PATH-1];
		{
			char *l = lfn;
			while (s != e && *s != DIR_SEPARATOR)
			{
				*l++ = *s++;
			}
			*l = 0;
		}
		FatLfn_t l;
		l.lfn = lfn;
		makeSFN(&l);

		// FFS this is so bad lolol
		dir_to_canonical(f, l.sfn);

		f += strlen(f);
		if (*s == DIR_SEPARATOR)
		{
			s++;
			*f++ = DIR_SEPARATOR;
		}
	}
	*f = 0;

}

int create_new_disk_image_in_ram(int create_new_disk_image_type, char *PackedFile)
{
	// Let's create a disk image and attach it
	int sides, tracks, sectors;
	uint8_t *buf;

	if (create_new_disk_image_type == 1)
	{
		// 80 tracks, 9 sectors, 2 sides
		tracks = 80;
		sides = 2;
		sectors = 9;
	}
	else if (create_new_disk_image_type == 2)
	{
		// 82 tracks, 10 sectors, 2 sides
		tracks = 82;
		sides = 2;
		sectors = 10;
	}
	else if (create_new_disk_image_type == 3)
	{
		// 82 tracks, 11 sectors, 2 sides
		tracks = 82;
		sides = 2;
		sectors = 11;
	}
	else
	{
		// We tried everything we could for double density disks, give up (for now)
		// TODO: support high density disks
		return E_TOO_MANY_FILES;
	}

	buf = (uint8_t *)calloc(1, tracks * sides * sectors * 512);
	if (!buf)
	{
		return E_NO_MEMORY;
	}

	// TODO: initialisations are copied from STEem engine's blank images, perhaps we could make this even better?
	PLBR rs = (PLBR)buf;
	rs->bra = 0x30eb;

	// Get a random number for the disk serial (check out https://github.com/imneme/pcg-c-basic/blob/master/pcg32-demo.c)
	pcg32_random_t rng;
	int rounds = 5;
	pcg32_srandom_r(&rng, time(NULL) ^ (intptr_t)&printf, (intptr_t)&rounds);

	rs->serial[0] = (uint8_t)(rng.state);
	rs->serial[1] = (uint8_t)(rng.state >> 8);
	rs->serial[2] = (uint8_t)(rng.state >> 16);

	rs->bpb.BPS_l = (uint8_t)(512);
	rs->bpb.BPS_h = (uint8_t)(512 >> 8);
	rs->bpb.SPC = 2;
	rs->bpb.RES_l = 1;
	rs->bpb.RES_h = 0;
	rs->bpb.NFATS = 2;
	rs->bpb.NDIRS_l = (uint8_t)(112);
	rs->bpb.NDIRS_h = (uint8_t)(112 >> 8);
	rs->bpb.NSECTS_l = (uint8_t)(tracks * sectors * sides);
	rs->bpb.NSECTS_h = (uint8_t)((tracks * sectors * sides) >> 8);
	rs->bpb.MEDIA = 0xf9;
	rs->bpb.SPF_l = 5;
	rs->bpb.SPF_h = 0;
	rs->bpb.SPT_l = sectors;
	rs->bpb.SPT_h = 0;
	rs->bpb.NSIDES_l = 2;
	rs->bpb.NSIDES_h = 0;
	rs->bpb.NHID_l = 0;
	rs->bpb.NHID_h = 0;

	rs->checksum[0] = 0x97;
	rs->checksum[1] = 0xc7;

	// Initialise FAT table
	buf[512 + 0] = 0xf0;
	buf[512 + 1] = 0xff;
	buf[512 + 2] = 0xff;

	if (strlen(PackedFile) > 3 && _strcmpi(PackedFile + strlen(PackedFile) - 3, ".st") == 0)
	{
		disk_image.mode = DISKMODE_LINEAR;
	}
	else if (strlen(PackedFile) > 4 && _strcmpi(PackedFile + strlen(PackedFile) - 4, ".msa") == 0)
	{
		disk_image.mode = DISKMODE_MSA;
	}
	else if (strlen(PackedFile) > 4 && _strcmpi(PackedFile + strlen(PackedFile) - 4, ".dim") == 0)
	{
		disk_image.mode = DISKMODE_FCOPY_CONF_ALL_SECTORS;
		return E_UNKNOWN_FORMAT; // No .dim write support yet
	}
	else
	{
		return E_UNKNOWN_FORMAT;
	}

	disk_image.buffer = buf;
	disk_image.file_size = tracks * sectors * sides * SECTOR_SIZE;
	disk_image.disk_geometry_does_not_match_bpb = FALSE;
	disk_image.image_sectors = sectors;
	disk_image.image_sides = sides;
	disk_image.image_tracks = tracks;

	return J_OK;
}

uint32_t create_new_folder(char *folder_name, VOLINFO *vi, void *scratch_sector)
{
	uint32_t ret;
	int file_timestamp;
	unsigned int bytes_written;
	struct tm *file_tm;
	FILEINFO fi;

	// Check if path exists before creating
	ret = DFS_OpenFile(vi, (uint8_t *)folder_name, DFS_READ, scratch_sector, &fi, 0);
	if (ret == DFS_OK || ret == DFS_ISDIRECTORY)
	{
		// Uh-oh, this pathname exists, let's not proceed
		return E_ECREATE;
	}

	uint8_t *buf = (uint8_t *)calloc(1, SECTOR_SIZE * 2);
	if (!buf)
	{
		return E_ECREATE;
	}

	// Create the directory entry in the parent directory
	time_t ltime;
	time(&ltime);	// Get current date/time
	file_tm = localtime(&ltime);
	file_timestamp = ((file_tm->tm_year - 80) << 25) | (file_tm->tm_wday << 21) | (file_tm->tm_mday << 16) | (file_tm->tm_hour << 11) | (file_tm->tm_min << 5) | ((file_tm->tm_sec / 2));

	if (strlen(folder_name) > MAX_PATH)
	{
		// This limit should be modified to fit TOS' filename limits
		free(buf);
		return E_ECREATE;
	}

	// Ask DOSFS to allocate a cluster from the FAT table, and then we'll fill it in below
	ret = DFS_OpenFile(vi, (uint8_t *)folder_name, DFS_WRITE | DFS_FOLDER, scratch_sector, &fi, file_timestamp);
	if (ret != DFS_OK)
	{
		free(buf);
		return E_ECREATE;
	}

	// Now, create the "." and ".." entries in the new folder
	DIRENT *de = (DIRENT *)buf;
	memcpy((char *)de->name, ".          ", 11);
	// This used to be a part of the string above. But OSX thinks that this is 
	// not good for some reason and crashes? *Shrudder*
	de->attr = ATTR_DIRECTORY;
	// TODO: Root folders and sub-folder folders have different rules
	//       on what cluster number(s) get encoded in their "." and ".." entries
	//de->crtdate_h = (uint8_t)(file_timestamp >> 24);
	//de->crtdate_l = (uint8_t)(file_timestamp >> 16);
	//de->crttime_h = (uint8_t)(file_timestamp >> 8);
	//de->crttime_l = (uint8_t)file_timestamp;
	de->wrtdate_h = (uint8_t)(file_timestamp >> 24);
	de->wrtdate_l = (uint8_t)(file_timestamp >> 16);
	de->wrttime_h = (uint8_t)(file_timestamp >> 8);
	de->wrttime_l = (uint8_t)file_timestamp;
	de->startclus_l_l = fi.cluster & 0xff;
	de->startclus_l_h = (fi.cluster & 0xff00) >> 8;
	de->startclus_h_l = (fi.cluster & 0xff0000) >> 16;
	de->startclus_h_h = (fi.cluster & 0xff000000) >> 24;
	de++;
	memcpy((char *)de->name, "..         ", 11);
	de->attr = ATTR_DIRECTORY;
	//de->crtdate_h = (uint8_t)(file_timestamp >> 24);
	//de->crtdate_l = (uint8_t)(file_timestamp >> 16);
	//de->crttime_h = (uint8_t)(file_timestamp >> 8);
	//de->crttime_l = (uint8_t)file_timestamp;
	//de->wrtdate_h = (uint8_t)(file_timestamp >> 24);
	//de->wrtdate_l = (uint8_t)(file_timestamp >> 16);
	//de->wrttime_h = (uint8_t)(file_timestamp >> 8);
	//de->wrttime_l = (uint8_t)file_timestamp;
	
	// Write the directory entries
	ret = DFS_WriteFile(&fi, scratch_sector, buf, &bytes_written, SECTOR_SIZE * 2);

	// Cleanups
	free(buf);
	if (ret != DFS_OK)
	{
		return E_ECREATE;
	}
	return J_OK;
}

int Pack(char *PackedFile, char *SubPath, char *SrcPath, char *AddList, int Flags)
{
	uint32_t ret;
	FILEINFO fi;
	uint8_t scratch_sector[SECTOR_SIZE];
	tOpenArchiveData wcx_archive = { 0 };
	tArchive archive_handle = { 0 };
	char filename_source[MAX_PATH];
	char filename_dest[MAX_PATH];
	char *filename_subpath;
	char *current_file;
	int create_new_disk_image_type = 0;
	BOOL abort = FALSE;

	if (!AddList || *AddList == 0) return E_NO_FILES;
	strcpy(archive_handle.archname, PackedFile);
	ret = OpenImage(&wcx_archive, &archive_handle);

	try_new_image_size:
	if (ret == J_FILE_NOT_FOUND)
	{
		// If the image isn't physically available on the disk then we assume that we want
		// to create a new one. As we don't know sizes, we will try a 82/2/9 image first. If
		// the files don't fit, we'll keep raising the disk size until we run out of options.
		// When we run out of space we branch to "try_new_image_size" above and try again.
		create_new_disk_image_type++;
		ret = create_new_disk_image_in_ram(create_new_disk_image_type, PackedFile);
		if (ret != J_OK)
		{
			return ret;
		}

		// Now, let DOSFS fill its internal tables
		if (DFS_GetVolInfo(0, scratch_sector, 0, &archive_handle.vi[0]))
		{
			// TODO: I mean, we shouldn't get here since the data should be correct,
			//       but let's just leave it here until we're done testing
			return E_BAD_DATA;
		}

		ret = J_OK; // Bypass exit condition below (i.e. all's fine, fuggetaboutit!
	}

	// TODO: This is here for now to disallow people from messing up .DIM images.
	//       It will go away eventually once we implement .DIM creation
	if (disk_image.mode == DISKMODE_FCOPY_CONF_ALL_SECTORS || disk_image.mode == DISKMODE_FCOPY_NO_CONF || disk_image.mode == DISKMODE_FCOPY_CONF_USED_SECTORS)
	{
		return E_NOT_SUPPORTED;
	}

	if (ret != J_OK)
	{
		return wcx_archive.OpenResult;
	}

	int partition = 0;
	if (disk_image.mode == DISKMODE_HARD_DISK)
	{
		// Determine which partition we are going to write to.
		partition = *SubPath - '0';

		// Strip out the partition path prefix (for now it's "0", "1", etc depending on partition)
		SubPath ++;
		if (*SubPath == DIR_SEPARATOR)
		{
			SubPath++;
		}
	}
	current_partition = partition;

	if (Flags & PK_PACK_SAVE_PATHS)
	{
		// TODO (basically we support this, we need to also support the inverse, i.e. removing paths)
	}
	if (SubPath && *SubPath)
	{
		strcpy(filename_dest, SubPath);
		strcat(filename_dest, DIR_SEPARATOR_STRING);
	}
	else
	{
		*filename_dest = 0;
	}
	strcpy(filename_source, SrcPath);
	filename_subpath = filename_source + strlen(filename_source);
	char *filename_dest_subpath = filename_dest + strlen(filename_dest);
	current_file = AddList;
	while (*current_file) // Each string in AddList is zero-delimited (ends in zero), and the AddList string ends with an extra zero byte, i.e. there are two zero bytes at the end of AddList.
	{
		struct tm *file_tm;
		struct _stat file_stats;
		int file_timestamp;
		int file_size;
		unsigned char *read_buf;
		size_t items_read;
		unsigned int bytes_written;
		char current_short_filename[MAX_PATH + 1];

		// Because the DOSFS lib has a really bad time with long filename entries (and for good reasons)
		// convert the filename into a 8.3 entry before using it.
		strcpy(filename_subpath, current_file);
		strcpy(filename_dest_subpath, current_file);
		convert_pathname_to_dos_path(filename_dest, current_short_filename);

		// At this point we should probably be calling makeUniqueSfn() but eeeeeh? (look at the comments above the function for more insights)
		// Also, turns out that the function we call generates a directory entry string (i.e. 11 chars, spaces, no dots), so
		// we need to convert this to canonical filename here, and then DOSFS will internally re-convert this back to directory entry string.
		// Sigh... This is (almost) the equivalent of graphics programmers converting the coordinate system multiple times inside a rendered
		// frame because their engine or middleware makes different assumptions. Need to clean this up at some point...

		if (current_file[strlen(current_file) - 1] == DIR_SEPARATOR)
		{
			// New folder to be created
			current_short_filename[strlen(current_short_filename) - 1] = 0; // Remove the trailing '\' as to be not confused by a path
			char *folder_name = current_short_filename;

			ret = create_new_folder(folder_name, &archive_handle.vi[partition], scratch_sector);
			if (ret != J_OK)
			{
				DFS_HostDetach(&archive_handle);
				return E_ECREATE;
			}

			// Point to next item in the list (although there probably won't be one)
			current_file += strlen(current_file) + 1;
			continue;
		}
		FILE *handle_to_add = fopen(filename_source, "rb");
		if (!handle_to_add)
		{
			return E_NO_FILES;
		}
		_stat(filename_source, &file_stats);
		file_tm = localtime(&file_stats.st_mtime);
		file_timestamp = ((file_tm->tm_year-80) << 25) | (file_tm->tm_wday << 21) | (file_tm->tm_mday << 16) | (file_tm->tm_hour << 11) | (file_tm->tm_min << 5) | ((file_tm->tm_sec / 2));
		fseek(handle_to_add, 0, SEEK_END);
		file_size = ftell(handle_to_add);
		if (file_size < 0)
		{
			return E_NO_FILES;
		}
		read_buf = (unsigned char *)calloc(1, file_size + 1024); // Allocate some extra RAM and wipe it so we don't write undefined values to the file
		if (!read_buf)
		{
			DFS_HostDetach(&archive_handle);
			return E_NO_MEMORY;
		}
		if (file_size) // don't read for 0 sized files
		{
			fseek(handle_to_add, 0, SEEK_SET);
			items_read = fread(read_buf, file_size, 1, handle_to_add);
			if (!items_read)
			{
				fclose(handle_to_add);
				free(read_buf);
				DFS_HostDetach(&archive_handle);
				return E_EREAD;
			}
		}
		fclose(handle_to_add);
		ret = DFS_OpenFile(&archive_handle.vi[partition], (uint8_t *)current_short_filename, DFS_WRITE | DFS_DELETEOPEN, scratch_sector, &fi, file_timestamp);
		if (ret != DFS_OK)
		{
			free(read_buf);
			DFS_HostDetach(&archive_handle);
			return E_ECREATE;
		}
		ret = DFS_WriteFile(&fi, scratch_sector, read_buf, &bytes_written, file_size);
		if (bytes_written != file_size)
		{
			// Out of disk space - unsure what error to return here
			free(read_buf);
			if (create_new_disk_image_type)
			{
				// We tried to create a new disk image and send the files in, but the files didn't fit
				// Try with a disk geometry that gives a bigger size, if available
				free(disk_image.buffer);
				ret = J_FILE_NOT_FOUND;
				goto try_new_image_size;
			}
			DFS_HostDetach(&archive_handle);
			return E_TOO_MANY_FILES;
		}
		if (ProcessDataProc)
		{
			abort = !ProcessDataProc(current_file, file_size);
		}
		if (ret != DFS_OK || abort)
		{
			free(read_buf);
			DFS_HostDetach(&archive_handle);
			return E_EWRITE;
		}
		free(read_buf);
		// Point to next file (or NULL termination)
		current_file += strlen(current_file) + 1;
	}
	archive_handle.volume_dirty = TRUE;
	DFS_HostDetach(&archive_handle);
	if (Flags & PK_PACK_MOVE_FILES)
	{
		LPCSTR delete_list = (LPCSTR)AddList;

		while (*delete_list)
		{
#ifdef _WIN32
			if (!DeleteFileA(delete_list))
#else
			if (unlink(delete_list))
#endif
			{
				return E_ECLOSE;	// No idea what would be the correct error code
			}
			delete_list += strlen(delete_list) + 1;
		}
	}
	return 0; // All ok
}

uint32_t scan_folder_and_delete(PVOLINFO vi, char *path)
{
	uint8_t *scratch_dir = (uint8_t *)malloc(SECTOR_SIZE);
	if (!scratch_dir) return DFS_ERRMISC;
	uint8_t *scratch_delete = (uint8_t *)malloc(SECTOR_SIZE);
	if (!scratch_delete) return DFS_ERRMISC;

	PDIRINFO di = (PDIRINFO)malloc(sizeof(DIRINFO));
	if (!di) return DFS_ERRMISC;
	di->scratch = scratch_dir;

	uint32_t ret;
	
	ret = DFS_OpenDir(vi, (uint8_t *)path, di);
	if (ret != DFS_OK) return ret;
	char filename_canonical[13];
	BOOL abort = FALSE;
	DIRENT de;

	do
	{
		ret = DFS_GetNext(vi, di, &de);

		if (ret == DFS_EOF) break;

		if (de.name[0] == 0) continue;
		if (strcmp((char *)de.name, ".          \x10") == 0) continue;
		if (strcmp((char *)de.name, "..         \x10") == 0) continue;
		dir_to_canonical(filename_canonical, de.name);
		if (de.attr & ATTR_VOLUME_ID)
		{
			continue;
		}
		if (de.attr & ATTR_DIRECTORY)
		{
			char new_path[MAX_PATH + 1];
			sprintf(new_path, "%s\\%s", path, de.name);
			ret = scan_folder_and_delete(vi, new_path);

			if (ret != DFS_OK && ret != DFS_EOF)
			{
				break;
			}

		}
		else
		{
			uint8_t filename_to_delete[MAX_PATH + 1];
			if (strlen(path) + strlen(filename_canonical) + 1 >= MAX_PATH ||
				sprintf_s((char *const )filename_to_delete, MAX_PATH, "%s\\%s", path, filename_canonical) == -1)
			{
				ret = DFS_ERRMISC;
				break;
			}

			ret = DFS_UnlinkFile(vi, filename_to_delete, scratch_delete);
			if (ProcessDataProc)
			{
				abort = !ProcessDataProc(filename_to_delete, 0);
			}

			if (ret != DFS_OK || abort)
			{
				break;
			}

		}
	} while (1);

	if (ret == DFS_EOF)
	{
		// Delete the actual folder
		ret = DFS_UnlinkFile(vi, (uint8_t *)path, scratch_dir);
	}

	free(scratch_dir);
	free(scratch_delete);
	free(di);
	return ret;
}

int Delete(char *PackedFile, char *DeleteList)
{
	if (!DeleteList || !*DeleteList) return E_NO_FILES;

	tOpenArchiveData wcx_archive = { 0 };
	tArchive archive_handle = { 0 };

	wcx_archive.ArcName = PackedFile;
	strcpy(archive_handle.archname, PackedFile);
	
	BOOL abort = FALSE;

	uint32_t ret = OpenImage(&wcx_archive, &archive_handle);
	if (ret != J_OK)
	{
		return wcx_archive.OpenResult;
	}

	uint8_t scratch_sector[SECTOR_SIZE];

	while (*DeleteList) // Each string in AddList is zero-delimited (ends in zero), and the AddList string ends with an extra zero byte, i.e. there are two zero bytes at the end of AddList.
	{
		int partition = 0;
		if (disk_image.mode == DISKMODE_HARD_DISK)
		{
			// Determine which partition we are going to write to.
			partition = *DeleteList - '0';

			// Strip out the partition path prefix (for now it's "0", "1", etc depending on partition)
			DeleteList += 2;
		}
		current_partition = partition;

		if (strlen(DeleteList) > 4 && strcmp(&DeleteList[strlen(DeleteList) - 3], "*.*") == 0)
		{
			// We have to delete a folder. This is more tricky as we need to ensure that the folder
			// is empty first. Which means we have to recursively scan and delete all the things
			DeleteList[strlen(DeleteList) - 4] = 0; // Remove the "\*.*" postfix
			ret = scan_folder_and_delete(&archive_handle.vi[partition], DeleteList);
			DeleteList += strlen(DeleteList) + 1; // Point to "*.*" which we chopped out above, so we can then point to the next item to delete (if any)
		}
		else
		{
			ret = DFS_UnlinkFile(&archive_handle.vi[partition], (uint8_t *)DeleteList, scratch_sector);
		}

		if (ProcessDataProc)
		{
			abort = !ProcessDataProc(DeleteList, 0);
		}

		if (ret != DFS_OK || abort)
		{
			DFS_HostDetach(&archive_handle);
			return E_ECLOSE;
		}

		DeleteList += strlen(DeleteList) + 1;
	}
	archive_handle.volume_dirty = TRUE;
	DFS_HostDetach(&archive_handle);

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
	ProcessDataProc = pProcessDataProc;
}

int __stdcall GetPackerCaps() {
	return PK_CAPS_SEARCHTEXT | PK_CAPS_BY_CONTENT | PK_CAPS_MODIFY | PK_CAPS_MULTIPLE | PK_CAPS_DELETE | PK_CAPS_NEW;
}

BOOL __stdcall CanYouHandleThisFile(char* FileName) {
	//we simply can't check .ST files by contents, so fake checking it by actually opening it
	//do the same for .MSA for good measure
	if ((strlen(FileName) > 3 && _strcmpi(FileName + strlen(FileName) - 3, ".st") == 0) || 
		(strlen(FileName) > 4 && _strcmpi(FileName + strlen(FileName) - 4, ".msa") == 0) ||
		(strlen(FileName) > 4 && _strcmpi(FileName + strlen(FileName) - 4, ".dim") == 0) ||
		(strlen(FileName) > 4 && _strcmpi(FileName + strlen(FileName) - 4, ".ahd") == 0)) {
		tOpenArchiveData oad;
		oad.ArcName = FileName;
		tArchive* pa = Open(&oad);
		if (pa == NULL) {
			return FALSE;
		}
		if (oad.OpenResult != 0) {
			return FALSE;
		}
		Close(pa);
		return TRUE;
	}
	return FALSE;
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
