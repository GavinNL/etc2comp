
#pragma once

#include "EtcExecutor.h"

namespace Etc {

	class ThreadedExecutor : public Executor
	{
	public:
		ThreadedExecutor(Image& a_image);

		EncodingStatus Encode(Image::Format a_format, ErrorMetric a_errormetric, float a_fEffort,
			unsigned int a_uiJobs, unsigned int a_uiMaxJobs);
	private:
		unsigned int IterateThroughWorstBlocks(float a_fEffort,
										unsigned int a_uiMaxBlocks,
										unsigned int a_uiMultithreadingOffset,
										unsigned int a_uiMultithreadingStride);

		void RunFirstPass(float a_fEffort,
							unsigned int a_uiMultithreadingOffset,
							unsigned int a_uiMultithreadingStride);

		unsigned int CalculateJobs(unsigned int a_uiJobs, unsigned int a_uiMaxJobs);
	};

} // namespace Etc
