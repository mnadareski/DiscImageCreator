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
#include "calcHash.h"
#include "check.h"
#include "convert.h"
#include "execScsiCmd.h"
#include "execScsiCmdforCD.h"
#include "execScsiCmdforCDCheck.h"
#include "execIoctl.h"
#include "get.h"
#include "init.h"
#include "output.h"
#include "outputScsiCmdLog.h"
#include "outputScsiCmdLogforCD.h"
#include "set.h"

// These global variable is set at prngcd.cpp
extern unsigned char scrambled_table[2352];

BOOL ExecReadDisc(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	LPBYTE pCdb,
	INT nLBA,
	LPBYTE lpBuf,
	LPBYTE bufDec,
	BYTE byTransferLen
) {
	if (*pExecType == gd) {
		if (!ExecReadGD(pExtArg, pDevice, pDisc, pCdb, nLBA, byTransferLen, lpBuf, bufDec)) {
			return FALSE;
		}
		for (BYTE i = 0; i < byTransferLen; i++) {
			memcpy(lpBuf + DISC_RAW_READ_SIZE * i, bufDec + CD_RAW_SECTOR_SIZE * i + 16, DISC_RAW_READ_SIZE);
		}
	}
	else {
		if (!ExecReadCD(pExtArg, pDevice, pCdb, nLBA, lpBuf,
			(DWORD)(DISC_RAW_READ_SIZE * byTransferLen), _T(__FUNCTION__), __LINE__)) {
			return FALSE;
		}
	}
	return TRUE;
}

BOOL ExecReadCD(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	LPBYTE lpCmd,
	INT nLBA,
	LPBYTE lpBuf,
	DWORD dwBufSize,
	LPCTSTR pszFuncName,
	LONG lLineNum
) {
	REVERSE_BYTES(&lpCmd[2], &nLBA);
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, lpCmd, CDB12GENERIC_LENGTH
		, lpBuf, dwBufSize, &byScsiStatus, pszFuncName, lLineNum)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		OutputLogA(standardError | fileMainError,
			"lpCmd: %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x\n"
			"dwBufSize: %lu\n"
			, lpCmd[0], lpCmd[1], lpCmd[2], lpCmd[3], lpCmd[4], lpCmd[5]
			, lpCmd[6], lpCmd[7], lpCmd[8], lpCmd[9], lpCmd[10], lpCmd[11]
			, dwBufSize
		);
		return FALSE;
	}
	return TRUE;
}

BOOL ExecReadGD(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	LPBYTE pCdb,
	INT nLBA,
	BYTE byTransferLen,
	LPBYTE lpInBuf,
	LPBYTE lpOutBuf
) {
	for (INT n = 1; n <= 10; n++) {
		BOOL bRet = TRUE;
		if (!ExecReadCD(pExtArg, pDevice, pCdb, nLBA, lpInBuf,
			CD_RAW_SECTOR_SIZE * (DWORD)byTransferLen, _T(__FUNCTION__), __LINE__)) {
			if (n == 10) {
				return FALSE;
			}
			StartStopUnit(pExtArg, pDevice, STOP_UNIT_CODE, STOP_UNIT_CODE);
			DWORD milliseconds = 10000;
			OutputErrorString(_T("Retry %d/10 after %ld milliseconds\n"), n, milliseconds);
			Sleep(milliseconds);
			bRet = FALSE;
		}
		if (!bRet) {
			continue;
		}
		else {
			break;
		}
	}
	INT nOfs = pDisc->MAIN.nCombinedOffset;
	if (pDisc->MAIN.nCombinedOffset < 0) {
		nOfs = CD_RAW_SECTOR_SIZE + pDisc->MAIN.nCombinedOffset;
	}
#if 0
	OutputCDMain(fileMainInfo, lpInBuf + idx, nLBA, CD_RAW_SECTOR_SIZE);
#endif
	for (BYTE i = 0; i < byTransferLen; i++) {
		for (INT j = 0; j < CD_RAW_SECTOR_SIZE; j++) {
			lpOutBuf[j + CD_RAW_SECTOR_SIZE * i] =
				(BYTE)(lpInBuf[(nOfs + j) + CD_RAW_SECTOR_SIZE * i] ^ scrambled_table[j]);
		}
	}
#if 0
	OutputCDMain(fileMainInfo, lpOutBuf, nLBA, CD_RAW_SECTOR_SIZE);
#endif
	return TRUE;
}

BOOL ExecReadCDForC2(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	LPBYTE lpCmd,
	INT nLBA,
	LPBYTE lpBuf,
	LPCTSTR pszFuncName,
	LONG lLineNum
) {
	REVERSE_BYTES(&lpCmd[2], &nLBA);
	BYTE byTransferLen = lpCmd[9];
	if (lpCmd[0] != 0xd8) {
		byTransferLen = lpCmd[8];
	}
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, lpCmd, CDB12GENERIC_LENGTH, lpBuf,
		pDevice->TRANSFER.dwBufLen * byTransferLen, &byScsiStatus, pszFuncName, lLineNum)) {
		if (pExtArg->byScanProtectViaFile) {
			return RETURNED_CONTINUE;
		}
		else {
			return RETURNED_FALSE;
		}
	}
	if (byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		if (*pExecType != gd) {
			return RETURNED_CONTINUE;
		}
		else {
			return RETURNED_FALSE;
		}
	}
	return RETURNED_NO_C2_ERROR_1ST;
}

// http://tmkk.undo.jp/xld/secure_ripping.html
// https://forum.dbpoweramp.com/showthread.php?33676
BOOL FlushDriveCache(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	INT nLBA
) {
	if (nLBA < 449850 && nLBA % pExtArg->dwCacheDelNum == 0) {
		CDB::_READ12 cdb = { 0 };
		cdb.OperationCode = SCSIOP_READ12;
		cdb.ForceUnitAccess = TRUE;
		cdb.LogicalUnitNumber = pDevice->address.Lun;
		INT NextLBAAddress = nLBA + 1;
		REVERSE_BYTES(&cdb.LogicalBlock, &NextLBAAddress);
		BYTE byScsiStatus = 0;
		if (!ScsiPassThroughDirect(pExtArg, pDevice, (LPBYTE)&cdb, CDB12GENERIC_LENGTH,
			NULL, 0, &byScsiStatus, _T(__FUNCTION__), __LINE__)
			|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			return FALSE;
		}
	}
	return TRUE;
}

BOOL ProcessReadCD(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	PDISC_PER_SECTOR pDiscPerSector,
	LPBYTE lpCmd,
	INT nLBA
) {
	BOOL bRet = RETURNED_NO_C2_ERROR_1ST;
	if (*pExecType != gd && !pExtArg->byMultiSession && pDevice->bySuccessReadTocFull) {
		if (pDisc->SCSI.nFirstLBAof2ndSession != -1) {
			if (pExtArg->byReverse) {
				if (pDisc->SCSI.nFirstLBAof2ndSession == nLBA + 1) {
					OutputMainInfoLogA(
						"Skip from Leadout of Session 1 [%d, %#x] to Leadin of Session 2 [%d, %#x]\n",
						pDisc->SCSI.nFirstLBAofLeadout, pDisc->SCSI.nFirstLBAofLeadout,
						pDisc->SCSI.nFirstLBAof2ndSession - 1, pDisc->SCSI.nFirstLBAof2ndSession - 1);
					pDiscPerSector->subQ.prev.nAbsoluteTime = nLBA - SESSION_TO_SESSION_SKIP_LBA - 150;
					return RETURNED_SKIP_LBA;
				}
			}
			else {
				if (pDisc->MAIN.nFixFirstLBAofLeadout == nLBA) {
					OutputMainInfoLogA(
						"Skip from Leadout of Session 1 [%d, %#x] to Leadin of Session 2 [%d, %#x]\n",
						pDisc->SCSI.nFirstLBAofLeadout, pDisc->SCSI.nFirstLBAofLeadout,
						pDisc->SCSI.nFirstLBAof2ndSession - 1, pDisc->SCSI.nFirstLBAof2ndSession - 1);
					if (pDisc->MAIN.nCombinedOffset > 0) {
						pDiscPerSector->subQ.prev.nAbsoluteTime =
							nLBA + SESSION_TO_SESSION_SKIP_LBA + 150 - pDisc->MAIN.nAdjustSectorNum - 1;
					}
					else if (pDisc->MAIN.nCombinedOffset < 0) {
						pDiscPerSector->subQ.prev.nAbsoluteTime =
							nLBA + SESSION_TO_SESSION_SKIP_LBA + 150 + pDisc->MAIN.nAdjustSectorNum;
					}
					return RETURNED_SKIP_LBA;
				}
			}
		}
	}
	if (pExtArg->byFua || pDisc->SUB.nCorruptCrcH == 1 || pDisc->SUB.nCorruptCrcL == 1) {
		FlushDriveCache(pExtArg, pDevice, nLBA);
		pDisc->SUB.nCorruptCrcH = 0;
		pDisc->SUB.nCorruptCrcL = 0;
	}
	bRet = ExecReadCDForC2(pExecType, pExtArg, pDevice, lpCmd, nLBA,
		pDiscPerSector->data.current, _T(__FUNCTION__), __LINE__);

	if (pDevice->byPlxtrDrive) {
		if (bRet == RETURNED_NO_C2_ERROR_1ST) {
			AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
#if 0
			if (!(pExtArg->byScanProtectViaFile && pDisc->PROTECT.byExist &&
				(pDisc->PROTECT.ERROR_SECTOR.nExtentPos <= nLBA &&
					nLBA <= pDisc->PROTECT.ERROR_SECTOR.nExtentPos + pDisc->PROTECT.ERROR_SECTOR.nSectorSize) ||
					(pDisc->PROTECT.byExist == microids && pDisc->PROTECT.ERROR_SECTOR.nExtentPos2nd <= nLBA &&
					nLBA <= pDisc->PROTECT.ERROR_SECTOR.nExtentPos2nd + pDisc->PROTECT.ERROR_SECTOR.nSectorSize2nd))) {
#endif
				if (pDiscPerSector->data.next != NULL && 1 <= pExtArg->dwSubAddionalNum) {
					memcpy(pDiscPerSector->data.next, pDiscPerSector->data.current + pDevice->TRANSFER.dwBufLen, pDevice->TRANSFER.dwBufLen);
					AlignRowSubcode(pDiscPerSector->data.next + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.next);
					
					if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
						bRet = ContainsC2Error(pDevice, pDiscPerSector->data.next, &pDiscPerSector->dwC2errorNum);
					}
					if (pDiscPerSector->data.nextNext != NULL && 2 <= pExtArg->dwSubAddionalNum) {
						ExecReadCDForC2(pExecType, pExtArg, pDevice, lpCmd,
							nLBA + 2, pDiscPerSector->data.nextNext, _T(__FUNCTION__), __LINE__);
						AlignRowSubcode(pDiscPerSector->data.nextNext + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.nextNext);
					}
				}
#if 0
			}
			else {
				// replace sub to sub of prev
				ZeroMemory(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, CD_RAW_READ_SUBCODE_SIZE);
				ZeroMemory(pDiscPerSector->subcode.current, CD_RAW_READ_SUBCODE_SIZE);
				SetBufferFromTmpSubQData(pDiscPerSector->subQ.prev, pDiscPerSector->subcode.current, 0);
				if (pDiscPerSector->data.next != NULL && 1 <= pExtArg->dwSubAddionalNum) {
					SetBufferFromTmpSubQData(pDiscPerSector->subQ.current, pDiscPerSector->subcode.next, 0);
					if (pDiscPerSector->data.nextNext != NULL && 2 <= pExtArg->dwSubAddionalNum) {
						SetBufferFromTmpSubQData(pDiscPerSector->subQ.next, pDiscPerSector->subcode.nextNext, 0);
					}
				}
			}
#endif
		}
	}
	else {
		if (bRet == RETURNED_NO_C2_ERROR_1ST) {
			AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
			if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
#if 0
				if (pExtArg->byScanProtectViaFile && pDisc->PROTECT.byExist &&
					(pDisc->PROTECT.ERROR_SECTOR.nExtentPos <= nLBA &&
						nLBA <= pDisc->PROTECT.ERROR_SECTOR.nExtentPos + pDisc->PROTECT.ERROR_SECTOR.nSectorSize)) {
					// skip check c2 error
					ZeroMemory(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufC2Offset, CD_RAW_READ_C2_294_SIZE);
				}
				else {
#endif
					bRet = ContainsC2Error(pDevice, pDiscPerSector->data.current, &pDiscPerSector->dwC2errorNum);
#if 0
				}
#endif
			}
			if (!IsValidProtectedSector(pDisc, nLBA)) {
				if (pDiscPerSector->data.next != NULL && 1 <= pExtArg->dwSubAddionalNum) {
					ExecReadCDForC2(pExecType, pExtArg, pDevice, lpCmd,
						nLBA + 1, pDiscPerSector->data.next, _T(__FUNCTION__), __LINE__);
					AlignRowSubcode(pDiscPerSector->data.next + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.next);

					if (pDiscPerSector->data.nextNext != NULL && 2 <= pExtArg->dwSubAddionalNum) {
						ExecReadCDForC2(pExecType, pExtArg, pDevice, lpCmd,
							nLBA + 2, pDiscPerSector->data.nextNext, _T(__FUNCTION__), __LINE__);
						AlignRowSubcode(pDiscPerSector->data.nextNext + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.nextNext);
					}
				}
			}
		}
