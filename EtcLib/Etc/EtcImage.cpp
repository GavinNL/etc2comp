/*
 * Copyright 2015 The Etc2Comp Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
EtcImage.cpp

Image is an array of 4x4 blocks that represent the encoding of the source image

*/

#include "EtcConfig.h"

#include <cstdlib>

#include "EtcImage.h"

#include "Etc.h"
#include "EtcBlock4x4.h"
#include "EtcBlock4x4EncodingBits.h"
#include "EtcExecutor.h"
#include "EtcSortedBlockList.h"

#if ETC_WINDOWS
#include <windows.h>
#endif
#include <ctime>
#include <chrono>
#include <future>
#include <cstdio>
#include <cstring>
#include <cassert>

// fix conflict with Block4x4::AlphaMix
#ifdef OPAQUE
#undef OPAQUE
#endif
#ifdef TRANSPARENT
#undef TRANSPARENT
#endif

namespace Etc
{

	// ----------------------------------------------------------------------------------------------------
	//
	Image::Image(void)
	{
		m_encodingStatus = EncodingStatus::SUCCESS;
		m_warningsToCapture = EncodingStatus::SUCCESS;
		m_pafrgbaSource = nullptr;

		m_pablock = nullptr;

		m_encodingbitsformat = Block4x4EncodingBits::Format::UNKNOWN;
		m_uiEncodingBitsBytes = 0;
		m_paucEncodingBits = nullptr;

		m_format = Format::UNKNOWN;
		m_iNumOpaquePixels = 0;
		m_iNumTranslucentPixels = 0;
		m_iNumTransparentPixels = 0;
	}

	// ----------------------------------------------------------------------------------------------------
	// constructor using source image
	// used to set state before Encode() is called
	//
	Image::Image(float *a_pafSourceRGBA, unsigned int a_uiSourceWidth,
					unsigned int a_uiSourceHeight, 
					ErrorMetric a_errormetric)
	{
		m_encodingStatus = EncodingStatus::SUCCESS;
		m_warningsToCapture = EncodingStatus::SUCCESS;
		m_pafrgbaSource = (ColorFloatRGBA *) a_pafSourceRGBA;
		m_uiSourceWidth = a_uiSourceWidth;
		m_uiSourceHeight = a_uiSourceHeight;

		m_uiExtendedWidth = CalcExtendedDimension((unsigned short)m_uiSourceWidth);
		m_uiExtendedHeight = CalcExtendedDimension((unsigned short)m_uiSourceHeight);

		m_uiBlockColumns = m_uiExtendedWidth >> 2;
		m_uiBlockRows = m_uiExtendedHeight >> 2;

		m_pablock = new Block4x4[GetNumberOfBlocks()];
		assert(m_pablock);

		m_format = Format::UNKNOWN;

		m_encodingbitsformat = Block4x4EncodingBits::Format::UNKNOWN;
		m_uiEncodingBitsBytes = 0;
		m_paucEncodingBits = nullptr;

		m_errormetric = a_errormetric;

		m_iNumOpaquePixels = 0;
		m_iNumTranslucentPixels = 0;
		m_iNumTransparentPixels = 0;
	}

