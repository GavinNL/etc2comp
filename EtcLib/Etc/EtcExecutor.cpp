
#include <future>

#include "Etc.h"
#include "EtcBlock4x4.h"
#include "EtcExecutor.h"
#include "EtcSortedBlockList.h"

namespace Etc {

	Executor::Executor(Image& image)
		: m_image(image)
	{}

	Executor::EncodingStatus Executor::InitEncode(Format const a_format, ErrorMetric const a_errormetric, float const a_fEffort)
	{
		m_encodingStatus = EncodingStatus::SUCCESS;

		m_image.m_format = a_format;
		m_errormetric = a_errormetric;
		m_fEffort = a_fEffort;

		if (m_errormetric < 0 || m_errormetric > ERROR_METRICS)
		{
			AddToEncodingStatus(ERROR_UNKNOWN_ERROR_METRIC);
			return m_encodingStatus;
		}

		if (m_fEffort < ETCCOMP_MIN_EFFORT_LEVEL)
		{
			AddToEncodingStatus(WARNING_EFFORT_OUT_OF_RANGE);
			m_fEffort = ETCCOMP_MIN_EFFORT_LEVEL;
		}
		else if (m_fEffort > ETCCOMP_MAX_EFFORT_LEVEL)
		{
			AddToEncodingStatus(WARNING_EFFORT_OUT_OF_RANGE);
			m_fEffort = ETCCOMP_MAX_EFFORT_LEVEL;
		}

		m_encodingbitsformat = DetermineEncodingBitsFormat(m_image.GetFormat());

		if (m_encodingbitsformat == Block4x4EncodingBits::Format::UNKNOWN)
		{
			AddToEncodingStatus(ERROR_UNKNOWN_FORMAT);
			return m_encodingStatus;
		}

		assert(m_paucEncodingBits == nullptr);
		m_uiEncodingBitsBytes = m_image.GetNumberOfBlocks() * Block4x4EncodingBits::GetBytesPerBlock(m_encodingbitsformat);
		m_paucEncodingBits = new unsigned char[m_uiEncodingBitsBytes];

		InitBlocksAndBlockSorter();
		return m_encodingStatus;
	}

