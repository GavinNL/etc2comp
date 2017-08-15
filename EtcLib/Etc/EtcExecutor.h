
#pragma once

#include <chrono>

#include "EtcImage.h"

namespace Etc {

	class Executor
	{
	public:
		using Format = Image::Format;

		//the differnt warning and errors that can come up during encoding
		enum EncodingStatus : std::uint32_t
		{
			SUCCESS = 0,
			//
			WARNING_THRESHOLD = 1 << 0,
			//
			WARNING_EFFORT_OUT_OF_RANGE = 1 << 1,
			WARNING_JOBS_OUT_OF_RANGE = 1 << 2,
			WARNING_SOME_NON_OPAQUE_PIXELS = 1 << 3,//just for opaque formats, etc1, rgb8, r11, rg11
			WARNING_ALL_OPAQUE_PIXELS = 1 << 4,
			WARNING_ALL_TRANSPARENT_PIXELS = 1 << 5,
			WARNING_SOME_TRANSLUCENT_PIXELS = 1 << 6,//just for rgb8A1
			WARNING_SOME_RGBA_NOT_0_TO_1 = 1 << 7,
			WARNING_SOME_BLUE_VALUES_ARE_NOT_ZERO = 1 << 8,
			WARNING_SOME_GREEN_VALUES_ARE_NOT_ZERO = 1 << 9,
			//
			ERROR_THRESHOLD = 1 << 16,
			//
			ERROR_UNKNOWN_FORMAT = 1 << 17,
			ERROR_UNKNOWN_ERROR_METRIC = 1 << 18,
			ERROR_ZERO_WIDTH_OR_HEIGHT = 1 << 19,
			//
		};

		Executor(Image& image);

		inline void AddToEncodingStatus(EncodingStatus a_encStatus)
		{
			m_encodingStatus = (EncodingStatus)((unsigned int)m_encodingStatus | (unsigned int)a_encStatus);
		}

		inline unsigned char * GetEncodingBits(void)
		{
			return m_paucEncodingBits;
		}

		inline unsigned int GetEncodingBitsBytes(void)
		{
			return m_uiEncodingBitsBytes;
		}
		static Block4x4EncodingBits::Format DetermineEncodingBitsFormat(Format a_format);

		inline ErrorMetric GetErrorMetric(void)
		{
			return m_errormetric;
		}

		inline Image& GetImage() const
		{
			return m_image;
		}
	protected:
		EncodingStatus InitEncode(Format a_format, ErrorMetric a_errormetric, float a_fEffort);

		void SetEncodingBits(unsigned int a_uiMultithreadingOffset,
								unsigned int a_uiMultithreadingStride);
	private:

		//add a warning or error to check for while encoding
		inline void TrackEncodingWarning(EncodingStatus a_encStatus)
		{
			m_warningsToCapture = (EncodingStatus)((unsigned int)m_warningsToCapture | (unsigned int)a_encStatus);
		}

		//report the warning if it is something we care about for this encoding
		inline void AddToEncodingStatusIfSignfigant(EncodingStatus a_encStatus)
		{
			if ((EncodingStatus)((unsigned int)m_warningsToCapture & (unsigned int)a_encStatus) == a_encStatus)
			{
				AddToEncodingStatus(a_encStatus);
			}
		}

		void FindEncodingWarningTypesForCurFormat();
		void FindAndSetEncodingWarnings();

		EncodingStatus FindEncodingWarning() const;

		void InitBlocksAndBlockSorter(void);

		Image& m_image;
	protected:
		float m_fEffort = 0.0f;
	private:
		Block4x4EncodingBits::Format m_encodingbitsformat = Block4x4EncodingBits::Format::UNKNOWN;
		unsigned int m_uiEncodingBitsBytes = 0;		// for entire image
		unsigned char *m_paucEncodingBits = nullptr;
		ErrorMetric m_errormetric;
		
	protected:
		SortedBlockList *m_psortedblocklist;
		//this will hold any warning or errors that happen during encoding
		EncodingStatus m_encodingStatus = EncodingStatus::SUCCESS;
	private:
		//these will be the warnings we are tracking
		EncodingStatus m_warningsToCapture = EncodingStatus::SUCCESS;
	public:
		bool m_bVerboseOutput = false;

		friend class Image;
	};


	constexpr bool IsError(Executor::EncodingStatus const status)
	{
		return ((status & Executor::ERROR_THRESHOLD) - 1) >= (Executor::ERROR_THRESHOLD - 1);
	}

