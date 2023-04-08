/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for Petit FatFs (C)ChaN, 2014      */
/*-----------------------------------------------------------------------*/

#include "pff.h"		/* Petit FatFs configurations and declarations */
#include "diskio.h"
#include <stdio.h>
#include "..\wcxhead.h"
#include "..\msast.h"

extern tArchive* pCurrentArchive;

/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (void)
{
	DSTATUS stat;

	// Put your code here
	stat = RES_OK;
	return stat;
}



/*-----------------------------------------------------------------------*/
/* Read Partial Sector                                                   */
/*-----------------------------------------------------------------------*/

DRESULT disk_readp (
	BYTE* buff,		/* Pointer to the destination object */
	DWORD sector,	/* Sector number (LBA) */
	UINT offset,	/* Offset in the sector */
	UINT count		/* Byte count (bit15:destination) */
)
{
	DRESULT res;

	if (pCurrentArchive == NULL) {
		return RES_NOTRDY;
	}
	
	if(pCurrentArchive->mode==DISKMODE_LINEAR){
		fseek(pCurrentArchive->fp, sector * 512 + offset, SEEK_SET);
		fread(buff, 1, count, pCurrentArchive->fp);
	}else if(pCurrentArchive->mode == DISKMODE_MSA && pCurrentArchive->unpackedMsa!=NULL){
		memcpy(buff,pCurrentArchive->unpackedMsa+ sector * 512 + offset,count);
	}else{
		return RES_NOTRDY;
	}
	// Put your code here
	res = RES_OK;
	return res;
}



/*-----------------------------------------------------------------------*/
/* Write Partial Sector                                                  */
/*-----------------------------------------------------------------------*/

DRESULT disk_writep (
	BYTE* buff,		/* Pointer to the data to be written, NULL:Initiate/Finalize write operation */
	DWORD sc		/* Sector number (LBA) or Number of bytes to send */
)
{
	DRESULT res=RES_OK;

	if (pCurrentArchive == NULL) {
		return RES_NOTRDY;
	}

	if (!buff) {
		if (sc) {

			// Initiate write process

		} else {

			// Finalize write process

		}
	} else {

		// Send data to the disk

	}

	return res;
}

