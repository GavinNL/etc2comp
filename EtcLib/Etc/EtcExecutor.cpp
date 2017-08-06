

#include "EtcExecutor.h"

namespace Etc {

	Executor::Executor(Image& image)
		: m_image(image)
	{}

	Image::EncodingStatus Executor::Encode(Image::Format const a_format, ErrorMetric const a_errormetric, float const a_fEffort, unsigned int const a_uiJobs, unsigned int const a_uiMaxJobs)
	{

		auto const start = std::chrono::steady_clock::now();

		auto status = m_image.Encode(*this, a_format, a_errormetric, a_fEffort, a_uiJobs, a_uiMaxJobs);

		auto const end = std::chrono::steady_clock::now();
		auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
		m_msEncodeTime = elapsed;

		return status;
	}

} // namespace Etc
