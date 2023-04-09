// File renamed to .cpp to address the craziness of mixing C and C++
#define _CRT_SECURE_NO_WARNINGS

/*
	hostemu.c
	DOSFS Embedded FAT-Compatible Filesystem
	Host-Side Emulation Code	
	(C) 2005 Lewin A.R.W. Edwards (sysadm@zws.com)
*/

#include <stdio.h>
#include <stdlib.h>

#include "dosfs.h"
#include "hostemu.h"
#include <string.h>

#include "../wcxhead.h"
#include "../jacknife.h"

uint8_t *unpack_msa(/*const char *file, */tArchive *arch, uint8_t *packedMsa, int PackedSize);

//===================================================================
// Globals
FILE *hostfile;			// references host-side image file
bool cached_into_ram;	// Should we just load the whole thing into RAM?
uint8_t *disk_cache;	// Buffer for the above
int file_size;			// Size of the above buffer

/*
	Attach emulation to a host-side disk image file
	Returns 0 OK, nonzero for any error
*/
int DFS_HostAttach(tArchive *arch)
{
	hostfile = fopen(arch->archname, "r+b");
	if (hostfile == NULL)
		return -1;

	fseek(hostfile, 0, SEEK_END);
	file_size = ftell(hostfile);
	fseek(hostfile, 0, SEEK_SET);

	cached_into_ram = false;
	if (file_size <= 2880 * 1024)
	{
		// Definitely a disk image, let's cache it into RAM
		cached_into_ram = true;
		disk_cache = (uint8_t *)malloc(file_size);
		if (!disk_cache) return -1;
		if (!fread(disk_cache, file_size, 1, hostfile)) { fclose(hostfile); return -1; }
		fclose(hostfile);
		arch->mode = DISKMODE_LINEAR;
		if (disk_cache[0] == 0xe && disk_cache[1] == 0xf)
		{
			arch->mode = DISKMODE_MSA;
			uint8_t *unpacked_msa = unpack_msa(arch, disk_cache, file_size);
			free(disk_cache);
			if (!unpacked_msa)
			{
				return -1;
			}
			disk_cache = unpacked_msa;
			file_size = arch->unpackedMsaSize;
			//return 0;
		}
		cached_into_ram = true;
	}

	return 0;	// OK
}

/*
	Read sector from image
	Returns 0 OK, nonzero for any error
*/
int DFS_HostReadSector(uint8_t *buffer, uint32_t sector, uint32_t count)
{
	// fseek into an opened for writing file can extend the file on Windows and won't fail, so let's check bounds
	if ((int)(sector * SECTOR_SIZE) > file_size)
		return -1;

	if (cached_into_ram)
	{
		memcpy(buffer, &disk_cache[sector * SECTOR_SIZE], SECTOR_SIZE);
		return 0;
	}
	else
	{
		if (fseek(hostfile, sector * SECTOR_SIZE, SEEK_SET))
			return -1;

		fread(buffer, SECTOR_SIZE, count, hostfile);
		return 0;
	}
}

/*
	Write sector to image
	Returns 0 OK, nonzero for any error
*/
int DFS_HostWriteSector(uint8_t *buffer, uint32_t sector, uint32_t count)
{
	// fseek into an opened for writing file can extend the file on Windows and won't fail, so let's check bounds
	if ((int)(sector * SECTOR_SIZE) > file_size)
		return -1;

	if (cached_into_ram)
	{
		memcpy(&disk_cache[sector * SECTOR_SIZE], buffer, SECTOR_SIZE);
		return 0;
	}
	else
	{
		if (fseek(hostfile, sector * SECTOR_SIZE, SEEK_SET))
			return -1;

		fwrite(buffer, SECTOR_SIZE, count, hostfile);
		fflush(hostfile);
		return 0;
	}
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
		int n = p - prev;
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
	int sectors = arch->unpackedMsaSectors;
	int sides = arch->unpackedMsaSides;
	int start_track = 0;
	int end_track = arch->unpackedMsaEndTrack;

	unsigned char *packed_buffer = (unsigned char *)malloc(10 + end_track * (sectors * SECTOR_SIZE + 2) * sides+100000); // 10=header size, +2 bytes per track for writing the track size
	if (!packed_buffer) return 0;
	unsigned char *pack = packed_buffer;

	memcpy(pack + 0, "\x0e\x0f", 2);
	*(unsigned short *)(pack + 2) = ((unsigned short)(sectors << 8)) | ((unsigned short)(sectors >> 8));
	*(unsigned short *)(pack + 4) = ((unsigned short)((sides-1) << 8)) | ((unsigned short)((sides-1) >> 8));
	*(unsigned short *)(pack + 6) = 0;
	*(unsigned short *)(pack + 8) = ((unsigned short)(end_track << 8)) | ((unsigned short)(end_track >> 8));
	//fwrite(header, 10, 1, arch->fp);
	pack += 10;

	int track;
	unsigned char *p = disk_cache;
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
	file_size = pack - packed_buffer;
	return packed_buffer;
}

// ggn
int DFS_HostDetach(tArchive *arch)
{
	if (cached_into_ram)
	{
		if (!arch->volume_dirty)
		{
			free(disk_cache);
			return 0;
		}
		if (arch->mode == DISKMODE_MSA)
		{
			uint8_t *packed_msa = make_msa(arch);
			if (!packed_msa)
			{
				free(disk_cache); 
				return -1;
			}
			free(disk_cache);
			disk_cache = packed_msa;
		}		
		hostfile = fopen(arch->archname, "wb");
		if (!hostfile) return -1;
		fwrite(disk_cache, file_size, 1, hostfile);
		free(disk_cache);
		fclose(hostfile);
		return 0;
	}
	else
	{
		if (!hostfile) return -1;
		return fclose(hostfile);
	}
}