	// ----------------------------------------------------------------------------------------------------
	// constructor using encoding bits
	// recreates encoding state using a previously encoded image
	//
	Image::Image(Format a_format,
					unsigned int a_uiSourceWidth, unsigned int a_uiSourceHeight,
					unsigned char *a_paucEncidingBits, unsigned int a_uiEncodingBitsBytes,
					Image *a_pimageSource, ErrorMetric a_errormetric)
	{
		m_encodingStatus = EncodingStatus::SUCCESS;
		m_pafrgbaSource = nullptr;
		m_uiSourceWidth = a_uiSourceWidth;
		m_uiSourceHeight = a_uiSourceHeight;

		m_uiExtendedWidth = CalcExtendedDimension((unsigned short)m_uiSourceWidth);
		m_uiExtendedHeight = CalcExtendedDimension((unsigned short)m_uiSourceHeight);

		m_uiBlockColumns = m_uiExtendedWidth >> 2;
		m_uiBlockRows = m_uiExtendedHeight >> 2;

		unsigned int uiBlocks = GetNumberOfBlocks();

		m_pablock = new Block4x4[uiBlocks];
		assert(m_pablock);

		m_format = a_format;

		m_iNumOpaquePixels = 0;
		m_iNumTranslucentPixels = 0;
		m_iNumTransparentPixels = 0;
		
		m_encodingbitsformat = DetermineEncodingBitsFormat(m_format);
		if (m_encodingbitsformat == Block4x4EncodingBits::Format::UNKNOWN)
		{
			AddToEncodingStatus(ERROR_UNKNOWN_FORMAT);
			return;
		}
		m_uiEncodingBitsBytes = a_uiEncodingBitsBytes;
		m_paucEncodingBits = a_paucEncidingBits;

		m_errormetric = a_errormetric;
		
		unsigned char *paucEncodingBits = m_paucEncodingBits;
		unsigned int uiEncodingBitsBytesPerBlock = Block4x4EncodingBits::GetBytesPerBlock(m_encodingbitsformat);

		unsigned int uiH = 0;
		unsigned int uiV = 0;
		for (unsigned int uiBlock = 0; uiBlock < uiBlocks; uiBlock++)
		{
			m_pablock[uiBlock].InitFromEtcEncodingBits(a_format, uiH, uiV, paucEncodingBits, 
														a_pimageSource, a_errormetric);
			paucEncodingBits += uiEncodingBitsBytesPerBlock;
			uiH += 4;
			if (uiH >= m_uiSourceWidth)
			{
				uiH = 0;
				uiV += 4;
			}
		}

	}

	// ----------------------------------------------------------------------------------------------------
	//
	Image::~Image(void)
	{
		if (m_pablock != nullptr)
		{
			delete[] m_pablock;
			m_pablock = nullptr;
		}

		/*if (m_paucEncodingBits != nullptr)
		{
			delete[] m_paucEncodingBits;
			m_paucEncodingBits = nullptr;
		}*/
	}

	Image::EncodingStatus Image::InitEncode(Executor& a_executor, Format const a_format, ErrorMetric const a_errormetric, float const a_fEffort)
	{
		m_encodingStatus = EncodingStatus::SUCCESS;

		m_format = a_format;
		m_errormetric = a_errormetric;
		a_executor.m_fEffort = a_fEffort;

		if (m_errormetric < 0 || m_errormetric > ERROR_METRICS)
		{
			AddToEncodingStatus(ERROR_UNKNOWN_ERROR_METRIC);
			return m_encodingStatus;
		}

		if (a_executor.m_fEffort < ETCCOMP_MIN_EFFORT_LEVEL)
		{
			AddToEncodingStatus(WARNING_EFFORT_OUT_OF_RANGE);
			a_executor.m_fEffort = ETCCOMP_MIN_EFFORT_LEVEL;
		}
		else if (a_executor.m_fEffort > ETCCOMP_MAX_EFFORT_LEVEL)
		{
			AddToEncodingStatus(WARNING_EFFORT_OUT_OF_RANGE);
			a_executor.m_fEffort = ETCCOMP_MAX_EFFORT_LEVEL;
		}

		m_encodingbitsformat = DetermineEncodingBitsFormat(m_format);

		if (m_encodingbitsformat == Block4x4EncodingBits::Format::UNKNOWN)
		{
			AddToEncodingStatus(ERROR_UNKNOWN_FORMAT);
			return m_encodingStatus;
		}

		assert(m_paucEncodingBits == nullptr);
		m_uiEncodingBitsBytes = GetNumberOfBlocks() * Block4x4EncodingBits::GetBytesPerBlock(m_encodingbitsformat);
		m_paucEncodingBits = new unsigned char[m_uiEncodingBitsBytes];

		InitBlocksAndBlockSorter();
		return m_encodingStatus;
	}

	unsigned int Image::CalculateJobs(unsigned int a_uiJobs, unsigned int const a_uiMaxJobs) {
		if (a_uiJobs < 1)
		{
			a_uiJobs = 1;
			AddToEncodingStatus(WARNING_JOBS_OUT_OF_RANGE);
		}
		else if (a_uiJobs > a_uiMaxJobs)
		{
			a_uiJobs = a_uiMaxJobs;
			AddToEncodingStatus(WARNING_JOBS_OUT_OF_RANGE);
		}

		return a_uiJobs;
	}