#if 0
		if (pExtArg->byScanProtectViaFile && pDisc->PROTECT.byExist &&
			(pDisc->PROTECT.ERROR_SECTOR.nExtentPos <= nLBA &&
				nLBA <= pDisc->PROTECT.ERROR_SECTOR.nExtentPos + pDisc->PROTECT.ERROR_SECTOR.nSectorSize)) {
#if 1
			if (bRet == RETURNED_CONTINUE) {
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
					// skip check c2 error
					ZeroMemory(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufC2Offset, CD_RAW_READ_C2_294_SIZE);
				}
			}
#endif
			// replace sub to sub of prev
			ZeroMemory(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, CD_RAW_READ_SUBCODE_SIZE);
			ZeroMemory(pDiscPerSector->subcode.current, CD_RAW_READ_SUBCODE_SIZE);
			SetBufferFromTmpSubQData(pDiscPerSector->subQ.prev, pDiscPerSector->subcode.current, 0);
			if (pDiscPerSector->data.next != NULL && 1 <= pExtArg->dwSubAddionalNum) {
				SetBufferFromTmpSubQData(pDiscPerSector->subQ.current, pDiscPerSector->subcode.next, 0);
				if (pDiscPerSector->data.nextNext != NULL && 2 <= pExtArg->dwSubAddionalNum) {
					SetBufferFromTmpSubQData(pDiscPerSector->subQ.next, pDiscPerSector->subcode.nextNext, 0);
				}
			}
		}
#endif
	}
	return bRet;
}

BOOL ReadCDForRereadingSectorType1(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	PDISC_PER_SECTOR pDiscPerSector,
	LPBYTE lpCmd,
	FILE* fpImg,
	FILE* fpC2
) {
	LPBYTE lpBuf = NULL;
	if (NULL == (lpBuf = (LPBYTE)calloc(CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * 2, sizeof(BYTE)))) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}
	BOOL bRet = TRUE;
	try {
		if ((pExtArg->byD8 || pDevice->byPlxtrDrive) && !pExtArg->byBe) {
			CDB::_PLXTR_READ_CDDA cdb = { 0 };
			SetReadD8Command(pDevice, &cdb, 2, CDFLAG::_PLXTR_READ_CDDA::MainC2Raw);
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}
		else {
			// non plextor && support scrambled ripping
			CDB::_READ_CD cdb = { 0 };
			SetReadCDCommand(pDevice, &cdb, CDFLAG::_READ_CD::CDDA
				, 2, CDFLAG::_READ_CD::byte294, CDFLAG::_READ_CD::NoSub);
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}

		for (INT m = 0; m < pDisc->MAIN.nC2ErrorCnt; m++) {
			INT nLBA = pDisc->MAIN.lpAllLBAOfC2Error[m];
			for (DWORD i = 0; i < pExtArg->dwMaxRereadNum; i++) {
				OutputString(_T("\rNeed to reread sector: %6d rereading times: %4ld/%4ld")
					, nLBA, i + 1, pExtArg->dwMaxRereadNum);
				if (!ExecReadCD(pExtArg, pDevice, lpCmd, nLBA, lpBuf
					, CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * 2, _T(__FUNCTION__), __LINE__)) {
					throw FALSE;
				}
				DWORD dwTmpCrc32 = 0;
				GetCrc32(&dwTmpCrc32, lpBuf, CD_RAW_SECTOR_SIZE);
//				OutputC2ErrorWithLBALogA("to [%06d] crc32[%03ld]: 0x%08lx "
//					, nLBA - pDisc->MAIN.nOffsetStart - 1, nLBA - pDisc->MAIN.nOffsetStart, i, dwTmpCrc32);
				OutputC2ErrorWithLBALogA("crc32[%03ld]: 0x%08lx ", nLBA, i, dwTmpCrc32);

				LPBYTE lpNextBuf = lpBuf + CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE;
				if (ContainsC2Error(pDevice, lpNextBuf, &pDiscPerSector->dwC2errorNum) == RETURNED_NO_C2_ERROR_1ST) {
					LONG lSeekMain = CD_RAW_SECTOR_SIZE * (LONG)nLBA - pDisc->MAIN.nCombinedOffset;
					fseek(fpImg, lSeekMain, SEEK_SET);
					// Write track to scrambled again
					WriteMainChannel(pExtArg, pDisc, lpBuf, nLBA, fpImg);
					LONG lSeekC2 = CD_RAW_READ_C2_294_SIZE * (LONG)nLBA - (pDisc->MAIN.nCombinedOffset / 8);
					fseek(fpC2, lSeekC2, SEEK_SET);
					WriteC2(pExtArg, pDisc, lpNextBuf + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
					OutputC2ErrorLogA("good. Rewrote .scm[%ld-%ld(%lx-%lx)] .c2[%ld-%ld(%lx-%lx)]\n"
						, lSeekMain, lSeekMain + 2351, lSeekMain, lSeekMain + 2351
						, lSeekC2, lSeekC2 + 293, lSeekC2, lSeekC2 + 293);
					break;
				}
				else {
					if (i == pExtArg->dwMaxRereadNum - 1 && !pDisc->PROTECT.byExist) {
						OutputLogA(standardError | fileC2Error, "\nbad all. need to reread more\n");
						throw FALSE;
					}
					else {
						OutputC2ErrorLogA("bad\n");
					}
				}
				if (!FlushDriveCache(pExtArg, pDevice, nLBA)) {
					throw FALSE;
				}
			}
		}
		OutputString(_T("\nDone. See _c2Error.txt\n"));
	}
	catch (BOOL bErr) {
		bRet = bErr;
	}
	FreeAndNull(lpBuf);
	return bRet;
}

