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

#pragma once

//#include "Etc.h"
#include "EtcColorFloatRGBA.h"
#include "EtcBlock4x4EncodingBits.h"
#include "EtcErrorMetric.h"


namespace Etc
{
	class Block4x4;
	class EncoderSpec;
	class SortedBlockList;

    class Image
    {
    public:
		
		enum class Format
		{
			UNKNOWN,
			//
			ETC1,
			//
			// ETC2 formats
			RGB8,
			SRGB8,
			RGBA8,
			SRGBA8,
			R11,
			SIGNED_R11,
			RG11,
			SIGNED_RG11,
			RGB8A1,
			SRGB8A1,
			//
			FORMATS,
			//
			DEFAULT = SRGB8
		};

		// constructor using source image
		Image(float *a_pafSourceRGBA, unsigned int a_uiSourceWidth,
				unsigned int a_uiSourceHeight,
				ErrorMetric a_errormetric);

		// constructor using encoding bits
		Image(Format a_format, 
				unsigned int a_uiSourceWidth, unsigned int a_uiSourceHeight,
				unsigned char *a_paucEncodingBits, unsigned int a_uiEncodingBitsBytes,
				Block4x4EncodingBits::Format a_encodingbitsformat,
				Image *a_pimageSource,
				ErrorMetric a_errormetric);

		~Image(void);
public:
		
		inline unsigned int GetSourceWidth(void)
		{
			return m_uiSourceWidth;
		}

		inline unsigned int GetSourceHeight(void)
		{
			return m_uiSourceHeight;
		}

		inline unsigned int GetExtendedWidth(void)
		{
			return m_uiExtendedWidth;
		}

		inline unsigned int GetExtendedHeight(void)
		{
			return m_uiExtendedHeight;
		}

		inline unsigned int GetNumberOfBlocks()
		{
			return m_uiBlockColumns * m_uiBlockRows;
		}

		inline Block4x4 * GetBlocks()
		{
			return m_pablock;
		}


		float GetError(void);

		inline ColorFloatRGBA * GetSourcePixel(unsigned int a_uiH, unsigned int a_uiV)
		{
			if (a_uiH >= m_uiSourceWidth || a_uiV >= m_uiSourceHeight)
			{
				return nullptr;
			}

			return &m_pafrgbaSource[a_uiV*m_uiSourceWidth + a_uiH];
		}

		inline Format GetFormat(void)
		{
			return m_format;
		}

		inline static unsigned short CalcExtendedDimension(unsigned short a_ushOriginalDimension)
		{
			return (unsigned short)((a_ushOriginalDimension + 3) & ~3);
		}

		inline ErrorMetric GetErrorMetric(void)
		{
			return m_errormetric;
		}

		static const char * EncodingFormatToString(Image::Format a_format);
		const char * EncodingFormatToString(void);
		//used to get basic information about the image data
		int m_iNumOpaquePixels;
		int m_iNumTranslucentPixels;
		int m_iNumTransparentPixels;

		ColorFloatRGBA m_numColorValues;
		ColorFloatRGBA m_numOutOfRangeValues;

	private:

		Image(void);


		// inputs
		ColorFloatRGBA *m_pafrgbaSource;
		unsigned int m_uiSourceWidth;
		unsigned int m_uiSourceHeight;
		unsigned int m_uiExtendedWidth;
		unsigned int m_uiExtendedHeight;
		unsigned int m_uiBlockColumns;
		unsigned int m_uiBlockRows;
		// intermediate data
		Block4x4 *m_pablock;
		// encoding
		Format m_format;
		ErrorMetric m_errormetric;

		friend class Executor;
	};

} // namespace Etc