	// ----------------------------------------------------------------------------------------------------
	// encode an image
	// create a set of encoding bits that conforms to a_format
	// find best fit using a_errormetric
	// explore a range of possible encodings based on a_fEffort (range = [0:100])
	// speed up process using a_uiJobs as the number of process threads (a_uiJobs must not excede a_uiMaxJobs)
	//
	Image::EncodingStatus Image::Encode(Executor& a_executor, Format a_format, ErrorMetric a_errormetric, float a_fEffort, unsigned int a_uiJobs, unsigned int a_uiMaxJobs)
	{
		auto encodingStatus = InitEncode(a_executor, a_format, a_errormetric, a_fEffort);

		if (IsError(encodingStatus))
		{
			return m_encodingStatus;
		}

		a_uiJobs = CalculateJobs(a_uiJobs, a_uiMaxJobs);

		std::future<void> *handle = new std::future<void>[a_uiMaxJobs];

		unsigned int uiNumThreadsNeeded = 0;
		unsigned int uiUnfinishedBlocks = GetNumberOfBlocks();

		uiNumThreadsNeeded = (uiUnfinishedBlocks < a_uiJobs) ? uiUnfinishedBlocks : a_uiJobs;
			
		for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
		{
			handle[i] = async(std::launch::async, &Image::RunFirstPass, this, a_executor.m_fEffort, i, uiNumThreadsNeeded);
		}

		RunFirstPass(a_executor.m_fEffort, uiNumThreadsNeeded - 1, uiNumThreadsNeeded);

		for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
		{
			handle[i].get();
		}

		// perform effort-based encoding
		if (a_executor.m_fEffort > ETCCOMP_MIN_EFFORT_LEVEL)
		{
			unsigned int uiFinishedBlocks = 0;
			unsigned int uiTotalEffortBlocks = static_cast<unsigned int>(roundf(0.01f * a_executor.m_fEffort  * GetNumberOfBlocks()));

			if (a_executor.m_bVerboseOutput)
			{
				printf("effortblocks = %d\n", uiTotalEffortBlocks);
			}
			unsigned int uiPass = 0;
			while (1)
			{
				if (a_executor.m_bVerboseOutput)
				{
					uiPass++;
					printf("pass %u\n", uiPass);
				}
				m_psortedblocklist->Sort();
				uiUnfinishedBlocks = m_psortedblocklist->GetNumberOfSortedBlocks();
				uiFinishedBlocks = GetNumberOfBlocks() - uiUnfinishedBlocks;
				if (a_executor.m_bVerboseOutput)
				{
					printf("    %u unfinished blocks\n", uiUnfinishedBlocks);
					// m_psortedblocklist->Print();
				}

				

				//stop enocding when we did enough to satify the effort percentage
				if (uiFinishedBlocks >= uiTotalEffortBlocks)
				{
					if (a_executor.m_bVerboseOutput)
					{
						printf("Finished %d Blocks out of %d\n", uiFinishedBlocks, uiTotalEffortBlocks);
					}
					break;
				}

				unsigned int uiIteratedBlocks = 0;
				unsigned int blocksToIterateThisPass = (uiTotalEffortBlocks - uiFinishedBlocks);
				uiNumThreadsNeeded = (uiUnfinishedBlocks < a_uiJobs) ? uiUnfinishedBlocks : a_uiJobs;

				if (uiNumThreadsNeeded <= 1)
				{
					//since we already how many blocks each thread will process
					//cap the thread limit to do the proper amount of work, and not more
					uiIteratedBlocks = IterateThroughWorstBlocks(a_executor.m_fEffort, blocksToIterateThisPass, 0, 1);
				}
				else
				{
					//we have a lot of work to do, so lets multi thread it
					std::future<unsigned int> *handleToBlockEncoders = new std::future<unsigned int>[uiNumThreadsNeeded-1];

					for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
					{
						handleToBlockEncoders[i] = async(std::launch::async, &Image::IterateThroughWorstBlocks, this, a_executor.m_fEffort, blocksToIterateThisPass, i, uiNumThreadsNeeded);
					}
					uiIteratedBlocks = IterateThroughWorstBlocks(a_executor.m_fEffort, blocksToIterateThisPass, uiNumThreadsNeeded - 1, uiNumThreadsNeeded);

					for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
					{
						uiIteratedBlocks += handleToBlockEncoders[i].get();
					}

					delete[] handleToBlockEncoders;
				}

				if (a_executor.m_bVerboseOutput)
				{
					printf("    %u iterated blocks\n", uiIteratedBlocks);
				}
			}
		}

		// generate Etc2-compatible bit-format 4x4 blocks
		for (int i = 0; i < (int)a_uiJobs - 1; i++)
		{
			handle[i] = async(std::launch::async, &Image::SetEncodingBits, this, i, a_uiJobs);
		}
		SetEncodingBits(a_uiJobs - 1, a_uiJobs);

		for (int i = 0; i < (int)a_uiJobs - 1; i++)
		{
			handle[i].get();
		}

		delete[] handle;
		delete m_psortedblocklist;
		return m_encodingStatus;
	}