BOOL ReadCDForRereadingSectorType2(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	LPBYTE lpCmd,
	FILE* fpImg,
	FILE* fpC2,
	INT nStartLBA,
	INT nEndLBA
) {
	BOOL bRet = TRUE;
	DWORD dwTransferLen = pDevice->dwMaxTransferLength / CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE;
	DWORD dwTransferLenBak = dwTransferLen;
	LPBYTE lpBufMain = NULL;
	if (NULL == (lpBufMain = (LPBYTE)calloc(dwTransferLen * CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * pExtArg->dwMaxRereadNum, sizeof(BYTE)))) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}
	LPBYTE lpBufC2 = NULL;
	LPBYTE* lpRereadSector = NULL;
	LPDWORD* lpCrc32RereadSector = NULL;
	LPDWORD* lpRepeatedNum = NULL;
	LPDWORD* lpContainsC2 = NULL;
	try {
		if (NULL == (lpBufC2 = (LPBYTE)calloc(dwTransferLen * CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * pExtArg->dwMaxRereadNum, sizeof(BYTE)))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (lpRereadSector = (LPBYTE*)calloc(dwTransferLen, sizeof(ULONG_PTR)))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (lpCrc32RereadSector = (LPDWORD*)calloc(dwTransferLen, sizeof(ULONG_PTR)))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (lpRepeatedNum = (LPDWORD*)calloc(dwTransferLen, sizeof(ULONG_PTR)))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (lpContainsC2 = (LPDWORD*)calloc(dwTransferLen, sizeof(ULONG_PTR)))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		for (DWORD a = 0; a < dwTransferLen; a++) {
			if (NULL == (lpRereadSector[a] = (LPBYTE)calloc(CD_RAW_SECTOR_SIZE * pExtArg->dwMaxRereadNum, sizeof(BYTE)))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			if (NULL == (lpCrc32RereadSector[a] = (LPDWORD)calloc(pExtArg->dwMaxRereadNum, sizeof(DWORD)))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			if (NULL == (lpRepeatedNum[a] = (LPDWORD)calloc(pExtArg->dwMaxRereadNum, sizeof(DWORD)))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			if (NULL == (lpContainsC2[a] = (LPDWORD)calloc(pExtArg->dwMaxRereadNum, sizeof(DWORD)))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
		}

		if ((pExtArg->byD8 || pDevice->byPlxtrDrive) && !pExtArg->byBe) {
			CDB::_PLXTR_READ_CDDA cdb = { 0 };
			SetReadD8Command(pDevice, &cdb, 2, CDFLAG::_PLXTR_READ_CDDA::MainC2Raw);
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}
		else {
			// non plextor && support scrambled ripping
			CDB::_READ_CD cdb = { 0 };
			SetReadCDCommand(pDevice, &cdb, CDFLAG::_READ_CD::CDDA
				, 2, CDFLAG::_READ_CD::byte294, CDFLAG::_READ_CD::NoSub);
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}

		INT nLBA = nStartLBA;
		INT nLastLBA = nEndLBA;

		while (nLBA < nLastLBA) {
			OutputString(_T("\rRewrited img (LBA) %6d/%6d"), nLBA, pDisc->SCSI.nAllLength - 1);
			INT idx = 0;
			if (nLastLBA - nLBA < (INT)dwTransferLen) {
				dwTransferLen = (DWORD)(nLastLBA - nLBA);
				lpCmd[9] = (BYTE)dwTransferLen;
			}
			for (DWORD i = 0; i < pExtArg->dwMaxRereadNum; i++) {
				if (!ExecReadCD(pExtArg, pDevice, lpCmd, nLBA, lpBufMain
					, CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * dwTransferLen, _T(__FUNCTION__), __LINE__)) {
					throw FALSE;
				}
				if (!ExecReadCD(pExtArg, pDevice, lpCmd, nLBA + 1, lpBufC2
					, CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * dwTransferLen, _T(__FUNCTION__), __LINE__)) {
					throw FALSE;
				}
				for (DWORD k = 0; k < dwTransferLen; k++) {
					OutputC2ErrorWithLBALogA("crc32", nLBA - pDisc->MAIN.nOffsetStart + (INT)k);
					DWORD dwTmpCrc32 = 0;
					GetCrc32(&dwTmpCrc32, lpBufMain + CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * k, CD_RAW_SECTOR_SIZE);

					BOOL bMatch = FALSE;
					BOOL bC2 = RETURNED_NO_C2_ERROR_1ST;
					for (WORD wC2ErrorPos = 0; wC2ErrorPos < CD_RAW_READ_C2_294_SIZE; wC2ErrorPos++) {
						INT nBit = 0x80;
						DWORD dwPos = CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * k + pDevice->TRANSFER.dwBufC2Offset + wC2ErrorPos;
						for (INT n = 0; n < CHAR_BIT; n++) {
							// exist C2 error
							if (lpBufC2[dwPos] & nBit) {
								bC2 = RETURNED_EXIST_C2_ERROR;
								break;
							}
							nBit >>= 1;
						}
						if (bC2 == RETURNED_EXIST_C2_ERROR) {
							break;
						}
					}
					for (DWORD j = 0; j <= i; j++) {
						if (dwTmpCrc32 == lpCrc32RereadSector[k][j]) {
							OutputC2ErrorLogA("[%03ld]:0x%08lx, %d ", i, dwTmpCrc32, bC2);
							lpRepeatedNum[k][j] += 1;
							lpContainsC2[k][j] += bC2;
							bMatch = TRUE;
							break;
						}
					}
					if (!bMatch) {
						lpCrc32RereadSector[k][idx] = dwTmpCrc32;
						memcpy(&lpRereadSector[k][CD_RAW_SECTOR_SIZE * idx]
							, lpBufMain + CD_RAW_SECTOR_WITH_C2_294_AND_SUBCODE_SIZE * k, CD_RAW_SECTOR_SIZE);
						OutputC2ErrorLogA("[%03d]:0x%08lx, %d ", idx, dwTmpCrc32, bC2);
#if 0
						if (idx > 0) {
							OutputCDMain(fileC2Error, &lpRereadSector[k][CD_RAW_SECTOR_SIZE * idx]
								, nLBA - pDisc->MAIN.nOffsetStart + (INT)k, CD_RAW_SECTOR_SIZE);
						}
#endif
					}
				}
				OutputC2ErrorLogA("\n");
				idx++;
				if (!FlushDriveCache(pExtArg, pDevice, nLBA)) {
					throw FALSE;
				}
			}

			for (DWORD q = 0; q < dwTransferLen; q++) {
				DWORD dwMaxNum = lpRepeatedNum[q][0];
				DWORD dwMaxC2 = lpContainsC2[q][0];
				for (INT k = 0; k < idx - 1; k++) {
					dwMaxNum = max(dwMaxNum, lpRepeatedNum[q][k + 1]);
					dwMaxC2 = max(dwMaxC2, lpContainsC2[q][k + 1]);
				}

				INT nTmpLBA = nLBA - pDisc->MAIN.nOffsetStart + (INT)q;
				if (dwMaxC2 == 0) {
					OutputC2ErrorWithLBALogA(
						"to[%06d] All crc32 is probably bad. No rewrite\n", nTmpLBA - 1, nTmpLBA);
				}
				else if (dwMaxNum + 1 == pExtArg->dwMaxRereadNum &&
					lpCrc32RereadSector[q][0] == pDisc->MAIN.lpAllSectorCrc32[nTmpLBA]) {
					OutputC2ErrorWithLBALogA(
						"to[%06d] All same crc32. No rewrite\n", nTmpLBA - 1, nTmpLBA);
				}
				else {
					for (INT l = 0; l < idx; l++) {
						if (dwMaxC2 == lpContainsC2[q][l]) {
							OutputC2ErrorWithLBALogA(
								"to[%06d] crc32[%d]:0x%08lx, no c2 %lu times. Rewrite\n"
								, nTmpLBA - 1, nTmpLBA, l, lpCrc32RereadSector[q][l], dwMaxC2 + 1);
							fseek(fpImg, CD_RAW_SECTOR_SIZE * (LONG)(nLBA + q) - pDisc->MAIN.nCombinedOffset, SEEK_SET);
							// Write track to scrambled again
							WriteMainChannel(pExtArg, pDisc, &lpRereadSector[q][CD_RAW_SECTOR_SIZE * l], nLBA, fpImg);
							fseek(fpC2, CD_RAW_READ_C2_294_SIZE * (LONG)nLBA - (pDisc->MAIN.nCombinedOffset / 8), SEEK_SET);
							if (q - 1 < dwTransferLen) {
								WriteC2(pExtArg, pDisc, &lpRereadSector[q + 1][CD_RAW_SECTOR_SIZE * l] + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
							}
#if 0
							OutputC2ErrorLogA("Seek to %ld (0x%08lx)\n"
								, CD_RAW_SECTOR_SIZE * (LONG)(nLBA + q) - pDisc->MAIN.nCombinedOffset
								, CD_RAW_SECTOR_SIZE * (LONG)(nLBA + q) - pDisc->MAIN.nCombinedOffset);
							OutputCDMain(fileC2Error, &lpRereadSector[q][CD_RAW_SECTOR_SIZE * l], nTmpLBA, CD_RAW_SECTOR_SIZE);
							OutputC2ErrorLogA("\n");
#endif
							break;
						}
					}
				}
			}
			nLBA += dwTransferLen;
			for (DWORD r = 0; r < dwTransferLen; r++) {
				for (INT p = 0; p < idx; p++) {
					lpRereadSector[r][p] = 0;
					lpCrc32RereadSector[r][p] = 0;
					lpRepeatedNum[r][p] = 0;
					lpContainsC2[r][p] = 0;
				}
			}
		}
		OutputLogA(standardOut | fileC2Error, "\n");
	}
	catch (BOOL bErr) {
		bRet = bErr;
	}
	FreeAndNull(lpBufMain);
	FreeAndNull(lpBufC2);
	for (DWORD r = 0; r < dwTransferLenBak; r++) {
		FreeAndNull(lpRereadSector[r]);
		FreeAndNull(lpCrc32RereadSector[r]);
		FreeAndNull(lpRepeatedNum[r]);
		FreeAndNull(lpContainsC2[r]);
	}
	FreeAndNull(lpRereadSector);
	FreeAndNull(lpCrc32RereadSector);
	FreeAndNull(lpRepeatedNum);
	FreeAndNull(lpContainsC2);

	return bRet;
}

VOID ExecEccEdc(
	BYTE byScanProtectViaFile,
	_DISC::_PROTECT protect,
	LPCTSTR pszImgPath,
	_DISC::_PROTECT::_ERROR_SECTOR errorSector
) {
	CONST INT nCmdSize = 6;
	CONST INT nStrSize = _MAX_PATH * 2 + nCmdSize;
	_TCHAR str[nStrSize] = { 0 };
	_TCHAR cmd[nCmdSize] = { _T("check") };
	INT nStartLBA = errorSector.nExtentPos;
	INT nEndLBA = errorSector.nExtentPos + errorSector.nSectorSize;
	if (byScanProtectViaFile) {
		_tcsncpy(cmd, _T("fix"), sizeof(cmd) / sizeof(cmd[0]));
	}
	if (GetEccEdcCmd(str, nStrSize, cmd, pszImgPath, nStartLBA, nEndLBA)) {
		OutputString(_T("Exec %s\n"), str);
		_tsystem(str);
	}
	if (protect.byExist == microids) {
		nStartLBA = errorSector.nExtentPos2nd;
		nEndLBA = errorSector.nExtentPos2nd + errorSector.nSectorSize2nd;
		if (GetEccEdcCmd(str, nStrSize, cmd, pszImgPath, nStartLBA, nEndLBA)) {
			OutputString(_T("Exec %s\n"), str);
			_tsystem(str);
		}
	}
}

VOID ProcessReturnedContinue(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	PDISC_PER_SECTOR pDiscPerSector,
	INT nLBA,
	INT nMainDataType,
	BYTE byCurrentTrackNum,
	FILE* fpImg,
	FILE* fpSub,
	FILE* fpC2,
	FILE* fpParse
) {
#if 1
	if ((pDiscPerSector->subQ.prev.byCtl & AUDIO_DATA_TRACK) == AUDIO_DATA_TRACK) {
		OutputCDMain(fileMainError,
			pDiscPerSector->mainHeader.current, nLBA, MAINHEADER_MODE1_SIZE);
	}
#endif
	UpdateTmpMainHeader(&pDiscPerSector->mainHeader,
		(LPBYTE)&pDiscPerSector->mainHeader.prev, pDiscPerSector->subQ.prev.byCtl, nMainDataType);
	WriteErrorBuffer(pExecType, pExtArg, pDevice, pDisc, pDiscPerSector,
		scrambled_table, nLBA, byCurrentTrackNum, fpImg, fpSub, fpC2, fpParse);
#if 1
	if ((pDiscPerSector->subQ.prev.byCtl & AUDIO_DATA_TRACK) == AUDIO_DATA_TRACK) {
		if (pExtArg->byBe) {
			OutputCDMain(fileMainError,
				pDiscPerSector->mainHeader.current, nLBA, MAINHEADER_MODE1_SIZE);
		}
		else {
			OutputCDMain(fileMainError,
				pDiscPerSector->data.current + pDisc->MAIN.uiMainDataSlideSize, nLBA, MAINHEADER_MODE1_SIZE);
		}
	}
#endif
	if (pDiscPerSector->subQ.prev.byIndex == 0) {
		pDiscPerSector->subQ.prev.nRelativeTime--;
	}
	else {
		pDiscPerSector->subQ.prev.nRelativeTime++;
	}
	pDiscPerSector->subQ.prev.nAbsoluteTime++;
}

BOOL ProcessDescramble(
	PEXT_ARG pExtArg,
	PDISC pDisc,
	LPCTSTR pszPath,
	_TCHAR* pszOutScmFile
) {
	_TCHAR pszNewPath[_MAX_PATH] = { 0 };
	_tcsncpy(pszNewPath, pszOutScmFile, sizeof(pszNewPath) / sizeof(pszNewPath[0]));
	pszNewPath[_MAX_PATH - 1] = 0;
	// "PathRenameExtension" fails to rename if space is included in extension.
	// e.g.
	//  no label. 2017-02-14_ 9-41-31 => no label. 2017-02-14_ 9-41-31.img
	//  no label.2017-02-14_9-41-31   => no label.img
	if (!PathRenameExtension(pszNewPath, _T(".img"))) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}
	// audio only -> from .scm to .img. other descramble img.
	if (pExtArg->byBe || pDisc->SCSI.trackType != TRACK_TYPE::dataExist) {
		OutputString(_T("Moving .scm to .img\n"));
		if (!MoveFileEx(pszOutScmFile, pszNewPath, MOVEFILE_REPLACE_EXISTING)) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			return FALSE;
		}
		if (pExtArg->byBe) {
			ExecEccEdc(pExtArg->byScanProtectViaFile, pDisc->PROTECT, pszNewPath, pDisc->PROTECT.ERROR_SECTOR);
		}
	}
	else {
		OutputString(_T("Copying .scm to .img\n"));
		if (!CopyFile(pszOutScmFile, pszNewPath, FALSE)) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			return FALSE;
		}
		FILE* fpImg = NULL;
		_TCHAR pszImgPath[_MAX_PATH] = { 0 };
		if (NULL == (fpImg = CreateOrOpenFile(
			pszPath, NULL, pszImgPath, NULL, NULL, _T(".img"), _T("rb+"), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			return FALSE;
		}
		DescrambleMainChannelAll(pExtArg, pDisc, scrambled_table, fpImg);
		FcloseAndNull(fpImg);
		ExecEccEdc(pExtArg->byScanProtectViaFile, pDisc->PROTECT, pszImgPath, pDisc->PROTECT.ERROR_SECTOR);
	}
	return TRUE;
}

BOOL ProcessCreateBin(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	LPCTSTR pszPath,
	FILE* fpCue,
	FILE* fpCueForImg,
	FILE* fpCcd
) {
	FILE* fpImg = NULL;
	_TCHAR pszImgName[_MAX_FNAME] = { 0 };
	if (NULL == (fpImg = CreateOrOpenFile(
		pszPath, NULL, NULL, pszImgName, NULL, _T(".img"), _T("rb"), 0, 0))) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}
	if (!CreateBinCueCcd(pExtArg, pDisc, pszPath, pszImgName,
		pDevice->FEATURE.byCanCDText, fpImg, fpCue, fpCueForImg, fpCcd)) {
		FreeAndNull(fpImg);
		return FALSE;
	}
	FreeAndNull(fpImg);
	return TRUE;
}

BOOL ReadCDAll(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	PDISC_PER_SECTOR pDiscPerSector,
	CDFLAG::_READ_CD::_ERROR_FLAGS c2,
	LPCTSTR pszPath,
	FILE* fpCcd,
	FILE* fpC2
) {
	FILE* fpImg = NULL;
	_TCHAR pszOutScmFile[_MAX_PATH] = { 0 };
	if (NULL == (fpImg = CreateOrOpenFile(pszPath, NULL,
		pszOutScmFile, NULL, NULL, _T(".scm"), _T("wb"), 0, 0))) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}
	BOOL bRet = TRUE;
	FILE* fpCue = NULL;
	FILE* fpCueForImg = NULL;
	FILE* fpParse = NULL;
	FILE* fpSub = NULL;
	LPBYTE pBuf = NULL;
	LPBYTE pNextBuf = NULL;
	LPBYTE pNextNextBuf = NULL;
	INT nMainDataType = scrambled;
	if (pExtArg->byBe) {
		nMainDataType = unscrambled;
	}

	try {
		// init start
		if (NULL == (fpCue = CreateOrOpenFile(
			pszPath, NULL, NULL, NULL, NULL, _T(".cue"), _T(WFLAG), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (fpCueForImg = CreateOrOpenFile(
			pszPath, _T("_img"), NULL, NULL, NULL, _T(".cue"), _T(WFLAG), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (fpParse = CreateOrOpenFile(
			pszPath, _T("_subReadable"), NULL, NULL, NULL, _T(".txt"), _T(WFLAG), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (fpSub = CreateOrOpenFile(
			pszPath, NULL, NULL, NULL, NULL, _T(".sub"), _T("wb"), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}

		BYTE byTransferLen = 1;
		if (pDevice->byPlxtrDrive) {
			byTransferLen = 2;
		}
		// store main + (c2) + sub data all
		if (!GetAlignedCallocatedBuffer(pDevice, &pBuf,
			pDevice->TRANSFER.dwBufLen * byTransferLen, &pDiscPerSector->data.current, _T(__FUNCTION__), __LINE__)) {
			throw FALSE;
		}
		if (1 <= pExtArg->dwSubAddionalNum) {
			if (!GetAlignedCallocatedBuffer(pDevice, &pNextBuf,
				pDevice->TRANSFER.dwBufLen * byTransferLen, &pDiscPerSector->data.next, _T(__FUNCTION__), __LINE__)) {
				throw FALSE;
			}
			if (2 <= pExtArg->dwSubAddionalNum) {
				if (!GetAlignedCallocatedBuffer(pDevice, &pNextNextBuf,
					pDevice->TRANSFER.dwBufLen * byTransferLen, &pDiscPerSector->data.nextNext, _T(__FUNCTION__), __LINE__)) {
					throw FALSE;
				}
			}
		}
		BYTE lpCmd[CDB12GENERIC_LENGTH] = { 0 };
		_TCHAR szSubCode[5] = { 0 };
		if ((pExtArg->byD8 || pDevice->byPlxtrDrive) && !pExtArg->byBe) {
			CDB::_PLXTR_READ_CDDA cdb = { 0 };
			if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
				_tcsncpy(szSubCode, _T("Raw"), sizeof(szSubCode) / sizeof(szSubCode[0]));
				SetReadD8Command(pDevice, &cdb, byTransferLen, CDFLAG::_PLXTR_READ_CDDA::MainC2Raw);
			}
			else {
				_tcsncpy(szSubCode, _T("Pack"), sizeof(szSubCode) / sizeof(szSubCode[0]));
				SetReadD8Command(pDevice, &cdb, byTransferLen, CDFLAG::_PLXTR_READ_CDDA::MainPack);
			}
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}
		else {
			// non plextor && support scrambled ripping
			CDB::_READ_CD cdb = { 0 };
			CDFLAG::_READ_CD::_EXPECTED_SECTOR_TYPE type = CDFLAG::_READ_CD::CDDA;
			if (pExtArg->byBe) {
				type = CDFLAG::_READ_CD::All;
			}
			CDFLAG::_READ_CD::_SUB_CHANNEL_SELECTION sub = CDFLAG::_READ_CD::Raw;
			_tcsncpy(szSubCode, _T("Raw"), sizeof(szSubCode) / sizeof(szSubCode[0]));
			if (pExtArg->byPack) {
				sub = CDFLAG::_READ_CD::Pack;
				_tcsncpy(szSubCode, _T("Pack"), sizeof(szSubCode) / sizeof(szSubCode[0]));
			}
			SetReadCDCommand(pDevice, &cdb, type, byTransferLen, c2, sub);
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}
		OutputLog(standardOut | fileDisc,
			_T("Set OpCode: %#02x, SubCode: %x(%s)\n"), lpCmd[0], lpCmd[10], szSubCode);

		BYTE lpPrevSubcode[CD_RAW_READ_SUBCODE_SIZE] = { 0 };
		// to get prevSubQ
		if (pDisc->SUB.nSubChannelOffset) {
			if (!ExecReadCD(pExtArg, pDevice, lpCmd, -2, pDiscPerSector->data.current,
				pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
				throw FALSE;
			}
			AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
			SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.prev, pDiscPerSector->subcode.current);

			if (!ExecReadCD(pExtArg, pDevice, lpCmd, -1, pDiscPerSector->data.current,
				pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
				throw FALSE;
			}
			AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
			SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.current, pDiscPerSector->subcode.current);
			memcpy(lpPrevSubcode, pDiscPerSector->subcode.current, CD_RAW_READ_SUBCODE_SIZE);
		}
		else {
			if (!ExecReadCD(pExtArg, pDevice, lpCmd, -1, pDiscPerSector->data.current,
				pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
				throw FALSE;
			}
			AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
			SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.prev, pDiscPerSector->subcode.current);
		}
		// special fix begin
		if (pDiscPerSector->subQ.prev.byAdr != ADR_ENCODES_CURRENT_POSITION) {
			// [PCE] 1552 Tenka Tairan
			pDiscPerSector->subQ.prev.byAdr = ADR_ENCODES_CURRENT_POSITION;
			pDiscPerSector->subQ.prev.byTrackNum = 1;
			pDiscPerSector->subQ.prev.nAbsoluteTime = 149;
		}
		if (!ReadCDForCheckingSecuROM(pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd)) {
			throw FALSE;
		}
		// special fix end

		for (UINT p = 0; p < pDisc->SCSI.toc.LastTrack; p++) {
			if (!ExecReadCD(pExtArg, pDevice, lpCmd, pDisc->SCSI.lpFirstLBAListOnToc[p]
				, pDiscPerSector->data.current, pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
				throw FALSE;
			}
			AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
			pDisc->SUB.lpEndCtlList[p] = (BYTE)((pDiscPerSector->subcode.current[12] >> 4) & 0x0f);
			OutputString(_T("\rChecking SubQ ctl (Track) %2u/%2u"), p + 1, pDisc->SCSI.toc.LastTrack);
		}
		OutputString(_T("\n"));
		SetCDOffset(pExecType, pExtArg->byBe, pDevice->byPlxtrDrive, pDisc, 0, pDisc->SCSI.nAllLength);

		BYTE byCurrentTrackNum = pDisc->SCSI.toc.FirstTrack;
		INT nFirstLBAForSub = 0;
		INT nFirstLBA = pDisc->MAIN.nOffsetStart;
		INT nLastLBA =  pDisc->SCSI.nAllLength + pDisc->MAIN.nOffsetEnd;
		INT nLBA = nFirstLBA;

		if (pExtArg->byPre) {
			nFirstLBAForSub = PREGAP_START_LBA;
			nFirstLBA = PREGAP_START_LBA;
			nLBA = nFirstLBA;
			pDisc->MAIN.nOffsetStart = PREGAP_START_LBA;
		}
		// init end
		FlushLog();

		BOOL bReadOK = pExtArg->byPre ? FALSE : TRUE;
		BOOL bC2Error = FALSE;
		BOOL bReread = FALSE;

		while (nFirstLBA < nLastLBA) {
			BOOL bProcessRet = ProcessReadCD(pExecType, pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd, nLBA);
			if (bProcessRet == RETURNED_EXIST_C2_ERROR) {
				bC2Error = TRUE;
				// C2 error points the current LBA - 1 (offset?)
				OutputLogA(standardError | fileC2Error,
					"\rLBA[%06d, %#07x] Detected C2 error %ld bit\n", nLBA, nLBA, pDiscPerSector->dwC2errorNum);
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
					if (!(IsValidProtectedSector(pDisc, nLBA) && IsValidIntentionalC2error(pDisc, pDiscPerSector))) {
						pDisc->MAIN.lpAllLBAOfC2Error[pDisc->MAIN.nC2ErrorCnt++] = nLBA;
					}
				}
			}
			else if (bProcessRet == RETURNED_SKIP_LBA) {
				nLBA = pDisc->MAIN.nFixFirstLBAof2ndSession - 1;
				nFirstLBA = nLBA;
			}
			else if (bProcessRet == RETURNED_CONTINUE &&
				!(pExtArg->byPre && -1149 <= nLBA && nLBA <= -76)) {
				ProcessReturnedContinue(pExecType, pExtArg, pDevice, pDisc, pDiscPerSector
					, nLBA, nMainDataType, byCurrentTrackNum, fpImg, fpSub, fpC2, fpParse);
			}
			else if (bProcessRet == RETURNED_FALSE) {
				throw FALSE;
			}
			if (bProcessRet != RETURNED_CONTINUE &&
				bProcessRet != RETURNED_SKIP_LBA) {
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData
					&& pExtArg->nC2RereadingType == 1 && nFirstLBA - pDisc->MAIN.nOffsetStart >= 0) {
					GetCrc32(&pDisc->MAIN.lpAllSectorCrc32[nFirstLBA - pDisc->MAIN.nOffsetStart]
						, pDiscPerSector->data.current, CD_RAW_SECTOR_SIZE);
				}
				if (pDisc->SUB.nSubChannelOffset) {
					if (!IsValidProtectedSector(pDisc, nLBA)) {
						if (2 <= pExtArg->dwSubAddionalNum) {
							memcpy(pDiscPerSector->subcode.nextNext
								, pDiscPerSector->subcode.next, CD_RAW_READ_SUBCODE_SIZE);
						}
						if (1 <= pExtArg->dwSubAddionalNum) {
							memcpy(pDiscPerSector->subcode.next
								, pDiscPerSector->subcode.current, CD_RAW_READ_SUBCODE_SIZE);
						}
						memcpy(pDiscPerSector->subcode.current, lpPrevSubcode, CD_RAW_READ_SUBCODE_SIZE);
					}
				}
				SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.current, pDiscPerSector->subcode.current);

				if (pExtArg->byPre && PREGAP_START_LBA <= nLBA && nLBA <= -76) {
					if (pDiscPerSector->subQ.current.byTrackNum == 1 &&
						pDiscPerSector->subQ.current.nAbsoluteTime == 0) {
						pDiscPerSector->subQ.prev.nRelativeTime = pDiscPerSector->subQ.current.nRelativeTime + 1;
						pDiscPerSector->subQ.prev.nAbsoluteTime = -1;
						pDisc->MAIN.nFixStartLBA = nLBA;
						bReadOK = TRUE;
#if 0
						if (pDisc->MAIN.nAdjustSectorNum < 0 ||
							1 < pDisc->MAIN.nAdjustSectorNum) {
							for (INT i = 0; i < abs(pDisc->MAIN.nAdjustSectorNum) * CD_RAW_SECTOR_SIZE; i++) {
								fputc(0, fpImg);
							}
						}
#endif
					}
					if (bReadOK) {
						if (pDiscPerSector->subQ.current.byTrackNum == 1 &&
							pDiscPerSector->subQ.current.nAbsoluteTime == 74) {
							nFirstLBA = -76;
						}
					}
				}
#if 0
				if (pExtArg->byPre && PREGAP_START_LBA <= nLBA && nLBA <= -76) {
					OutputCDSub96Align(pDiscPerSector->subcode.current, nLBA);
					OutputCDMain(fileDisc, pDiscPerSector->data.current, nLBA, CD_RAW_SECTOR_SIZE);
				}
#endif
				if (bReadOK) {
					if (nFirstLBAForSub <= nLBA && nLBA < pDisc->SCSI.nAllLength) {
						if (IsCheckingSubChannel(pExtArg, pDisc, nLBA)) {
							BOOL bLibCrypt = IsValidLibCryptSector(pExtArg->byLibCrypt, nLBA);
							BOOL bSecuRom = IsValidSecuRomSector(pExtArg->byIntentionalSub, pDisc, nLBA);
							CheckAndFixSubChannel(pExecType, pExtArg, pDevice, pDisc
								, pDiscPerSector, byCurrentTrackNum, nLBA, bLibCrypt, bSecuRom);
							if (pDisc->SUB.nCorruptCrcL && pDisc->SUB.nCorruptCrcH) {
								if (!bReread) {
									OutputSubErrorWithLBALogA(
										"Q Reread this sector because Crc16 doesn't match\n", nLBA, byCurrentTrackNum);
									bReread = TRUE;
									continue;
								}
								else {
									OutputSubErrorWithLBALogA("Q Reread NG\n", nLBA, byCurrentTrackNum);
									bReread = FALSE;
								}
							}
							else if (bReread) {
								OutputSubErrorWithLBALogA("Q Reread OK\n", nLBA, byCurrentTrackNum);
								bReread = FALSE;
							}
							BYTE lpSubcodeRaw[CD_RAW_READ_SUBCODE_SIZE] = { 0 };
							// fix raw subchannel
							AlignColumnSubcode(pDiscPerSector->subcode.current, lpSubcodeRaw);
#if 0
							OutputCDSub96Align(pDiscPerSector->subcode.current, nLBA);
							OutputCDSub96Raw(standardOut, lpSubcodeRaw, nLBA);
#endif
							WriteSubChannel(pDisc, lpSubcodeRaw,
								pDiscPerSector->subcode.current, nLBA, byCurrentTrackNum, fpSub, fpParse);
							CheckAndFixMainHeader(pExtArg, pDisc
								, pDiscPerSector, nLBA, byCurrentTrackNum, nMainDataType);
							SetTrackAttribution(pExecType, pExtArg, pDisc, nLBA,
								&byCurrentTrackNum, &pDiscPerSector->mainHeader, &pDiscPerSector->subQ);
							UpdateTmpSubQData(&pDiscPerSector->subQ, bLibCrypt, bSecuRom);
						}
					}
					// Write track to scrambled
					WriteMainChannel(pExtArg, pDisc, pDiscPerSector->data.current, nLBA, fpImg);
					if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
						if (pExtArg->byD8 || pDevice->byPlxtrDrive) {
							WriteC2(pExtArg, pDisc, pDiscPerSector->data.next + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
						}
						else {
							WriteC2(pExtArg, pDisc, pDiscPerSector->data.current + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
						}
					}
				}
#if 0
				else {
					BYTE lpSubcodeRaw[CD_RAW_READ_SUBCODE_SIZE] = { 0 };
					AlignColumnSubcode(pDiscPerSector->subcode.current, lpSubcodeRaw);
					OutputCDSubToLog(pDisc, pDiscPerSector->subcode.current, lpSubcodeRaw, nLBA, byCurrentTrackNum, fpParse);
				}
#endif
				if (pDisc->SUB.nSubChannelOffset) {
					memcpy(lpPrevSubcode, pDiscPerSector->subcode.next, CD_RAW_READ_SUBCODE_SIZE);
				}
			}

			OutputString(_T("\rCreating .scm (LBA) %6d/%6d"), nLBA, nLastLBA - 1);
			if (nFirstLBA == -76) {
				nLBA = nFirstLBA;
				if (!bReadOK) {
					bReadOK = TRUE;
				}
			}
			nLBA++;
			nFirstLBA++;
		}
		OutputString(_T("\n"));
		FcloseAndNull(fpParse);
		FcloseAndNull(fpSub);
		FlushLog();

		if (pDisc->SCSI.toc.FirstTrack == pDisc->SCSI.toc.LastTrack) {
			// [3DO] Jurassic Park Interactive (Japan)
			if (pDisc->SUB.lpFirstLBAListOnSub[0][2] == -1 &&
				pDisc->SCSI.trackType != TRACK_TYPE::pregapIn1stTrack) {
				pDisc->SUB.lpFirstLBAListOnSub[0][1] = pDisc->SCSI.lpLastLBAListOnToc[0];
			}
		}
		for (INT i = 0; i < pDisc->SCSI.toc.LastTrack; i++) {
			if (pDisc->PROTECT.byExist == cds300 && i == pDisc->SCSI.toc.LastTrack - 1) {
				break;
			}
			BOOL bErr = FALSE;
			LONG lLine = 0;
			if (pDisc->SUB.lpFirstLBAListOnSub[i][1] == -1) {
				bErr = TRUE;
				lLine = __LINE__;
			}
			else if ((pDisc->SCSI.toc.TrackData[i].Control & AUDIO_DATA_TRACK) == AUDIO_DATA_TRACK) {
				if (pDisc->SUB.lpFirstLBAListOfDataTrackOnSub[i] == -1) {
					bErr = TRUE;
					lLine = __LINE__;
				}
				else if (pDisc->SUB.lpLastLBAListOfDataTrackOnSub[i] == -1) {
					bErr = TRUE;
					lLine = __LINE__;
				}
			}
			if (bErr) {
				OutputErrorString(
					_T("[L:%ld] Internal error. Failed to analyze the subchannel. Track[%02u]/[%02u]\n"),
					lLine, i + 1, pDisc->SCSI.toc.LastTrack);
				throw FALSE;
			}
		}
		if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
			if (bC2Error && pDisc->MAIN.nC2ErrorCnt > 0) {
				if (pExtArg->nC2RereadingType == 0) {
					if (!ReadCDForRereadingSectorType1(pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd, fpImg, fpC2)) {
						throw FALSE;
					}
				}
				else {
					INT nStartLBA = pExtArg->nStartLBAForC2;
					INT nEndLBA = pExtArg->nEndLBAForC2;
					if (nStartLBA == 0 && nEndLBA == 0) {
						nStartLBA = pDisc->MAIN.nOffsetStart;
						nEndLBA = pDisc->SCSI.nAllLength + pDisc->MAIN.nOffsetEnd;
					}
					if (!ReadCDForRereadingSectorType2(pExtArg, pDevice, pDisc, lpCmd, fpImg, fpC2, nStartLBA, nEndLBA)) {
						throw FALSE;
					}
				}
			}
			else {
				if (pDisc->PROTECT.byExist == PROTECT_TYPE_CD::no) {
					OutputLogA(standardOut | fileC2Error, "No C2 errors\n");
				}
				else {
					OutputLogA(standardOut | fileC2Error, "No unintentional C2 errors\n");
				}
			}
			FcloseAndNull(fpC2);
		}
		FcloseAndNull(fpImg);
		OutputTocWithPregap(pDisc);

		if (!ProcessDescramble(pExtArg, pDisc, pszPath, pszOutScmFile)) {
			throw FALSE;
		}
		if (!ProcessCreateBin(pExtArg, pDevice, pDisc, pszPath, fpCue, fpCueForImg, fpCcd)) {
			throw FALSE;
		}
	}
	catch (BOOL ret) {
		bRet = ret;
	}
	FcloseAndNull(fpImg);
	FcloseAndNull(fpCueForImg);
	FcloseAndNull(fpCue);
	FcloseAndNull(fpParse);
	FcloseAndNull(fpSub);
	FreeAndNull(pBuf);
	if (1 <= pExtArg->dwSubAddionalNum) {
		FreeAndNull(pNextBuf);
		if (2 <= pExtArg->dwSubAddionalNum) {
			FreeAndNull(pNextNextBuf);
		}
	}

	return bRet;
}

BOOL ReadCDForSwap(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	PDISC_PER_SECTOR pDiscPerSector,
	CDFLAG::_READ_CD::_ERROR_FLAGS c2,
	LPCTSTR pszPath,
	INT nStart,
	INT nEnd,
	CDFLAG::_READ_CD::_EXPECTED_SECTOR_TYPE flg,
	FILE* fpCcd,
	FILE* fpC2
) {
	_TCHAR pszScmPath[_MAX_PATH] = { 0 };
#ifndef __DEBUG
	FILE* fpScm = CreateOrOpenFile(pszPath, NULL, pszScmPath, NULL, NULL, _T(".scm"), _T("wb"), 0, 0);
#else
	FILE* fpScm = CreateOrOpenFile(pszPath, NULL, pszScmPath, NULL, NULL, _T(".scm"), _T("rb"), 0, 0);
#endif
	if (!fpScm) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}
	BOOL bRet = TRUE;
	FILE* fpCue = NULL;
	FILE* fpCueForImg = NULL;
	FILE* fpParse = NULL;
	FILE* fpSub = NULL;
	LPBYTE pBuf = NULL;
	INT nMainDataType = scrambled;
	if (*pExecType == data || pExtArg->byBe) {
		nMainDataType = unscrambled;
	}

	try {
		// init start
		if (NULL == (fpCue = CreateOrOpenFile(
			pszPath, NULL, NULL, NULL, NULL, _T(".cue"), _T(WFLAG), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (fpCueForImg = CreateOrOpenFile(
			pszPath, _T("_img"), NULL, NULL, NULL, _T(".cue"), _T(WFLAG), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
#ifndef __DEBUG
		if (NULL == (fpParse = CreateOrOpenFile(
			pszPath, _T("_subReadable"), NULL, NULL, NULL, _T(".txt"), _T(WFLAG), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		if (NULL == (fpSub = CreateOrOpenFile(
			pszPath, NULL, NULL, NULL, NULL, _T(".sub"), _T("wb"), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		BYTE byTransferLen = 1;
		// store main+(c2)+sub data
		if (!GetAlignedCallocatedBuffer(pDevice, &pBuf,
			pDevice->TRANSFER.dwBufLen * byTransferLen, &pDiscPerSector->data.current, _T(__FUNCTION__), __LINE__)) {
			throw FALSE;
		}
		BYTE lpCmd[CDB12GENERIC_LENGTH] = { 0 };
		_TCHAR szSubCode[5] = { 0 };
		// non plextor && support scrambled ripping
		CDB::_READ_CD cdb = { 0 };
		_tcsncpy(szSubCode, _T("Raw"), sizeof(szSubCode) / sizeof(szSubCode[0]));
		SetReadCDCommand(pDevice, &cdb, flg, byTransferLen, c2, CDFLAG::_READ_CD::Raw);
		memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);

		OutputLog(standardOut | fileDisc,
			_T("Set OpCode: %#02x, SubCode: %x(%s)\n"), lpCmd[0], lpCmd[10], szSubCode);

		SetCDOffset(pExecType, pExtArg->byBe, pDevice->byPlxtrDrive, pDisc, nStart, nEnd);
		BYTE byCurrentTrackNum = 1;
		INT nFirstLBA = nStart + pDisc->MAIN.nOffsetStart;
		INT nLastLBA = nEnd + pDisc->MAIN.nOffsetEnd;
		INT nLBA = nFirstLBA;
		// init end
		FlushLog();

		BOOL bC2Error = FALSE;
#if 1
		BOOL bReread = FALSE;
#endif
		BOOL bSetLastLBA = FALSE;
		while (nFirstLBA < nLastLBA) {
			BOOL bProcessRet = ProcessReadCD(pExecType, pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd, nLBA);
			if (bProcessRet == RETURNED_EXIST_C2_ERROR) {
				bC2Error = TRUE;
				// C2 error points the current LBA - 1 (offset?)
				OutputLogA(standardError | fileC2Error,
					"\rLBA[%06d, %#07x] Detected C2 error %ld bit\n", nLBA, nLBA, pDiscPerSector->dwC2errorNum);
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
					if (!(IsValidProtectedSector(pDisc, nLBA) && (pDisc->PROTECT.byExist == codelock
						|| (pDisc->PROTECT.byExist == safeDisc && pDiscPerSector->dwC2errorNum == SAFEDISC_C2ERROR_NUM)))) {
						pDisc->MAIN.lpAllLBAOfC2Error[pDisc->MAIN.nC2ErrorCnt++] = nLBA;
					}
				}
			}
			else if (bProcessRet == RETURNED_SKIP_LBA) {
				if (pExtArg->byReverse) {
					nLBA = pDisc->SCSI.nFirstLBAof2ndSession - SESSION_TO_SESSION_SKIP_LBA;
				}
				else {
					nLBA = pDisc->MAIN.nFixFirstLBAof2ndSession - 1;
				}
			}
			else if (bProcessRet == RETURNED_CONTINUE) {
				ProcessReturnedContinue(pExecType, pExtArg, pDevice, pDisc, pDiscPerSector
					, nLBA, nMainDataType, byCurrentTrackNum, fpScm, fpSub, fpC2, fpParse);
			}
			else if (bProcessRet == RETURNED_FALSE) {
				throw FALSE;
			}

			if (bProcessRet != RETURNED_CONTINUE &&
				bProcessRet != RETURNED_SKIP_LBA) {
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData
					&& pExtArg->nC2RereadingType == 1 && nFirstLBA - pDisc->MAIN.nOffsetStart >= 0) {
					GetCrc32(&pDisc->MAIN.lpAllSectorCrc32[nFirstLBA - pDisc->MAIN.nOffsetStart]
						, pDiscPerSector->data.current, CD_RAW_SECTOR_SIZE);
				}
				SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.current, pDiscPerSector->subcode.current);
				if (!bSetLastLBA) {
					if (pDiscPerSector->subcode.current[13] == 0xaa) {
						pDisc->MAIN.nFixEndLBA = nLBA + pDisc->MAIN.nAdjustSectorNum;
						nLastLBA = nLBA + pDisc->MAIN.nOffsetEnd;
						bSetLastLBA = TRUE;
					}
					else {
						BYTE lpSubcodeRaw[CD_RAW_READ_SUBCODE_SIZE] = { 0 };
						if (IsCheckingSubChannel(pExtArg, pDisc, nLBA)) {
#if 1
							CheckAndFixSubChannel(pExecType, pExtArg, pDevice, pDisc
								, pDiscPerSector, byCurrentTrackNum, nLBA, FALSE, FALSE);
							if (pDisc->SUB.nCorruptCrcL && pDisc->SUB.nCorruptCrcH) {
								if (!bReread) {
									OutputSubErrorWithLBALogA(
										"Q Reread this sector because Crc16 doesn't match\n", nLBA, byCurrentTrackNum);
									bReread = TRUE;
									continue;
								}
								else {
									OutputSubErrorWithLBALogA("Q Reread NG\n", nLBA, byCurrentTrackNum);
									bReread = FALSE;
								}
							}
							else if (bReread) {
								OutputSubErrorWithLBALogA("Q Reread OK\n", nLBA, byCurrentTrackNum);
								bReread = FALSE;
							}
							// fix raw subchannel
							AlignColumnSubcode(pDiscPerSector->subcode.current, lpSubcodeRaw);
#endif
#if 0
								OutputCDSub96Align(pDiscPerSector->subcode.current, nLBA);
#endif
							WriteSubChannel(pDisc, lpSubcodeRaw,
								pDiscPerSector->subcode.current, nLBA, byCurrentTrackNum, fpSub, fpParse);
							CheckAndFixMainHeader(pExtArg, pDisc
								, pDiscPerSector, nLBA, byCurrentTrackNum, nMainDataType);
						}
//						SetTrackAttribution(pExecType, pExtArg, pDisc, nLBA,
//							&byCurrentTrackNum, &pDiscPerSector->mainHeader, &pDiscPerSector->subQ);
						UpdateTmpSubQData(&pDiscPerSector->subQ, FALSE, FALSE);
					}
				}
				// Write track to scrambled
				WriteMainChannel(pExtArg, pDisc, pDiscPerSector->data.current, nLBA, fpScm);
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
					if (pExtArg->byD8 || pDevice->byPlxtrDrive) {
						WriteC2(pExtArg, pDisc, pDiscPerSector->data.next + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
					}
					else {
						WriteC2(pExtArg, pDisc, pDiscPerSector->data.current + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
					}
				}
			}
			OutputString(_T("\rCreating .scm from %d to %d (LBA) %6d")
				, nStart + pDisc->MAIN.nOffsetStart, nEnd + pDisc->MAIN.nOffsetEnd, nLBA);
			nLBA++;
			nFirstLBA++;
		}
		OutputString(_T("\n"));
		FcloseAndNull(fpParse);
		FcloseAndNull(fpSub);
		FlushLog();
		nLBA = 0;

		if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
			if (bC2Error && pDisc->MAIN.nC2ErrorCnt > 0) {
				if (pExtArg->nC2RereadingType == 0) {
					if (!ReadCDForRereadingSectorType1(pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd, fpScm, fpC2)) {
						throw FALSE;
					}
				}
				else {
					INT nStartLBA = pExtArg->nStartLBAForC2;
					INT nEndLBA = pExtArg->nEndLBAForC2;
					if (nStartLBA == 0 && nEndLBA == 0) {
						nStartLBA = nStart;
						nEndLBA = nEnd;
					}
					if (!ReadCDForRereadingSectorType2(pExtArg, pDevice, pDisc, lpCmd, fpScm, fpC2, nStartLBA, nEndLBA)) {
						throw FALSE;
					}
				}
			}
			else {
				if (pDisc->PROTECT.byExist == PROTECT_TYPE_CD::no) {
					OutputLogA(standardOut | fileC2Error, "No C2 errors\n");
				}
				else {
					OutputLogA(standardOut | fileC2Error, "No unintentional C2 errors\n");
				}
			}
			FcloseAndNull(fpC2);
		}
		FcloseAndNull(fpScm);

		// eject
		bRet = StartStopUnit(pExtArg, pDevice, STOP_UNIT_CODE, START_UNIT_CODE);
		if (!CloseHandle(pDevice->hDevice)) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		OutputString(_T("Close the tray automatically after 3000 msec\n"));
		Sleep(3000);
		CONST size_t bufSize = 8;
		_TCHAR szBuf[8] = { 0 };
		if (!GetHandle(pDevice, szBuf, bufSize)) {
			throw FALSE;
		}
		// close
		bRet = StartStopUnit(pExtArg, pDevice, START_UNIT_CODE, START_UNIT_CODE);
		OutputString(_T("Wait 15000 msec until your drive recognizes the disc\n"));
		Sleep(15000);

		OutputString(_T("Read TOC\n"));
#endif
		if (!ReadTOC(pExtArg, pExecType, pDevice, pDisc)) {
			throw FALSE;
		}
		if (!ReadTOCFull(pExtArg, pDevice, pDisc, pDiscPerSector, fpCcd)) {
			throw FALSE;
		}
		if (!ReadCDCheck(pExecType, pExtArg, pDevice, pDisc, flg)) {
			throw FALSE;
		}
		if (NULL == (fpSub = CreateOrOpenFile(
			pszPath, NULL, NULL, NULL, NULL, _T(".sub"), _T("rb"), 0, 0))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		DWORD size = GetFileSize(0, fpSub);
#ifdef __DEBUG
		INT nLBA = 0;
		BYTE byCurrentTrackNum = 1;
#endif
		for (DWORD i = 0; i < size; i += CD_RAW_READ_SUBCODE_SIZE) {
			fread(pDiscPerSector->subcode.current, sizeof(BYTE), CD_RAW_READ_SUBCODE_SIZE, fpSub);
			SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.current, pDiscPerSector->subcode.current);
			if (IsCheckingSubChannel(pExtArg, pDisc, nLBA)) {
				CheckAndFixSubChannel(pExecType, pExtArg, pDevice, pDisc
					, pDiscPerSector, byCurrentTrackNum, nLBA, FALSE, FALSE);
			}
			SetTrackAttribution(pExecType, pExtArg, pDisc, nLBA,
				&byCurrentTrackNum, &pDiscPerSector->mainHeader, &pDiscPerSector->subQ);
			UpdateTmpSubQData(&pDiscPerSector->subQ, FALSE, FALSE);
			nLBA++;
		}
		if (!ProcessDescramble(pExtArg, pDisc, pszPath, pszScmPath)) {
			throw FALSE;
		}
		FlushLog();
		if (!ProcessCreateBin(pExtArg, pDevice, pDisc, pszPath, fpCue, fpCueForImg, fpCcd)) {
			throw FALSE;
		}
	}
	catch (BOOL ret) {
		bRet = ret;
	}
	FcloseAndNull(fpScm);
	FcloseAndNull(fpCueForImg);
	FcloseAndNull(fpCue);
	FcloseAndNull(fpParse);
	FcloseAndNull(fpSub);
	FcloseAndNull(fpC2);
	FreeAndNull(pBuf);

	return bRet;
}

BOOL ReadCDPartial(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	PDISC_PER_SECTOR pDiscPerSector,
	CDFLAG::_READ_CD::_ERROR_FLAGS c2,
	LPCTSTR pszPath,
	INT nStart,
	INT nEnd,
	CDFLAG::_READ_CD::_EXPECTED_SECTOR_TYPE flg,
	FILE* fpC2
) {
	CONST INT size = 5;
	_TCHAR szExt[size] = { 0 };

	if (*pExecType == gd) {
		_tcsncpy(szExt, _T(".scm"), size);
	}
	else {
		_tcsncpy(szExt, _T(".bin"), size);
	}
	_TCHAR pszBinPath[_MAX_PATH] = { 0 };
	FILE* fpBin = NULL;
	if (pExtArg->byReverse) {
		fpBin = CreateOrOpenFile(pszPath, _T("_reverse"), pszBinPath, NULL, NULL, szExt, _T("wb"), 0, 0);
	}
	else {
		fpBin = CreateOrOpenFile(pszPath, NULL, pszBinPath, NULL, NULL, szExt, _T("wb"), 0, 0);
	}
	if (!fpBin) {
		OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
		return FALSE;
	}
	BOOL bRet = TRUE;
	FILE* fpParse = NULL;
	FILE* fpSub = NULL;
	LPBYTE pBuf = NULL;
	LPBYTE pNextBuf = NULL;
	LPBYTE pNextNextBuf = NULL;
	INT nMainDataType = scrambled;
	if (*pExecType == data || pExtArg->byBe) {
		nMainDataType = unscrambled;
	}

	try {
		// init start
		if (!pExtArg->byReverse) {
			if (NULL == (fpParse = CreateOrOpenFile(
				pszPath, _T("_subReadable"), NULL, NULL, NULL, _T(".txt"), _T(WFLAG), 0, 0))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			if (NULL == (fpSub = CreateOrOpenFile(
				pszPath, NULL, NULL, NULL, NULL, _T(".sub"), _T("wb"), 0, 0))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
		}
		BYTE byTransferLen = 1;
		if (pDevice->byPlxtrDrive) {
			byTransferLen = 2;
		}
		// store main+(c2)+sub data
		if (!GetAlignedCallocatedBuffer(pDevice, &pBuf,
			pDevice->TRANSFER.dwBufLen * byTransferLen, &pDiscPerSector->data.current, _T(__FUNCTION__), __LINE__)) {
			throw FALSE;
		}
		if (1 <= pExtArg->dwSubAddionalNum) {
			if (!GetAlignedCallocatedBuffer(pDevice, &pNextBuf,
				pDevice->TRANSFER.dwBufLen * byTransferLen, &pDiscPerSector->data.next, _T(__FUNCTION__), __LINE__)) {
				throw FALSE;
			}
			if (2 <= pExtArg->dwSubAddionalNum) {
				if (!GetAlignedCallocatedBuffer(pDevice, &pNextNextBuf,
					pDevice->TRANSFER.dwBufLen * byTransferLen, &pDiscPerSector->data.nextNext, _T(__FUNCTION__), __LINE__)) {
					throw FALSE;
				}
			}
		}
		BYTE lpCmd[CDB12GENERIC_LENGTH] = { 0 };
		_TCHAR szSubCode[5] = { 0 };
		if ((pExtArg->byD8 || pDevice->byPlxtrDrive) && !pExtArg->byBe) {
			CDB::_PLXTR_READ_CDDA cdb = { 0 };
			if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
				_tcsncpy(szSubCode, _T("Raw"), sizeof(szSubCode) / sizeof(szSubCode[0]));
				SetReadD8Command(pDevice, &cdb, byTransferLen, CDFLAG::_PLXTR_READ_CDDA::MainC2Raw);
			}
			else {
				_tcsncpy(szSubCode, _T("Pack"), sizeof(szSubCode) / sizeof(szSubCode[0]));
				SetReadD8Command(pDevice, &cdb, byTransferLen, CDFLAG::_PLXTR_READ_CDDA::MainPack);
			}
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}
		else {
			// non plextor && support scrambled ripping
			CDB::_READ_CD cdb = { 0 };
			_tcsncpy(szSubCode, _T("Raw"), sizeof(szSubCode) / sizeof(szSubCode[0]));
			SetReadCDCommand(pDevice, &cdb, flg, byTransferLen, c2, CDFLAG::_READ_CD::Raw);
			memcpy(lpCmd, &cdb, CDB12GENERIC_LENGTH);
		}
		OutputLog(standardOut | fileDisc,
			_T("Set OpCode: %#02x, SubCode: %x(%s)\n"), lpCmd[0], lpCmd[10], szSubCode);

		BYTE lpPrevSubcode[CD_RAW_READ_SUBCODE_SIZE] = { 0 };
		if (pDisc->SUB.nSubChannelOffset) { // confirmed PXS88T, TS-H353A
			if (!ExecReadCD(pExtArg, pDevice, lpCmd, nStart - 2, pDiscPerSector->data.current,
				pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
				if (nStart == 0) {
					pDiscPerSector->subQ.current.nRelativeTime = 0;
					pDiscPerSector->subQ.current.nAbsoluteTime = 149;
					SetBufferFromTmpSubQData(pDiscPerSector->subQ.current, pDiscPerSector->subcode.current, 1);
					SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.prev, pDiscPerSector->subcode.current);
				}
				else {
					throw FALSE;
				}
			}
			else {
				AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
				SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.prev, pDiscPerSector->subcode.current);
			}
			if (!ExecReadCD(pExtArg, pDevice, lpCmd, nStart - 1, pDiscPerSector->data.current,
				pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
				if (nStart == 0) {
					pDiscPerSector->subQ.current.nRelativeTime = 0;
					pDiscPerSector->subQ.current.nAbsoluteTime = 150;
					SetBufferFromTmpSubQData(pDiscPerSector->subQ.current, pDiscPerSector->subcode.current, 1);
					for (INT i = 0; i < 12; i++) {
						pDiscPerSector->subcode.current[i] = 0xff;
					}
				}
				else {
					throw FALSE;
				}
			}
			else {
				AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
				SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.current, pDiscPerSector->subcode.current);
			}
			memcpy(lpPrevSubcode, pDiscPerSector->subcode.current, CD_RAW_READ_SUBCODE_SIZE);
		}
		else {
			if (*pExecType != gd) {
				if (!ExecReadCD(pExtArg, pDevice, lpCmd, nStart - 1, pDiscPerSector->data.current,
					pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
				}
				else {
					AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
					SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.prev, pDiscPerSector->subcode.current);
				}
			}
		}
		if (*pExecType == gd) {
			for (INT p = pDisc->SCSI.toc.FirstTrack - 1; p < pDisc->SCSI.toc.LastTrack; p++) {
				pDisc->SUB.lpEndCtlList[p] = pDisc->SCSI.toc.TrackData[p].Control;
			}
		}
		else {
			for (UINT p = 0; p < pDisc->SCSI.toc.LastTrack; p++) {
				if (!ExecReadCD(pExtArg, pDevice, lpCmd, pDisc->SCSI.lpFirstLBAListOnToc[p]
					, pDiscPerSector->data.current, pDevice->TRANSFER.dwBufLen * byTransferLen, _T(__FUNCTION__), __LINE__)) {
					throw FALSE;
				}
				AlignRowSubcode(pDiscPerSector->data.current + pDevice->TRANSFER.dwBufSubOffset, pDiscPerSector->subcode.current);
				pDisc->SUB.lpEndCtlList[p] = (BYTE)((pDiscPerSector->subcode.current[12] >> 4) & 0x0f);
				OutputString(_T("\rChecking SubQ ctl (Track) %2u/%2u"), p + 1, pDisc->SCSI.toc.LastTrack);
			}
			OutputString(_T("\n"));
		}
		SetCDOffset(pExecType, pExtArg->byBe, pDevice->byPlxtrDrive, pDisc, nStart, nEnd);
#ifdef _DEBUG
		OutputString(
			_T("byBe: %d, nCombinedOffset: %d, uiMainDataSlideSize: %u, nOffsetStart: %d, nOffsetEnd: %d, nFixStartLBA: %d, nFixEndLBA: %d\n")
			, pExtArg->byBe, pDisc->MAIN.nCombinedOffset, pDisc->MAIN.uiMainDataSlideSize
			, pDisc->MAIN.nOffsetStart, pDisc->MAIN.nOffsetEnd, pDisc->MAIN.nFixStartLBA, pDisc->MAIN.nFixEndLBA);
#endif
		BYTE byCurrentTrackNum = pDiscPerSector->subQ.prev.byTrackNum;
		if (byCurrentTrackNum < pDisc->SCSI.toc.FirstTrack || pDisc->SCSI.toc.LastTrack < byCurrentTrackNum) {
			byCurrentTrackNum = pDisc->SCSI.toc.FirstTrack;
		}
#ifdef _DEBUG
		OutputString(
			_T("byBe: %d, nCombinedOffset: %d, uiMainDataSlideSize: %u, nOffsetStart: %d, nOffsetEnd: %d, nFixStartLBA: %d, nFixEndLBA: %d\n")
			, pExtArg->byBe, pDisc->MAIN.nCombinedOffset, pDisc->MAIN.uiMainDataSlideSize
			, pDisc->MAIN.nOffsetStart, pDisc->MAIN.nOffsetEnd, pDisc->MAIN.nFixStartLBA, pDisc->MAIN.nFixEndLBA);
#endif
		INT nFirstLBA = nStart + pDisc->MAIN.nOffsetStart - 1;
		INT nLastLBA = nEnd + pDisc->MAIN.nOffsetEnd;
		if (*pExecType == gd) {
			if (nFirstLBA < 0) {
				nFirstLBA = 0;
			}
			else if (nFirstLBA == 44849) {
				nFirstLBA = 44850;
			}
		}
		INT nLBA = nFirstLBA;
		if (pExtArg->byReverse) {
			nLBA = nLastLBA;
		}
		// init end
		FlushLog();

		INT nStoreLBA = 0;
		INT nRetryCnt = 1;
		BOOL bC2Error = FALSE;
		INT bReread = FALSE;

		while (nFirstLBA < nLastLBA) {
			BOOL bProcessRet = ProcessReadCD(pExecType, pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd, nLBA);
			if (bProcessRet == RETURNED_EXIST_C2_ERROR) {
				bC2Error = TRUE;
				// C2 error points the current LBA - 1 (offset?)
				OutputLogA(standardError | fileC2Error,
					"\rLBA[%06d, %#07x] Detected C2 error %ld bit\n", nLBA, nLBA, pDiscPerSector->dwC2errorNum);
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
					if (!(IsValidProtectedSector(pDisc, nLBA) && (pDisc->PROTECT.byExist == codelock
						|| (pDisc->PROTECT.byExist == safeDisc && pDiscPerSector->dwC2errorNum == SAFEDISC_C2ERROR_NUM)))) {
						pDisc->MAIN.lpAllLBAOfC2Error[pDisc->MAIN.nC2ErrorCnt++] = nLBA;
					}
				}
			}
			else if (bProcessRet == RETURNED_SKIP_LBA) {
				if (pExtArg->byReverse) {
					nLBA = pDisc->SCSI.nFirstLBAof2ndSession - SESSION_TO_SESSION_SKIP_LBA;
				}
				else {
					nLBA = pDisc->MAIN.nFixFirstLBAof2ndSession - 1;
				}
			}
			else if (bProcessRet == RETURNED_CONTINUE) {
				ProcessReturnedContinue(pExecType, pExtArg, pDevice, pDisc, pDiscPerSector
					, nLBA, nMainDataType, byCurrentTrackNum, fpBin, fpSub, fpC2, fpParse);
			}
			else if (bProcessRet == RETURNED_FALSE) {
				if (*pExecType == gd && nRetryCnt <= 10 && nLBA > 65000) {
					OutputLog(standardError | fileMainError, _T("Retry %d/10\n"), nRetryCnt);
					INT nTmpLBA = 0;
					for (nTmpLBA = nLBA - 20000; 449849 <= nTmpLBA; nTmpLBA -= 20000) {
						OutputString(_T("Reread %d sector\n"), nTmpLBA);
						if (RETURNED_FALSE == ExecReadCDForC2(pExecType, pExtArg, pDevice, lpCmd, nTmpLBA,
							pDiscPerSector->data.current, _T(__FUNCTION__), __LINE__)) {
							if (nTmpLBA < 20000) {
								break;
							}
						}
						else {
							break;
						}
					}
					if (nStoreLBA == 0 && nRetryCnt == 1) {
						nStoreLBA = nLBA;
					}
					nLBA = nTmpLBA;
					nRetryCnt++;
					continue;
				}
				else {
					throw FALSE;
				}
			}
			if (nRetryCnt > 1) {
				if (nStoreLBA == nLBA) {
					// init
					nStoreLBA = 0;
					nRetryCnt = 1;
					OutputString(_T("\n"));
				}
				else {
					OutputString(_T("\rReread %d sector"), nLBA);
					nLBA++;
					continue;
				}
			}
			if (bProcessRet != RETURNED_CONTINUE &&
				bProcessRet != RETURNED_SKIP_LBA) {
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData
					&& pExtArg->nC2RereadingType == 1 && nFirstLBA - pDisc->MAIN.nOffsetStart >= 0) {
					GetCrc32(&pDisc->MAIN.lpAllSectorCrc32[nFirstLBA - pDisc->MAIN.nOffsetStart]
						, pDiscPerSector->data.current, CD_RAW_SECTOR_SIZE);
				}
				if (pDisc->SUB.nSubChannelOffset) {
					if (!IsValidProtectedSector(pDisc, nLBA)) {
						if (2 <= pExtArg->dwSubAddionalNum) {
							memcpy(pDiscPerSector->subcode.nextNext, pDiscPerSector->subcode.next, CD_RAW_READ_SUBCODE_SIZE);
						}
						if (1 <= pExtArg->dwSubAddionalNum) {
							memcpy(pDiscPerSector->subcode.next, pDiscPerSector->subcode.current, CD_RAW_READ_SUBCODE_SIZE);
						}
						memcpy(pDiscPerSector->subcode.current, lpPrevSubcode, CD_RAW_READ_SUBCODE_SIZE);
					}
				}
				SetTmpSubQDataFromBuffer(&pDiscPerSector->subQ.current, pDiscPerSector->subcode.current);

				if (nStart <= nLBA && nLBA < nEnd) {
					BYTE lpSubcodeRaw[CD_RAW_READ_SUBCODE_SIZE] = { 0 };
					if (!pExtArg->byReverse) {
						if (IsCheckingSubChannel(pExtArg, pDisc, nLBA)) {
							CheckAndFixSubChannel(pExecType, pExtArg, pDevice, pDisc
								, pDiscPerSector, byCurrentTrackNum, nLBA, FALSE, FALSE);
							if (pDisc->SUB.nCorruptCrcL && pDisc->SUB.nCorruptCrcH) {
								if (!bReread) {
									OutputSubErrorWithLBALogA(
										"Q Reread this sector because Crc16 doesn't match\n", nLBA, byCurrentTrackNum);
									bReread = TRUE;
									continue;
								}
								else {
									OutputSubErrorWithLBALogA("Q Reread NG\n", nLBA, byCurrentTrackNum);
									bReread = FALSE;
								}
							}
							else if (bReread) {
								OutputSubErrorWithLBALogA("Q Reread OK\n", nLBA, byCurrentTrackNum);
								bReread = FALSE;
							}
							// fix raw subchannel
							AlignColumnSubcode(pDiscPerSector->subcode.current, lpSubcodeRaw);
#if 0
							OutputCDSub96Align(pDiscPerSector->subcode.current, nLBA);
#endif
							WriteSubChannel(pDisc, lpSubcodeRaw,
								pDiscPerSector->subcode.current, nLBA, byCurrentTrackNum, fpSub, fpParse);
							CheckAndFixMainHeader(pExtArg, pDisc
								, pDiscPerSector, nLBA, byCurrentTrackNum, nMainDataType);
						}
						SetTrackAttribution(pExecType, pExtArg, pDisc, nLBA,
							&byCurrentTrackNum, &pDiscPerSector->mainHeader, &pDiscPerSector->subQ);
						UpdateTmpSubQData(&pDiscPerSector->subQ, FALSE, FALSE);
					}
				}
				// Write track to scrambled
				WriteMainChannel(pExtArg, pDisc, pDiscPerSector->data.current, nLBA, fpBin);
				if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
					if (pExtArg->byD8 || pDevice->byPlxtrDrive) {
						WriteC2(pExtArg, pDisc, pDiscPerSector->data.next + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
					}
					else {
						WriteC2(pExtArg, pDisc, pDiscPerSector->data.current + pDevice->TRANSFER.dwBufC2Offset, nLBA, fpC2);
					}
				}
				if (pDisc->SUB.nSubChannelOffset) {
					memcpy(lpPrevSubcode, pDiscPerSector->subcode.next, CD_RAW_READ_SUBCODE_SIZE);
				}
			}
			if (pExtArg->byReverse) {
				OutputString(_T("\rCreating %s from %d to %d (LBA) %6d"), szExt
					, nEnd + pDisc->MAIN.nOffsetEnd, nStart + pDisc->MAIN.nOffsetStart - 1, nLBA);
				nLBA--;
			}
			else {
				OutputString(_T("\rCreating %s from %d to %d (LBA) %6d"), szExt
					, nStart + pDisc->MAIN.nOffsetStart, nEnd + pDisc->MAIN.nOffsetEnd, nLBA);
				nLBA++;
			}
			nFirstLBA++;
		}
		OutputString(_T("\n"));
		FcloseAndNull(fpParse);
		FcloseAndNull(fpSub);
		FlushLog();

		if (*pExecType == gd) {
			for (INT i = pDisc->SCSI.toc.FirstTrack - 1; i < pDisc->SCSI.toc.LastTrack; i++) {
				BOOL bErr = FALSE;
				LONG lLine = 0;
				if (pDisc->SUB.lpFirstLBAListOnSub[i][1] == -1) {
					bErr = TRUE;
					lLine = __LINE__;
				}
				else if ((pDisc->SCSI.toc.TrackData[i].Control & AUDIO_DATA_TRACK) == AUDIO_DATA_TRACK) {
					if (pDisc->SUB.lpFirstLBAListOfDataTrackOnSub[i] == -1) {
						bErr = TRUE;
						lLine = __LINE__;
					}
					else if (pDisc->SUB.lpLastLBAListOfDataTrackOnSub[i] == -1) {
						bErr = TRUE;
						lLine = __LINE__;
					}
				}
				if (bErr) {
					OutputErrorString(
						_T("[L:%ld] Internal error. Failed to analyze the subchannel. Track[%02u]/[%02u]\n"),
						lLine, i + 1, pDisc->SCSI.toc.LastTrack);
					throw FALSE;
				}
			}
		}
		if (pExtArg->byC2 && pDevice->FEATURE.byC2ErrorData) {
			if (bC2Error && pDisc->MAIN.nC2ErrorCnt > 0) {
				if (pExtArg->nC2RereadingType == 0) {
					if (!ReadCDForRereadingSectorType1(pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd, fpBin, fpC2)) {
						throw FALSE;
					}
				}
				else {
					INT nStartLBA = pExtArg->nStartLBAForC2;
					INT nEndLBA = pExtArg->nEndLBAForC2;
					if (nStartLBA == 0 && nEndLBA == 0) {
						nStartLBA = nStart;
						nEndLBA = nEnd;
					}
					if (!ReadCDForRereadingSectorType2(pExtArg, pDevice, pDisc, lpCmd, fpBin, fpC2, nStartLBA, nEndLBA)) {
						throw FALSE;
					}
				}
			}
			else {
				if (pDisc->PROTECT.byExist == PROTECT_TYPE_CD::no) {
					OutputLogA(standardOut | fileC2Error, "No C2 errors\n");
				}
				else {
					OutputLogA(standardOut | fileC2Error, "No unintentional C2 errors\n");
				}
			}
			FcloseAndNull(fpC2);
		}
		FcloseAndNull(fpBin);
		if (*pExecType == gd) {
			OutputTocWithPregap(pDisc);
		}

		if (pExtArg->byReverse) {
			FILE* fpBin_r = NULL;
			if (NULL == (fpBin_r = CreateOrOpenFile(
				pszPath, _T("_reverse"), NULL, NULL, NULL, szExt, _T("rb"), 0, 0))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			if (NULL == (fpBin = CreateOrOpenFile(
				pszPath, NULL, NULL, NULL, NULL, szExt, _T("wb"), 0, 0))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			OutputString(_T("Reversing _reverse%s to %s\n"), szExt, szExt);
			BYTE rBuf[CD_RAW_SECTOR_SIZE] = { 0 };
			DWORD dwRoop = GetFileSize(0, fpBin_r) - CD_RAW_SECTOR_SIZE * 3;
			LONG lSeek = CD_RAW_SECTOR_SIZE - (LONG)pDisc->MAIN.uiMainDataSlideSize;
			fseek(fpBin_r, -lSeek, SEEK_END);
			fread(rBuf, sizeof(BYTE), (size_t)lSeek, fpBin_r);
			fwrite(rBuf, sizeof(BYTE), (size_t)lSeek, fpBin);
			fseek(fpBin_r, -CD_RAW_SECTOR_SIZE, SEEK_CUR);

			for (DWORD i = 0; i < dwRoop; i += CD_RAW_SECTOR_SIZE) {
				fseek(fpBin_r, -CD_RAW_SECTOR_SIZE, SEEK_CUR);
				fread(rBuf, sizeof(BYTE), CD_RAW_SECTOR_SIZE, fpBin_r);
				fwrite(rBuf, sizeof(BYTE), CD_RAW_SECTOR_SIZE, fpBin);
				fseek(fpBin_r, -CD_RAW_SECTOR_SIZE, SEEK_CUR);
			}
			fseek(fpBin_r, -CD_RAW_SECTOR_SIZE, SEEK_CUR);
			fread(rBuf, sizeof(BYTE), pDisc->MAIN.uiMainDataSlideSize, fpBin_r);
			fwrite(rBuf, sizeof(BYTE), pDisc->MAIN.uiMainDataSlideSize, fpBin);

			FcloseAndNull(fpBin);
			FcloseAndNull(fpBin_r);
		}

		if (*pExecType == data) {
			if ((pExtArg->byD8 || pDevice->byPlxtrDrive) && !pExtArg->byBe) {
				if (NULL == (fpBin = CreateOrOpenFile(
					pszPath, NULL, NULL, NULL, NULL, _T(".bin"), _T("rb+"), 0, 0))) {
					OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
					throw FALSE;
				}
				DescrambleMainChannelPartial(nStart, nEnd - 1, scrambled_table, fpBin);
				FcloseAndNull(fpBin);
			}
			ExecEccEdc(pExtArg->byScanProtectViaFile, pDisc->PROTECT, pszPath, pDisc->PROTECT.ERROR_SECTOR);
		}
		else if (*pExecType == gd) {
			_TCHAR pszImgPath[_MAX_PATH] = { 0 };
			if (!DescrambleMainChannelForGD(pszPath, pszImgPath)) {
				throw FALSE;
			}
			ExecEccEdc(pExtArg->byScanProtectViaFile, pDisc->PROTECT, pszImgPath, pDisc->PROTECT.ERROR_SECTOR);
			if (!CreateBinCueForGD(pDisc, pszPath)) {
				throw FALSE;
			}
		}
	}
	catch (BOOL ret) {
		bRet = ret;
	}
	FcloseAndNull(fpBin);
	FcloseAndNull(fpParse);
	FcloseAndNull(fpSub);
	FcloseAndNull(fpC2);
	FreeAndNull(pBuf);
	if (1 <= pExtArg->dwSubAddionalNum) {
		FreeAndNull(pNextBuf);
		if (2 <= pExtArg->dwSubAddionalNum) {
			FreeAndNull(pNextNextBuf);
		}
	}
	return bRet;
}
