
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
	// set the encoding bits (for the output file) based on the best encoding for each block
	//
	void Executor::SetEncodingBits(unsigned int const a_uiOffset,
								unsigned int const a_uiStride)
	{
		assert(a_uiStride > 0);

		for (unsigned int uiBlock = a_uiOffset;
				uiBlock < m_image.GetNumberOfBlocks();
				uiBlock += a_uiStride)
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