	// ----------------------------------------------------------------------------------------------------
	// iterate the encoding thru the blocks with the worst error
	// stop when a_uiMaxBlocks blocks have been iterated
	// split the blocks between the process threads using a_uiMultithreadingOffset and a_uiMultithreadingStride
	//
	unsigned int Image::IterateThroughWorstBlocks(float const a_fEffort,
													unsigned int a_uiMaxBlocks,
													unsigned int a_uiMultithreadingOffset, 
													unsigned int a_uiMultithreadingStride)
	{
		assert(a_uiMultithreadingStride > 0);
		unsigned int uiIteratedBlocks = a_uiMultithreadingOffset;

		SortedBlockList::Link *plink = m_psortedblocklist->GetLinkToFirstBlock();
		for (plink = plink->Advance(a_uiMultithreadingOffset);
				plink != nullptr;
				plink = plink->Advance(a_uiMultithreadingStride) )
		{
			if (uiIteratedBlocks >= a_uiMaxBlocks)
			{
				break;
			}

			plink->GetBlock()->PerformEncodingIteration(m_format, m_errormetric, a_fEffort);

			uiIteratedBlocks += a_uiMultithreadingStride;	
		}

		return uiIteratedBlocks;
	}

	// ----------------------------------------------------------------------------------------------------
	// determine which warnings to check for during Encode() based on encoding format
	//
	void Image::FindEncodingWarningTypesForCurFormat()
	{
		m_warningsToCapture = GetEncodingWarningTypes(m_format);
	}

