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

#if ETC_WINDOWS
#include <windows.h>
#endif
#include <ctime>
#include <chrono>
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
		m_pafrgbaSource = nullptr;

		m_pablock = nullptr;

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
					Block4x4EncodingBits::Format const a_encodingbitsformat,
					Image *a_pimageSource, ErrorMetric a_errormetric)
	{
		assert(a_encodingbitsformat != Block4x4EncodingBits::Format::UNKNOWN);
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
		m_errormetric = a_errormetric;
		
		unsigned char *paucEncodingBits = a_paucEncidingBits;
		unsigned int uiEncodingBitsBytesPerBlock = Block4x4EncodingBits::GetBytesPerBlock(a_encodingbitsformat);

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