	unsigned int Executor::CalculateJobs(unsigned int a_uiJobs, unsigned int const a_uiMaxJobs) {
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
	Executor::EncodingStatus Executor::Encode(Format a_format, ErrorMetric a_errormetric, float a_fEffort, unsigned int a_uiJobs, unsigned int a_uiMaxJobs)
	{
		auto encodingStatus = InitEncode(a_format, a_errormetric, a_fEffort);

		if (IsError(encodingStatus))
		{
			return m_encodingStatus;
		}

		a_uiJobs = CalculateJobs(a_uiJobs, a_uiMaxJobs);

		std::future<void> *handle = new std::future<void>[a_uiMaxJobs];

		unsigned int uiNumThreadsNeeded = 0;
		unsigned int uiUnfinishedBlocks = m_image.GetNumberOfBlocks();

		uiNumThreadsNeeded = (uiUnfinishedBlocks < a_uiJobs) ? uiUnfinishedBlocks : a_uiJobs;

		for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
		{
			handle[i] = async(std::launch::async, &Executor::RunFirstPass, this, m_fEffort, i, uiNumThreadsNeeded);
		}

		RunFirstPass(m_fEffort, uiNumThreadsNeeded - 1, uiNumThreadsNeeded);

		for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
		{
			handle[i].get();
		}

		// perform effort-based encoding
		if (m_fEffort > ETCCOMP_MIN_EFFORT_LEVEL)
		{
			unsigned int uiFinishedBlocks = 0;
			unsigned int uiTotalEffortBlocks = static_cast<unsigned int>(roundf(0.01f * m_fEffort  * m_image.GetNumberOfBlocks()));

			if (m_bVerboseOutput)
			{
				printf("effortblocks = %d\n", uiTotalEffortBlocks);
			}
			unsigned int uiPass = 0;
			while (1)
			{
				if (m_bVerboseOutput)
				{
					uiPass++;
					printf("pass %u\n", uiPass);
				}
				m_psortedblocklist->Sort();
				uiUnfinishedBlocks = m_psortedblocklist->GetNumberOfSortedBlocks();
				uiFinishedBlocks = m_image.GetNumberOfBlocks() - uiUnfinishedBlocks;
				if (m_bVerboseOutput)
				{
					printf("    %u unfinished blocks\n", uiUnfinishedBlocks);
					// m_psortedblocklist->Print();
				}

				//stop enocding when we did enough to satify the effort percentage
				if (uiFinishedBlocks >= uiTotalEffortBlocks)
				{
					if (m_bVerboseOutput)
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
					uiIteratedBlocks = IterateThroughWorstBlocks(m_fEffort, blocksToIterateThisPass, 0, 1);
				}
				else
				{
					//we have a lot of work to do, so lets multi thread it
					std::future<unsigned int> *handleToBlockEncoders = new std::future<unsigned int>[uiNumThreadsNeeded-1];

					for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
					{
						handleToBlockEncoders[i] = async(std::launch::async, &Executor::IterateThroughWorstBlocks, this, m_fEffort, blocksToIterateThisPass, i, uiNumThreadsNeeded);
					}
					uiIteratedBlocks = IterateThroughWorstBlocks(m_fEffort, blocksToIterateThisPass, uiNumThreadsNeeded - 1, uiNumThreadsNeeded);

					for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
					{
						uiIteratedBlocks += handleToBlockEncoders[i].get();
					}

					delete[] handleToBlockEncoders;
				}

				if (m_bVerboseOutput)
				{
					printf("    %u iterated blocks\n", uiIteratedBlocks);
				}
			}
		}

		// generate Etc2-compatible bit-format 4x4 blocks
		for (int i = 0; i < (int)a_uiJobs - 1; i++)
		{
			handle[i] = async(std::launch::async, &Executor::SetEncodingBits, this, i, a_uiJobs);
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
	unsigned int Executor::IterateThroughWorstBlocks(float const a_fEffort,
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

			plink->GetBlock()->PerformEncodingIteration(m_image.GetFormat(), m_errormetric, a_fEffort);

			uiIteratedBlocks += a_uiMultithreadingStride;
		}

		return uiIteratedBlocks;
	}

	// ----------------------------------------------------------------------------------------------------
	// determine which warnings to check for during Encode() based on encoding format
	//
	void Executor::FindEncodingWarningTypesForCurFormat()
	{
		m_warningsToCapture = GetEncodingWarningTypes(m_image.GetFormat());
	}

	// ----------------------------------------------------------------------------------------------------
	// examine source pixels to check for warnings
	//
	void Executor::FindAndSetEncodingWarnings()
	{
		auto const warning = FindEncodingWarning();
		if (warning == EncodingStatus::SUCCESS)
		{
			return;
		}

		AddToEncodingStatusIfSignfigant(warning);
	}

	Executor::EncodingStatus Executor::FindEncodingWarning() const {
		int numPixels = (m_image.m_uiBlockRows * 4) * (m_image.m_uiBlockColumns * 4);
		EncodingStatus warnings = EncodingStatus::SUCCESS;
		if (m_image.m_iNumOpaquePixels == numPixels)
		{
			warnings |= EncodingStatus::WARNING_ALL_OPAQUE_PIXELS;
		}
		if (m_image.m_iNumOpaquePixels < numPixels)
		{
			warnings |= EncodingStatus::WARNING_SOME_NON_OPAQUE_PIXELS;
		}
		if (m_image.m_iNumTranslucentPixels > 0)
		{
			warnings |= EncodingStatus::WARNING_SOME_TRANSLUCENT_PIXELS;
		}
		if (m_image.m_iNumTransparentPixels == numPixels)
		{
			warnings |= EncodingStatus::WARNING_ALL_TRANSPARENT_PIXELS;
		}
		if (m_image.m_numColorValues.fB > 0.0f)
		{
			warnings |= EncodingStatus::WARNING_SOME_BLUE_VALUES_ARE_NOT_ZERO;
		}
		if (m_image.m_numColorValues.fG > 0.0f)
		{
			warnings |= EncodingStatus::WARNING_SOME_GREEN_VALUES_ARE_NOT_ZERO;
		}

		if (m_image.m_numOutOfRangeValues.fR > 0.0f || m_image.m_numOutOfRangeValues.fG > 0.0f)
		{
			warnings |= EncodingStatus::WARNING_SOME_RGBA_NOT_0_TO_1;
		}
		if (m_image.m_numOutOfRangeValues.fB > 0.0f || m_image.m_numOutOfRangeValues.fA > 0.0f)
		{
			warnings |= EncodingStatus::WARNING_SOME_RGBA_NOT_0_TO_1;
		}
		return warnings;
	}

		// ----------------------------------------------------------------------------------------------------
	// init image blocks prior to encoding
	// init block sorter for subsequent sortings
	// check for encoding warnings
	//
	void Executor::InitBlocksAndBlockSorter(void)
	{
		FindEncodingWarningTypesForCurFormat();

		// init each block
		Block4x4 *pblock = m_image.m_pablock;
		unsigned char *paucEncodingBits = m_paucEncodingBits;
		for (unsigned int uiBlockRow = 0; uiBlockRow < m_image.m_uiBlockRows; uiBlockRow++)
		{
			unsigned int uiBlockV = uiBlockRow * 4;

			for (unsigned int uiBlockColumn = 0; uiBlockColumn < m_image.m_uiBlockColumns; uiBlockColumn++)
			{
				unsigned int uiBlockH = uiBlockColumn * 4;

				pblock->InitFromSource(&m_image, uiBlockH, uiBlockV, paucEncodingBits, m_errormetric);

				paucEncodingBits += Block4x4EncodingBits::GetBytesPerBlock(m_encodingbitsformat);

				pblock++;
			}
		}

		FindAndSetEncodingWarnings();

		// init block sorter
		{
			m_psortedblocklist = new SortedBlockList(m_image.GetNumberOfBlocks(), 100);

			for (unsigned int uiBlock = 0; uiBlock < m_image.GetNumberOfBlocks(); uiBlock++)
			{
				pblock = &m_image.m_pablock[uiBlock];
				m_psortedblocklist->AddBlock(pblock);
			}
		}

	}

	// ----------------------------------------------------------------------------------------------------
	// run the first pass of the encoder
	// the encoder generally finds a reasonable, fast encoding
	// this is run on all blocks regardless of effort to ensure that all blocks have a valid encoding
	//
	void Executor::RunFirstPass(float const a_fEffort,
								unsigned int a_uiMultithreadingOffset,
								unsigned int a_uiMultithreadingStride)
	{
		assert(a_uiMultithreadingStride > 0);

		for (unsigned int uiBlock = a_uiMultithreadingOffset;
				uiBlock < m_image.GetNumberOfBlocks();
				uiBlock += a_uiMultithreadingStride)
		{
			Block4x4 *pblock = &m_image.m_pablock[uiBlock];
			pblock->PerformEncodingIteration(m_image.GetFormat(), m_errormetric, a_fEffort);
		}
	}

    // ----------------------------------------------------------------------------------------------------
	// set the encoding bits (for the output file) based on the best encoding for each block
	//
	void Executor::SetEncodingBits(unsigned int a_uiMultithreadingOffset,
								unsigned int a_uiMultithreadingStride)
	{
		assert(a_uiMultithreadingStride > 0);

		for (unsigned int uiBlock = a_uiMultithreadingOffset;
				uiBlock < m_image.GetNumberOfBlocks();
				uiBlock += a_uiMultithreadingStride)
		{
			Block4x4 *pblock = &m_image.m_pablock[uiBlock];
			pblock->SetEncodingBitsFromEncoding(m_image.GetFormat());
		}

	}
	// ----------------------------------------------------------------------------------------------------
	// determine the encoding bits format based on the encoding format
	// the encoding bits format is a family of bit encodings that are shared across various encoding formats
	//
	Block4x4EncodingBits::Format Executor::DetermineEncodingBitsFormat(Format a_format)
	{
		Block4x4EncodingBits::Format encodingbitsformat;

		// determine encoding bits format from image format
		switch (a_format)
		{
		case Format::ETC1:
		case Format::RGB8:
		case Format::SRGB8:
			encodingbitsformat = Block4x4EncodingBits::Format::RGB8;
			break;

		case Format::RGBA8:
		case Format::SRGBA8:
			encodingbitsformat = Block4x4EncodingBits::Format::RGBA8;
			break;

		case Format::R11:
		case Format::SIGNED_R11:
			encodingbitsformat = Block4x4EncodingBits::Format::R11;
			break;

		case Format::RG11:
		case Format::SIGNED_RG11:
			encodingbitsformat = Block4x4EncodingBits::Format::RG11;
			break;

		case Format::RGB8A1:
		case Format::SRGB8A1:
			encodingbitsformat = Block4x4EncodingBits::Format::RGB8A1;
			break;

		default:
			encodingbitsformat = Block4x4EncodingBits::Format::UNKNOWN;
			break;
		}

		return encodingbitsformat;
	}

} // namespace Etc
