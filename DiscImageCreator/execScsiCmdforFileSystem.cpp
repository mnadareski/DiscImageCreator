/**
 * Copyright 2011-2018 sarami
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "struct.h"
#include "check.h"
#include "convert.h"
#include "execScsiCmdforCD.h"
#include "execScsiCmdforCDCheck.h"
#include "execScsiCmdforFileSystem.h"
#include "execIoctl.h"
#include "get.h"
#include "output.h"
#include "outputScsiCmdLogforCD.h"
#include "outputScsiCmdLogforDVD.h"
#include "set.h"

BOOL ReadCDFor3DODirectory(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	CDB::_READ12* pCdb,
	LPCH pPath,
	INT nLBA
) {
	BOOL bRet = TRUE;
	LPBYTE pBuf = NULL;
	LPBYTE lpBuf = NULL;
	if (!GetAlignedCallocatedBuffer(pDevice, &pBuf,
		DISC_RAW_READ_SIZE, &lpBuf, _T(__FUNCTION__), __LINE__)) {
		return FALSE;
	}
	try {
		if (!ExecReadCD(pExtArg, pDevice, (LPBYTE)pCdb, nLBA, lpBuf,
			DISC_RAW_READ_SIZE, _T(__FUNCTION__), __LINE__)) {
			throw FALSE;
		}
		LONG lOfs = THREEDO_DIR_HEADER_SIZE;
		LONG lDirSize =
			MAKELONG(MAKEWORD(lpBuf[15], lpBuf[14]), MAKEWORD(lpBuf[13], lpBuf[12]));
		OutputFs3doDirectoryRecord(lpBuf, nLBA, pPath, lDirSize);

		// next dir
		CHAR szNewPath[_MAX_PATH] = { 0 };
		CHAR fname[32] = { 0 };
		while (lOfs < lDirSize) {
			LPBYTE lpDirEnt = lpBuf + lOfs;
			LONG lFlags = MAKELONG(
				MAKEWORD(lpDirEnt[3], lpDirEnt[2]), MAKEWORD(lpDirEnt[1], lpDirEnt[0]));
			strncpy(fname, (LPCH)&lpDirEnt[32], sizeof(fname));
			LONG lastCopy = MAKELONG(
				MAKEWORD(lpDirEnt[67], lpDirEnt[66]), MAKEWORD(lpDirEnt[65], lpDirEnt[64]));
			lOfs += THREEDO_DIR_ENTRY_SIZE;

			if ((lFlags & 0xff) == 7) {
				sprintf(szNewPath, "%s%s/", pPath, fname);
				if (!ReadCDFor3DODirectory(pExtArg, pDevice, pDisc, pCdb, szNewPath,
					MAKELONG(MAKEWORD(lpDirEnt[71], lpDirEnt[70]), MAKEWORD(lpDirEnt[69], lpDirEnt[68])))) {
					throw FALSE;
				}
			}
			for (LONG i = 0; i < lastCopy; i++) {
				lOfs += sizeof(LONG);
			}
		}
	}
	catch (BOOL bErr) {
		bRet = bErr;
	}
	FreeAndNull(pBuf);
	return bRet;
}

BOOL ReadCDForFileSystem(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc
) {
	BOOL bRet = TRUE;
	for (BYTE i = 0; i < pDisc->SCSI.toc.LastTrack; i++) {
		if ((pDisc->SCSI.toc.TrackData[i].Control & AUDIO_DATA_TRACK) == AUDIO_DATA_TRACK) {
			// for Label Gate CD, XCP
			if (i > 1 && pDisc->SCSI.lpLastLBAListOnToc[i] - pDisc->SCSI.lpFirstLBAListOnToc[i] + 1 <= 750) {
				return TRUE;
			}
			LPBYTE pBuf = NULL;
			LPBYTE lpBuf = NULL;
			if (!GetAlignedCallocatedBuffer(pDevice, &pBuf,
				pDevice->dwMaxTransferLength, &lpBuf, _T(__FUNCTION__), __LINE__)) {
				return FALSE;
			}
			CDB::_READ12 cdb = { 0 };
			cdb.OperationCode = SCSIOP_READ12;
			cdb.LogicalUnitNumber = pDevice->address.Lun;
			cdb.TransferLength[3] = 1;
			BOOL bVD = FALSE;
			PDIRECTORY_RECORD pDirRec = NULL;
			try {
				// general data track disc
				VOLUME_DESCRIPTOR volDesc;
				if (!ReadVolumeDescriptor(pExecType, pExtArg, pDevice, pDisc, i, (LPBYTE)&cdb, lpBuf, 16, 0, &bVD, &volDesc)) {
					throw FALSE;
				}
				if (bVD) {
					pDirRec = (PDIRECTORY_RECORD)calloc(DIRECTORY_RECORD_SIZE, sizeof(DIRECTORY_RECORD));
					if (!pDirRec) {
						OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
						throw FALSE;
					}
					INT nDirPosNum = 0;
					if (!ReadPathTableRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)&cdb
						, volDesc.ISO_9660.dwLogicalBlkCoef, volDesc.ISO_9660.dwPathTblSize
						, volDesc.ISO_9660.dwPathTblPos, 0, pDirRec, &nDirPosNum)) {
						throw FALSE;
					}
					if (!ReadDirectoryRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)&cdb, lpBuf
						, volDesc.ISO_9660.dwLogicalBlkCoef, volDesc.ISO_9660.dwRootDataLen, 0, pDirRec, nDirPosNum)) {
						OutputVolDescLogA("Failed to read ISO9660\n");
						nDirPosNum = 0;
						if (!ReadPathTableRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)&cdb
							, volDesc.JOLIET.dwLogicalBlkCoef, volDesc.JOLIET.dwPathTblSize
							, volDesc.JOLIET.dwPathTblPos, 0, pDirRec, &nDirPosNum)) {
							throw FALSE;
						}
						if (!ReadDirectoryRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)&cdb, lpBuf
							, volDesc.JOLIET.dwLogicalBlkCoef, volDesc.JOLIET.dwRootDataLen, 0, pDirRec, nDirPosNum)) {
							throw FALSE;
						}
					}
					if (!ReadCDForCheckingExe(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)&cdb, lpBuf)) {
						throw FALSE;
					}
					if (pDisc->PROTECT.byExist) {
						OutputLogA(standardOut | fileDisc, "Detected [%s], from %d to %d"
							, pDisc->PROTECT.name, pDisc->PROTECT.ERROR_SECTOR.nExtentPos
							, pDisc->PROTECT.ERROR_SECTOR.nExtentPos + pDisc->PROTECT.ERROR_SECTOR.nSectorSize);
						if (pDisc->PROTECT.byExist == microids) {
							OutputLogA(standardOut | fileDisc, ", %d to %d\n"
								, pDisc->PROTECT.ERROR_SECTOR.nExtentPos2nd
								, pDisc->PROTECT.ERROR_SECTOR.nExtentPos2nd + pDisc->PROTECT.ERROR_SECTOR.nSectorSize2nd);
						}
						else {
							OutputLogA(standardOut | fileDisc, "\n");
						}
					}
				}
				else {
					BOOL bOtherHeader = FALSE;
					// for pce, pc-fx
					INT nLBA = pDisc->SCSI.nFirstLBAofDataTrack;
					if (!ExecReadCD(pExtArg, pDevice, (LPBYTE)&cdb, nLBA, lpBuf,
						DISC_RAW_READ_SIZE, _T(__FUNCTION__), __LINE__)) {
						throw FALSE;
					}
					if (IsValidPceSector(lpBuf)) {
						OutputFsPceStuff(lpBuf, nLBA);
						nLBA = pDisc->SCSI.nFirstLBAofDataTrack + 1;
						if (!ExecReadCD(pExtArg, pDevice, (LPBYTE)&cdb, nLBA, lpBuf,
							DISC_RAW_READ_SIZE, _T(__FUNCTION__), __LINE__)) {
							throw FALSE;
						}
						OutputFsPceBootSector(lpBuf, nLBA);
						bOtherHeader = TRUE;
					}
					else if (IsValidPcfxSector(lpBuf)) {
						OutputFsPcfxHeader(lpBuf, nLBA);
						nLBA = pDisc->SCSI.nFirstLBAofDataTrack + 1;
						if (!ExecReadCD(pExtArg, pDevice, (LPBYTE)&cdb, nLBA, lpBuf,
							DISC_RAW_READ_SIZE, _T(__FUNCTION__), __LINE__)) {
							throw FALSE;
						}
						OutputFsPcfxSector(lpBuf, nLBA);
						bOtherHeader = TRUE;
					}

					if (!bOtherHeader) {
						// for 3DO
						nLBA = 0;
						if (!ExecReadCD(pExtArg, pDevice, (LPBYTE)&cdb, nLBA, lpBuf,
							DISC_RAW_READ_SIZE, _T(__FUNCTION__), __LINE__)) {
							throw FALSE;
						}
						if (IsValid3doDataHeader(lpBuf)) {
							OutputFs3doHeader(lpBuf, nLBA);
							if (!ReadCDFor3DODirectory(pExtArg, pDevice, pDisc, &cdb, "/",
								MAKELONG(MAKEWORD(lpBuf[103], lpBuf[102]),
									MAKEWORD(lpBuf[101], lpBuf[100])))) {
								throw FALSE;
							}
							bOtherHeader = TRUE;
						}
					}
					if (!bOtherHeader) {
						// for MAC pattern 1
						nLBA = 1;
						if (!ExecReadCD(pExtArg, pDevice, (LPBYTE)&cdb, nLBA, lpBuf,
							DISC_RAW_READ_SIZE, _T(__FUNCTION__), __LINE__)) {
							throw FALSE;
						}
						if (IsValidMacDataHeader(lpBuf + 1024)) {
							OutputFsMasterDirectoryBlocks(lpBuf + 1024, nLBA);
							bOtherHeader = TRUE;
						}
						else if (IsValidMacDataHeader(lpBuf + 512)) {
							OutputFsMasterDirectoryBlocks(lpBuf + 512, nLBA);
							bOtherHeader = TRUE;
						}
						// for MAC pattern 2
						nLBA = 16;
						if (!ExecReadCD(pExtArg, pDevice, (LPBYTE)&cdb, nLBA, lpBuf,
							DISC_RAW_READ_SIZE, _T(__FUNCTION__), __LINE__)) {
							throw FALSE;
						}
						if (IsValidMacDataHeader(lpBuf + 1024)) {
							OutputFsMasterDirectoryBlocks(lpBuf + 1024, nLBA);
							bOtherHeader = TRUE;
						}
					}
					if (bOtherHeader) {
						FreeAndNull(pBuf);
						break;
					}
				}
			}
			catch (BOOL bErr) {
				bRet = bErr;
			}
			FreeAndNull(pDirRec);
			FreeAndNull(pBuf);
		}
	}
	return bRet;
}

VOID ManageEndOfDirectoryRecord(
	LPINT nSectorNum,
	BYTE byTransferLen,
	UINT uiZeroPaddingNum,
	LPBYTE lpDirRec,
	LPUINT nOfs
) {
	if (*nSectorNum < byTransferLen) {
		UINT j = 0;
		for (; j < uiZeroPaddingNum; j++) {
			if (lpDirRec[j] != 0) {
				break;
			}
		}
		if (j == uiZeroPaddingNum) {
			*nOfs += uiZeroPaddingNum;
			(*nSectorNum)++;
			return;
		}
	}
	else {
		return;
	}
}

BOOL ReadDirectoryRecordDetail(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	LPBYTE pCdb,
	INT nLBA,
	LPBYTE lpBuf,
	LPBYTE bufDec,
	BYTE byTransferLen,
	INT nDirPosNum,
	DWORD dwLogicalBlkCoef,
	INT nOffset,
	PDIRECTORY_RECORD pDirRec
) {
	if (!ExecReadDisc(pExecType, pExtArg, pDevice, pDisc
		, pCdb, nLBA + nOffset, lpBuf, bufDec, byTransferLen)) {
		return FALSE;
	}
	for (BYTE i = 0; i < byTransferLen; i++) {
		OutputCDMain(fileMainInfo, lpBuf + DISC_RAW_READ_SIZE * i, nLBA + i, DISC_RAW_READ_SIZE);
	}

	UINT uiOfs = 0;
	for (INT nSectorNum = 0; nSectorNum < byTransferLen;) {
		if (*(lpBuf + uiOfs) == 0) {
			break;
		}
		OutputVolDescLogA(
			OUTPUT_DHYPHEN_PLUS_STR_WITH_LBA_F(Directory Record), nLBA + nSectorNum, nLBA + nSectorNum);
		for (;;) {
			CHAR szCurDirName[MAX_FNAME_FOR_VOLUME] = { 0 };
			LPBYTE lpDirRec = lpBuf + uiOfs;
			if (lpDirRec[0] >= MIN_LEN_DR) {
				if (lpDirRec[0] == MIN_LEN_DR && uiOfs > 0 && uiOfs % DISC_RAW_READ_SIZE == 0) {
					// SimCity 3000 (USA)
					OutputVolDescLogA(
						"Direcory record size of the %d sector maybe incorrect. Skip the reading of this sector\n", nLBA);
					nSectorNum++;
					break;
				}
				DWORD dwMaxByte = DWORD(pDisc->SCSI.nAllLength * DISC_RAW_READ_SIZE);
				if (pDisc->SCSI.nAllLength >= 0x200000) {
					dwMaxByte = 0xffffffff;
				}
				DWORD dwExtentPos = GetSizeOrDwordForVolDesc(lpDirRec + 2, dwMaxByte) / dwLogicalBlkCoef;
				DWORD dwDataLen = GetSizeOrDwordForVolDesc(lpDirRec + 10, dwMaxByte);
				if (dwDataLen >= dwMaxByte) {
					OutputVolDescLogA(
						"Data length is incorrect. Skip the reading of this sector\n");
					nSectorNum++;
					break;
				}
				OutputFsDirectoryRecord(
					pExtArg, pDisc, lpDirRec, dwExtentPos, dwDataLen, szCurDirName);
				OutputVolDescLogA("\n");
				uiOfs += lpDirRec[0];

				if ((lpDirRec[25] & 0x02 || (pDisc->SCSI.byCdi && lpDirRec[25] == 0))
					&& !(lpDirRec[32] == 1 && szCurDirName[0] == 0)
					&& !(lpDirRec[32] == 1 && szCurDirName[0] == 1)) {
					// not upper and current directory 
					for (INT i = 1; i < nDirPosNum; i++) {
						if (dwExtentPos == pDirRec[i].uiPosOfDir &&
							!_strnicmp(szCurDirName, pDirRec[i].szDirName, MAX_FNAME_FOR_VOLUME)) {
							pDirRec[i].uiDirSize = PadSizeForVolDesc(dwDataLen);
							break;
						}
					}
				}
				if (uiOfs == (UINT)(DISC_RAW_READ_SIZE * (nSectorNum + 1))) {
					nSectorNum++;
					break;
				}
			}
			else {
				UINT uiZeroPaddingNum = DISC_RAW_READ_SIZE * (nSectorNum + 1) - uiOfs;
				if (uiZeroPaddingNum > MIN_LEN_DR) {
					BYTE byNextLenDR = lpDirRec[MIN_LEN_DR];
					if (byNextLenDR >= MIN_LEN_DR) {
						// Amiga Tools 4 : The second of Direcory Record (0x22 - 0x43) is corrupt...
						// ========== LBA[040915, 0x09fd3]: Main Channel ==========
						//        +0 +1 +2 +3 +4 +5 +6 +7  +8 +9 +A +B +C +D +E +F
						// 0000 : 22 00 D3 9F 00 00 00 00  9F D3 00 08 00 00 00 00   "...............
						// 0010 : 08 00 60 02 1D 17 18 2C  00 02 00 00 01 00 00 01   ..`....,........
						// 0020 : 01 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
						// 0030 : 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
						// 0040 : 00 00 01 01 2E 00 09 A0  00 00 00 00 A0 09 D8 01   ................
						OutputMainErrorWithLBALogA(
							"Direcory Record is corrupt. Skip reading from %d to %d byte. See _mainInfo.txt\n"
							, nLBA, 0, uiOfs, uiOfs + MIN_LEN_DR - 1);
						uiOfs += MIN_LEN_DR;
						break;
					}
					else {
						ManageEndOfDirectoryRecord(&nSectorNum, byTransferLen, uiZeroPaddingNum, lpDirRec, &uiOfs);
						break;
					}
				}
				else {
					ManageEndOfDirectoryRecord(&nSectorNum, byTransferLen, uiZeroPaddingNum, lpDirRec, &uiOfs);
					break;
				}
			}
		}
	}
	return TRUE;
}

BOOL ReadDirectoryRecord(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	LPBYTE pCdb,
	LPBYTE lpBuf,
	DWORD dwLogicalBlkCoef,
	DWORD dwRootDataLen,
	INT nSectorOfs,
	PDIRECTORY_RECORD pDirRec,
	INT nDirPosNum
) {
	LPBYTE bufDec = NULL;
	if (*pExecType == gd) {
		bufDec = (LPBYTE)calloc(pDevice->dwMaxTransferLength, sizeof(BYTE));
		if (!bufDec) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			return FALSE;
		}
	}
	BYTE byTransferLen = 1;
	// for CD-I
	if (dwRootDataLen == 0) {
		if (!ExecReadDisc(pExecType, pExtArg, pDevice, pDisc, pCdb
			, (INT)pDirRec[0].uiPosOfDir + nSectorOfs, lpBuf, bufDec, byTransferLen)) {
			return FALSE;
		}
		dwRootDataLen =
			PadSizeForVolDesc(GetSizeOrDwordForVolDesc(lpBuf + 10, (DWORD)(pDisc->SCSI.nAllLength * DISC_RAW_READ_SIZE)));
	}
	pDirRec[0].uiDirSize = dwRootDataLen;

	for (INT nDirRecIdx = 0; nDirRecIdx < nDirPosNum; nDirRecIdx++) {
		INT nLBA = (INT)pDirRec[nDirRecIdx].uiPosOfDir;
		if (pDirRec[nDirRecIdx].uiDirSize > pDevice->dwMaxTransferLength) {
			// [FMT] Psychic Detective Series Vol. 4 - Orgel (Japan) (v1.0)
			// [FMT] Psychic Detective Series Vol. 5 - Nightmare (Japan)
			// [IBM - PC compatible] Maria 2 - Jutai Kokuchi no Nazo (Japan) (Disc 1)
			// [IBM - PC compatible] PC Game Best Series Vol. 42 - J.B. Harold Series - Kiss of Murder - Satsui no Kuchizuke (Japan)
			// [SS] Madou Monogatari (Japan)
			// and more
			DWORD dwAdditionalTransferLen = pDirRec[nDirRecIdx].uiDirSize / pDevice->dwMaxTransferLength;
			SetCommandForTransferLength(pExecType, pDevice, pCdb, pDevice->dwMaxTransferLength, &byTransferLen);
			OutputMainInfoLogA("nLBA %d, uiDirSize: %lu, byTransferLen: %d [L:%d]\n"
				, nLBA, pDevice->dwMaxTransferLength, byTransferLen, (INT)__LINE__);

			for (DWORD n = 0; n < dwAdditionalTransferLen; n++) {
				if (!ReadDirectoryRecordDetail(pExecType, pExtArg, pDevice, pDisc, pCdb, nLBA
					, lpBuf, bufDec, byTransferLen, nDirPosNum, dwLogicalBlkCoef, nSectorOfs, pDirRec)) {
					continue;
				}
				nLBA += byTransferLen;
			}
			DWORD dwLastTblSize = pDirRec[nDirRecIdx].uiDirSize % pDevice->dwMaxTransferLength;
			SetCommandForTransferLength(pExecType, pDevice, pCdb, dwLastTblSize, &byTransferLen);
			OutputMainInfoLogA("nLBA %d, uiDirSize: %lu, byTransferLen: %d [L:%d]\n"
				, nLBA, dwLastTblSize, byTransferLen, (INT)__LINE__);

			if (!ReadDirectoryRecordDetail(pExecType, pExtArg, pDevice, pDisc, pCdb, nLBA
				, lpBuf, bufDec, byTransferLen, nDirPosNum, dwLogicalBlkCoef, nSectorOfs, pDirRec)) {
				continue;
			}
		}
		else {
			if (pDirRec[nDirRecIdx].uiDirSize == 0 || &byTransferLen == 0) {
				OutputMainErrorLogA("Directory Record is invalid\n");
				return FALSE;
			}
			SetCommandForTransferLength(pExecType, pDevice, pCdb, pDirRec[nDirRecIdx].uiDirSize, &byTransferLen);
			OutputMainInfoLogA("nLBA %d, uiDirSize: %u, byTransferLen: %d [L:%d]\n"
				, nLBA, pDirRec[nDirRecIdx].uiDirSize, byTransferLen, (INT)__LINE__);

			if (!ReadDirectoryRecordDetail(pExecType, pExtArg, pDevice, pDisc, pCdb, nLBA
				, lpBuf, bufDec, byTransferLen, nDirPosNum, dwLogicalBlkCoef, nSectorOfs, pDirRec)) {
				continue;
			}
		}
		OutputString(_T("\rReading DirectoryRecord %4d/%4d"), nDirRecIdx + 1, nDirPosNum);
	}
	OutputString(_T("\n"));
	return TRUE;
}

BOOL ReadPathTableRecord(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	LPBYTE pCdb,
	DWORD dwLogicalBlkCoef,
	DWORD dwPathTblSize,
	DWORD dwPathTblPos,
	INT nSectorOfs,
	PDIRECTORY_RECORD pDirRec,
	LPINT nDirPosNum
) {
	BYTE byTransferLen = 1;
	SetCommandForTransferLength(pExecType, pDevice, pCdb, dwPathTblSize, &byTransferLen);

	DWORD dwBufSize = 0;
	if (*pExecType == gd) {
		dwBufSize = (CD_RAW_SECTOR_SIZE - (dwPathTblSize % CD_RAW_SECTOR_SIZE) + dwPathTblSize) * byTransferLen * 2;
	}
	else {
		dwBufSize = DISC_RAW_READ_SIZE - (dwPathTblSize % DISC_RAW_READ_SIZE) + dwPathTblSize;
	}
	LPBYTE lpBuf = (LPBYTE)calloc(dwBufSize, sizeof(BYTE));
	if (!lpBuf) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}

	BOOL bRet = TRUE;
	try {
		LPBYTE bufDec = NULL;
		if (*pExecType == gd) {
			if (NULL == (bufDec = (LPBYTE)calloc(size_t(CD_RAW_SECTOR_SIZE * byTransferLen), sizeof(BYTE)))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				return FALSE;
			}
		}
		if (dwPathTblSize > pDevice->dwMaxTransferLength) {
			DWORD dwAdditionalTransferLen = dwPathTblSize / pDevice->dwMaxTransferLength;
			SetCommandForTransferLength(pExecType, pDevice, pCdb, pDevice->dwMaxTransferLength, &byTransferLen);
			OutputMainInfoLogA("dwPathTblSize: %lu, byTransferLen: %d [L:%d]\n"
				, pDevice->dwMaxTransferLength, byTransferLen, (INT)__LINE__);

			for (DWORD n = 0; n < dwAdditionalTransferLen; n++) {
				if (!ExecReadDisc(pExecType, pExtArg, pDevice, pDisc, pCdb
					, (INT)dwPathTblPos + nSectorOfs, lpBuf + pDevice->dwMaxTransferLength * n, bufDec, byTransferLen)) {
					throw FALSE;
				}
				for (BYTE i = 0; i < byTransferLen; i++) {
					OutputCDMain(fileMainInfo, lpBuf + DISC_RAW_READ_SIZE * i, (INT)dwPathTblPos + i, DISC_RAW_READ_SIZE);
				}
				dwPathTblPos += byTransferLen;
			}
			DWORD dwLastPathTblSize = dwPathTblSize % pDevice->dwMaxTransferLength;
			SetCommandForTransferLength(pExecType, pDevice, pCdb, dwLastPathTblSize, &byTransferLen);
			OutputMainInfoLogA(
				"dwPathTblSize: %lu, byTransferLen: %d [L:%d]\n", dwLastPathTblSize, byTransferLen, (INT)__LINE__);

			DWORD dwBufOfs = pDevice->dwMaxTransferLength * dwAdditionalTransferLen;
			if (!ExecReadDisc(pExecType, pExtArg, pDevice, pDisc, pCdb
				, (INT)dwPathTblPos + nSectorOfs, lpBuf + dwBufOfs, bufDec, byTransferLen)) {
				throw FALSE;
			}
			for (BYTE i = 0; i < byTransferLen; i++) {
				OutputCDMain(fileMainInfo, lpBuf + dwBufOfs + DISC_RAW_READ_SIZE * i, (INT)dwPathTblPos + i, DISC_RAW_READ_SIZE);
			}
			if (!OutputFsPathTableRecord(pDisc, lpBuf, dwLogicalBlkCoef, dwPathTblPos, dwPathTblSize, pDirRec, nDirPosNum)) {
				throw FALSE;
			}
		}
		else {
			if (!ExecReadDisc(pExecType, pExtArg, pDevice, pDisc, pCdb
				, (INT)dwPathTblPos + nSectorOfs, lpBuf, bufDec, byTransferLen)) {
				throw FALSE;
			}
			OutputMainInfoLogA(
				"dwPathTblSize: %lu, byTransferLen: %d [L:%d]\n", dwPathTblSize, byTransferLen, (INT)__LINE__);
			for (BYTE i = 0; i < byTransferLen; i++) {
				OutputCDMain(fileMainInfo, lpBuf + DISC_RAW_READ_SIZE * i, (INT)dwPathTblPos + i, DISC_RAW_READ_SIZE);
			}
			if (!OutputFsPathTableRecord(pDisc, lpBuf, dwLogicalBlkCoef, dwPathTblPos, dwPathTblSize, pDirRec, nDirPosNum)) {
				throw FALSE;
			}
		}
		OutputVolDescLogA("Directory Num: %u\n", *nDirPosNum);
	}
	catch (BOOL ret) {
		bRet = ret;
	}
	FreeAndNull(lpBuf);
	return bRet;
}

BOOL ReadVolumeDescriptor(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	BYTE byIdx,
	LPBYTE pCdb,
	LPBYTE lpBuf,
	INT nPVD,
	INT nSectorOfs,
	LPBOOL lpReadVD,
	PVOLUME_DESCRIPTOR pVolDesc
) {
	if (pDisc->SCSI.lpFirstLBAListOnToc) {
		nPVD += pDisc->SCSI.lpFirstLBAListOnToc[byIdx];
	}
	INT nTmpLBA = nPVD;
	BYTE bufDec[CD_RAW_SECTOR_SIZE] = { 0 };
	for (;;) {
		if (!ExecReadDisc(pExecType, pExtArg, pDevice, pDisc, pCdb, nTmpLBA + nSectorOfs, lpBuf, bufDec, 1)) {
			break;
		}
		if (!strncmp((LPCH)&lpBuf[1], "CD001", 5) ||
			(pDisc->SCSI.byCdi && !strncmp((LPCH)&lpBuf[1], "CD-I ", 5))) {
			if (nTmpLBA == nPVD) {
				DWORD dwLogicalBlkSize = GetSizeOrWordForVolDesc(lpBuf + 128);
				pVolDesc->ISO_9660.dwLogicalBlkCoef = (BYTE)(DISC_RAW_READ_SIZE / dwLogicalBlkSize);
				pVolDesc->ISO_9660.dwPathTblSize =
					GetSizeOrDwordForVolDesc(lpBuf + 132, (DWORD)(pDisc->SCSI.nAllLength * DISC_RAW_READ_SIZE));
				pVolDesc->ISO_9660.dwPathTblPos = MAKEDWORD(MAKEWORD(lpBuf[140], lpBuf[141]),
					MAKEWORD(lpBuf[142], lpBuf[143])) / pVolDesc->ISO_9660.dwLogicalBlkCoef;
				if (pVolDesc->ISO_9660.dwPathTblPos == 0) {
					pVolDesc->ISO_9660.dwPathTblPos = MAKEDWORD(MAKEWORD(lpBuf[151], lpBuf[150]),
						MAKEWORD(lpBuf[149], lpBuf[148]));
				}
				pVolDesc->ISO_9660.dwRootDataLen =
					GetSizeOrDwordForVolDesc(lpBuf + 166, (DWORD)(pDisc->SCSI.nAllLength * DISC_RAW_READ_SIZE));
				if (pVolDesc->ISO_9660.dwRootDataLen > 0) {
					pVolDesc->ISO_9660.dwRootDataLen = PadSizeForVolDesc(pVolDesc->ISO_9660.dwRootDataLen);
				}
				*lpReadVD = TRUE;
			}
			else if (lpBuf[0] == 2) {
				DWORD dwLogicalBlkSize = GetSizeOrWordForVolDesc(lpBuf + 128);
				pVolDesc->JOLIET.dwLogicalBlkCoef = (BYTE)(DISC_RAW_READ_SIZE / dwLogicalBlkSize);
				pVolDesc->JOLIET.dwPathTblSize =
					GetSizeOrDwordForVolDesc(lpBuf + 132, (DWORD)(pDisc->SCSI.nAllLength * DISC_RAW_READ_SIZE));
				pVolDesc->JOLIET.dwPathTblPos = MAKEDWORD(MAKEWORD(lpBuf[140], lpBuf[141]),
					MAKEWORD(lpBuf[142], lpBuf[143])) / pVolDesc->JOLIET.dwLogicalBlkCoef;
				if (pVolDesc->JOLIET.dwPathTblPos == 0) {
					pVolDesc->JOLIET.dwPathTblPos = MAKEDWORD(MAKEWORD(lpBuf[151], lpBuf[150]),
						MAKEWORD(lpBuf[149], lpBuf[148]));
				}
				pVolDesc->JOLIET.dwRootDataLen =
					GetSizeOrDwordForVolDesc(lpBuf + 166, (DWORD)(pDisc->SCSI.nAllLength * DISC_RAW_READ_SIZE));
				if (pVolDesc->JOLIET.dwRootDataLen > 0) {
					pVolDesc->JOLIET.dwRootDataLen = PadSizeForVolDesc(pVolDesc->JOLIET.dwRootDataLen);
				}
				*lpReadVD = TRUE;
			}
			OutputCDMain(fileMainInfo, lpBuf, nTmpLBA, DISC_RAW_READ_SIZE);
			OutputFsVolumeDescriptor(pExtArg, pDisc, lpBuf, nTmpLBA++);
		}
		else {
			break;
		}
	}
	return TRUE;
}

BOOL ReadGDForFileSystem(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc
) {
	LPBYTE pBuf = NULL;
	LPBYTE lpBuf = NULL;
	if (!GetAlignedCallocatedBuffer(pDevice, &pBuf,
		pDevice->dwMaxTransferLength, &lpBuf, _T(__FUNCTION__), __LINE__)) {
		return FALSE;
	}
	BOOL bRet = TRUE;
	PDIRECTORY_RECORD pDirRec = NULL;
	CDB::_READ_CD cdb = { 0 };
	SetReadCDCommand(pDevice, &cdb,
		CDFLAG::_READ_CD::CDDA, 1, CDFLAG::_READ_CD::NoC2, CDFLAG::_READ_CD::NoSub);
	try {
		INT nSectorOfs = pDisc->MAIN.nAdjustSectorNum - 1;
		if (pDisc->MAIN.nCombinedOffset < 0) {
			nSectorOfs = pDisc->MAIN.nAdjustSectorNum;
		}
		BOOL bVD = FALSE;
		VOLUME_DESCRIPTOR volDesc;
		if (!ReadVolumeDescriptor(pExecType, pExtArg, pDevice, pDisc, 0
			, (LPBYTE)&cdb, lpBuf, FIRST_LBA_FOR_GD + 16, nSectorOfs, &bVD, &volDesc)) {
			throw FALSE;
		}
		if (bVD) {
			pDirRec = (PDIRECTORY_RECORD)calloc(DIRECTORY_RECORD_SIZE, sizeof(DIRECTORY_RECORD));
			if (!pDirRec) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			INT nDirPosNum = 0;
			if (!ReadPathTableRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)&cdb
				, volDesc.ISO_9660.dwLogicalBlkCoef, volDesc.ISO_9660.dwPathTblSize
				, volDesc.ISO_9660.dwPathTblPos, nSectorOfs, pDirRec, &nDirPosNum)) {
				throw FALSE;
			}
			if (!ReadDirectoryRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)&cdb, lpBuf
				, volDesc.ISO_9660.dwLogicalBlkCoef, volDesc.ISO_9660.dwRootDataLen, nSectorOfs, pDirRec, nDirPosNum)) {
				throw FALSE;
			}
		}
	}
	catch (BOOL ret) {
		bRet = ret;
	}
	FreeAndNull(pDirRec);
	FreeAndNull(pBuf);
	return TRUE;
}

BOOL ReadDVDForFileSystem(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	CDB::_READ12* cdb,
	LPBYTE lpBuf
) {
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, cdb, CDB12GENERIC_LENGTH, lpBuf,
		pDevice->dwMaxTransferLength, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		return FALSE;
	}
	INT nLBA = 16;
	BOOL bPVD = FALSE;
	VOLUME_DESCRIPTOR volDesc;
	if (!ReadVolumeDescriptor(pExecType, pExtArg, pDevice, pDisc, 0, (LPBYTE)cdb, lpBuf, 16, 0, &bPVD, &volDesc)) {
		return FALSE;
	}
	if (bPVD) {
		PDIRECTORY_RECORD pDirRec = (PDIRECTORY_RECORD)calloc(DIRECTORY_RECORD_SIZE, sizeof(DIRECTORY_RECORD));
		if (!pDirRec) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			return FALSE;
		}
		INT nDirPosNum = 0;
		if (!ReadPathTableRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)cdb
			, volDesc.ISO_9660.dwLogicalBlkCoef, volDesc.ISO_9660.dwPathTblSize
			, volDesc.ISO_9660.dwPathTblPos, 0, pDirRec, &nDirPosNum)) {
			FreeAndNull(pDirRec);
			return FALSE;
		}
		if (!ReadDirectoryRecord(pExecType, pExtArg, pDevice, pDisc, (LPBYTE)cdb, lpBuf
			, volDesc.ISO_9660.dwLogicalBlkCoef, volDesc.ISO_9660.dwRootDataLen, 0, pDirRec, nDirPosNum)) {
			FreeAndNull(pDirRec);
			return FALSE;
		}
		FreeAndNull(pDirRec);
	}

	INT nStart = DISC_RAW_READ_SIZE * nLBA;
	INT nEnd = DISC_RAW_READ_SIZE * 32;
	for (INT i = nStart; i <= nEnd; i += DISC_RAW_READ_SIZE, nLBA++) {
		OutputFsVolumeRecognitionSequence(lpBuf + i, nLBA);
	}

	nLBA = 32;
	DWORD dwTransferLen = pDevice->dwMaxTransferLength / DISC_RAW_READ_SIZE;
	cdb->LogicalUnitNumber = pDevice->address.Lun;
	REVERSE_BYTES(&cdb->TransferLength, &dwTransferLen);
	cdb->LogicalBlock[0] = 0;
	cdb->LogicalBlock[1] = 0;
	cdb->LogicalBlock[2] = 0;
	cdb->LogicalBlock[3] = (UCHAR)nLBA;

	if (!ScsiPassThroughDirect(pExtArg, pDevice, cdb, CDB12GENERIC_LENGTH, lpBuf,
		pDevice->dwMaxTransferLength, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		return FALSE;
	}
	if (lpBuf[20] == 0 && lpBuf[21] == 0 && lpBuf[22] == 0 && lpBuf[23] == 0) {
		for (INT i = 0; i <= nEnd; i += DISC_RAW_READ_SIZE, nLBA++) {
			OutputFsVolumeDescriptorSequence(lpBuf + i, nLBA);
		}
	}

	cdb->LogicalBlock[2] = 1;
	cdb->LogicalBlock[3] = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, cdb, CDB12GENERIC_LENGTH, lpBuf,
		pDevice->dwMaxTransferLength, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		return FALSE;
	}
	nLBA = 256;
	OutputFsVolumeDescriptorSequence(lpBuf, nLBA);
	return TRUE;
}
