
#include <future>

#include "Etc.h"
#include "EtcBlock4x4.h"
#include "EtcSortedBlockList.h"
#include "EtcThreadedExecutor.h"

namespace Etc {

	ThreadedExecutor::ThreadedExecutor(Image& a_image)
		: Executor(a_image)
	{}

	unsigned int ThreadedExecutor::CalculateJobs(unsigned int a_uiJobs, unsigned int const a_uiMaxJobs) {
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
	Executor::EncodingStatus ThreadedExecutor::Encode(Format a_format, ErrorMetric a_errormetric, float a_fEffort, unsigned int a_uiJobs, unsigned int a_uiMaxJobs)
	{
		auto encodingStatus = InitEncode(a_format, a_errormetric, a_fEffort);

		if (IsError(encodingStatus))
		{
			return encodingStatus;
		}

		a_uiJobs = CalculateJobs(a_uiJobs, a_uiMaxJobs);

		std::future<void> *handle = new std::future<void>[a_uiMaxJobs];

		unsigned int uiNumThreadsNeeded = 0;
		unsigned int uiUnfinishedBlocks = GetImage().GetNumberOfBlocks();

		uiNumThreadsNeeded = (uiUnfinishedBlocks < a_uiJobs) ? uiUnfinishedBlocks : a_uiJobs;

		for (int i = 0; i < (int)uiNumThreadsNeeded - 1; i++)
		{
			handle[i] = async(std::launch::async, &ThreadedExecutor::RunFirstPass, this, m_fEffort, i, uiNumThreadsNeeded);
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
			unsigned int uiTotalEffortBlocks = static_cast<unsigned int>(roundf(0.01f * m_fEffort  * GetImage().GetNumberOfBlocks()));

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
				uiFinishedBlocks = GetImage().GetNumberOfBlocks() - uiUnfinishedBlocks;
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
						handleToBlockEncoders[i] = async(std::launch::async, &ThreadedExecutor::IterateThroughWorstBlocks, this, m_fEffort, blocksToIterateThisPass, i, uiNumThreadsNeeded);
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
			handle[i] = async(std::launch::async, [this, i, a_uiJobs]() {
				SetEncodingBits(i, a_uiJobs);
			});
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
	unsigned int ThreadedExecutor::IterateThroughWorstBlocks(float const a_fEffort,
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

			plink->GetBlock()->PerformEncodingIteration(GetImage().GetFormat(), GetErrorMetric(), a_fEffort);

			uiIteratedBlocks += a_uiMultithreadingStride;
		}

		return uiIteratedBlocks;
	}

	// ----------------------------------------------------------------------------------------------------
	// run the first pass of the encoder
	// the encoder generally finds a reasonable, fast encoding
	// this is run on all blocks regardless of effort to ensure that all blocks have a valid encoding
	//
	void ThreadedExecutor::RunFirstPass(float const a_fEffort,
								unsigned int a_uiMultithreadingOffset,
								unsigned int a_uiMultithreadingStride)
	{
		assert(a_uiMultithreadingStride > 0);

		for (unsigned int uiBlock = a_uiMultithreadingOffset;
				uiBlock < GetImage().GetNumberOfBlocks();
				uiBlock += a_uiMultithreadingStride)
		{
			Block4x4 *pblock = &GetImage().GetBlocks()[uiBlock];
			pblock->PerformEncodingIteration(GetImage().GetFormat(), GetErrorMetric(), a_fEffort);
		}
	}

} // namespace Etc