	// ----------------------------------------------------------------------------------------------------
	// examine source pixels to check for warnings
	//
	void Image::FindAndSetEncodingWarnings()
	{
		int numPixels = (m_uiBlockRows * 4) * (m_uiBlockColumns * 4);
		if (m_iNumOpaquePixels == numPixels)
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_ALL_OPAQUE_PIXELS);
		}
		if (m_iNumOpaquePixels < numPixels)
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_SOME_NON_OPAQUE_PIXELS);
		}
		if (m_iNumTranslucentPixels > 0)
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_SOME_TRANSLUCENT_PIXELS);
		}
		if (m_iNumTransparentPixels == numPixels)
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_ALL_TRANSPARENT_PIXELS);
		}
		if (m_numColorValues.fB > 0.0f)
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_SOME_BLUE_VALUES_ARE_NOT_ZERO);
		}
		if (m_numColorValues.fG > 0.0f) 
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_SOME_GREEN_VALUES_ARE_NOT_ZERO);
		}

		if (m_numOutOfRangeValues.fR > 0.0f || m_numOutOfRangeValues.fG > 0.0f)
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_SOME_RGBA_NOT_0_TO_1);
		}
		if (m_numOutOfRangeValues.fB > 0.0f || m_numOutOfRangeValues.fA > 0.0f)
		{
			AddToEncodingStatusIfSignfigant(Image::EncodingStatus::WARNING_SOME_RGBA_NOT_0_TO_1);
		}
	}
	
	// ----------------------------------------------------------------------------------------------------
	// return a string name for a given image format
	//
	const char * Image::EncodingFormatToString(Image::Format a_format)
	{
		switch (a_format)
		{
		case Image::Format::ETC1:
			return "ETC1";
		case Image::Format::RGB8:
			return "RGB8";
		case Image::Format::SRGB8:
			return "SRGB8";

		case Image::Format::RGB8A1:
			return "RGB8A1";
		case Image::Format::SRGB8A1:
			return "SRGB8A1";
		case Image::Format::RGBA8:
			return "RGBA8";
		case Image::Format::SRGBA8:
			return "SRGBA8";

		case Image::Format::R11:
			return "R11";
		case Image::Format::SIGNED_R11:
			return "SIGNED_R11";

		case Image::Format::RG11:
			return "RG11";
		case Image::Format::SIGNED_RG11:
			return "SIGNED_RG11";
		case Image::Format::FORMATS:
		case Image::Format::UNKNOWN:
		default:
			return "UNKNOWN";
		}
	}

	// ----------------------------------------------------------------------------------------------------
	// return a string name for the image's format
	//
	const char * Image::EncodingFormatToString(void)
	{
		return EncodingFormatToString(m_format);
	}

	// ----------------------------------------------------------------------------------------------------
	// init image blocks prior to encoding
	// init block sorter for subsequent sortings
	// check for encoding warnings
	//
	void Image::InitBlocksAndBlockSorter(void)
	{
		
		FindEncodingWarningTypesForCurFormat();

		// init each block
		Block4x4 *pblock = m_pablock;
		unsigned char *paucEncodingBits = m_paucEncodingBits;
		for (unsigned int uiBlockRow = 0; uiBlockRow < m_uiBlockRows; uiBlockRow++)
		{
			unsigned int uiBlockV = uiBlockRow * 4;

			for (unsigned int uiBlockColumn = 0; uiBlockColumn < m_uiBlockColumns; uiBlockColumn++)
			{
				unsigned int uiBlockH = uiBlockColumn * 4;

				pblock->InitFromSource(this, uiBlockH, uiBlockV, paucEncodingBits, m_errormetric);

				paucEncodingBits += Block4x4EncodingBits::GetBytesPerBlock(m_encodingbitsformat);

				pblock++;
			}
		}

		FindAndSetEncodingWarnings();

		// init block sorter
		{
			m_psortedblocklist = new SortedBlockList(GetNumberOfBlocks(), 100);

			for (unsigned int uiBlock = 0; uiBlock < GetNumberOfBlocks(); uiBlock++)
			{
				pblock = &m_pablock[uiBlock];
				m_psortedblocklist->AddBlock(pblock);
			}
		}

	}

	// ----------------------------------------------------------------------------------------------------
	// run the first pass of the encoder
	// the encoder generally finds a reasonable, fast encoding
	// this is run on all blocks regardless of effort to ensure that all blocks have a valid encoding
	//
	void Image::RunFirstPass(float const a_fEffort,
								unsigned int a_uiMultithreadingOffset,
								unsigned int a_uiMultithreadingStride)
	{
		assert(a_uiMultithreadingStride > 0);

		for (unsigned int uiBlock = a_uiMultithreadingOffset;
				uiBlock < GetNumberOfBlocks(); 
				uiBlock += a_uiMultithreadingStride)
		{
			Block4x4 *pblock = &m_pablock[uiBlock];
			pblock->PerformEncodingIteration(m_format, m_errormetric, a_fEffort);
		}
	}

    // ----------------------------------------------------------------------------------------------------
	// set the encoding bits (for the output file) based on the best encoding for each block
	//
	void Image::SetEncodingBits(unsigned int a_uiMultithreadingOffset,
								unsigned int a_uiMultithreadingStride)
	{
		assert(a_uiMultithreadingStride > 0);

		for (unsigned int uiBlock = a_uiMultithreadingOffset; 
				uiBlock < GetNumberOfBlocks(); 
				uiBlock += a_uiMultithreadingStride)
		{
			Block4x4 *pblock = &m_pablock[uiBlock];
			pblock->SetEncodingBitsFromEncoding(m_format);
		}

	}

	// ----------------------------------------------------------------------------------------------------
	// return the image error
	// image error is the sum of all block errors
	//
	float Image::GetError(void)
	{
		float fError = 0.0f;

		for (unsigned int uiBlock = 0; uiBlock < GetNumberOfBlocks(); uiBlock++)
		{
			Block4x4 *pblock = &m_pablock[uiBlock];
			fError += pblock->GetError();
		}

		return fError;
	}

	// ----------------------------------------------------------------------------------------------------
	//

}	// namespace Etc