	constexpr Executor::EncodingStatus GetEncodingWarningTypes(Image::Format const a_format)
	{
#define TrackEncodingWarning(x) warnings = static_cast<Executor::EncodingStatus>(warnings | Executor::x)
		Executor::EncodingStatus warnings = Executor::SUCCESS;
		TrackEncodingWarning(WARNING_ALL_TRANSPARENT_PIXELS);
		TrackEncodingWarning(WARNING_SOME_RGBA_NOT_0_TO_1);
		switch (a_format)
		{
		case Image::Format::ETC1:
		case Image::Format::RGB8:
		case Image::Format::SRGB8:
			TrackEncodingWarning(WARNING_SOME_NON_OPAQUE_PIXELS);
			TrackEncodingWarning(WARNING_SOME_TRANSLUCENT_PIXELS);
			break;

		case Image::Format::RGB8A1:
		case Image::Format::SRGB8A1:
			TrackEncodingWarning(WARNING_SOME_TRANSLUCENT_PIXELS);
			TrackEncodingWarning(WARNING_ALL_OPAQUE_PIXELS);
			break;
		case Image::Format::RGBA8:
		case Image::Format::SRGBA8:
			TrackEncodingWarning(WARNING_ALL_OPAQUE_PIXELS);
			break;

		case Image::Format::R11:
		case Image::Format::SIGNED_R11:
			TrackEncodingWarning(WARNING_SOME_NON_OPAQUE_PIXELS);
			TrackEncodingWarning(WARNING_SOME_TRANSLUCENT_PIXELS);
			TrackEncodingWarning(WARNING_SOME_GREEN_VALUES_ARE_NOT_ZERO);
			TrackEncodingWarning(WARNING_SOME_BLUE_VALUES_ARE_NOT_ZERO);
			break;

		case Image::Format::RG11:
		case Image::Format::SIGNED_RG11:
			TrackEncodingWarning(WARNING_SOME_NON_OPAQUE_PIXELS);
			TrackEncodingWarning(WARNING_SOME_TRANSLUCENT_PIXELS);
			TrackEncodingWarning(WARNING_SOME_BLUE_VALUES_ARE_NOT_ZERO);
			break;
		case Image::Format::FORMATS:
		case Image::Format::UNKNOWN:
		default:
			assert(0);
			break;
		}
#undef TrackEncodingWarning
		return warnings;
	}

	constexpr Executor::EncodingStatus operator|(Executor::EncodingStatus const lhs, Executor::EncodingStatus const rhs)
	{
		return static_cast<Executor::EncodingStatus>(
			std::underlying_type_t<Executor::EncodingStatus>(lhs)
			| std::underlying_type_t<Executor::EncodingStatus>(rhs)
		);
	}

	constexpr Executor::EncodingStatus& operator|=(Executor::EncodingStatus& lhs, Executor::EncodingStatus const rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	// ----------------------------------------------------------------------------------------------------
	// determine the encoding bits format based on the encoding format
	// the encoding bits format is a family of bit encodings that are shared across various encoding formats
	//
	constexpr Block4x4EncodingBits::Format DetermineEncodingBitsFormat(Image::Format const a_format)
	{
		// determine encoding bits format from image format
		switch (a_format)
		{
		case Image::Format::ETC1:
		case Image::Format::RGB8:
		case Image::Format::SRGB8:
			return Block4x4EncodingBits::Format::RGB8;

		case Image::Format::RGBA8:
		case Image::Format::SRGBA8:
			return Block4x4EncodingBits::Format::RGBA8;

		case Image::Format::R11:
		case Image::Format::SIGNED_R11:
			return Block4x4EncodingBits::Format::R11;

		case Image::Format::RG11:
		case Image::Format::SIGNED_RG11:
			return Block4x4EncodingBits::Format::RG11;

		case Image::Format::RGB8A1:
		case Image::Format::SRGB8A1:
			return Block4x4EncodingBits::Format::RGB8A1;

		default:
			return Block4x4EncodingBits::Format::UNKNOWN;
		}
	}

	class TimeEncodeResult
	{
	public:
		using Milliseconds = std::chrono::milliseconds;
		Milliseconds m_msEncodeTime;
		Executor::EncodingStatus m_status;
	};

	template <auto i>
	auto& get(TimeEncodeResult& result) {
		if constexpr (i == 0)
		{
			return result.m_msEncodeTime;
		}

		if constexpr (i == 1)
		{
			return result.m_status;
		}

		assert(false);
	}

	template <typename Executor, typename... Args>
	TimeEncodeResult TimeEncode(Executor&& a_executor, Args&&... args)
	{
		auto const start = std::chrono::steady_clock::now();

		auto status = a_executor.Encode(std::forward<Args>(args)...);

		auto const end = std::chrono::steady_clock::now();
		auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		return {elapsed, status};
	}

} // namespace Etc
