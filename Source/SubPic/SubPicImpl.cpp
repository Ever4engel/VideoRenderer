/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2024 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "Utils/Util.h"
#include "SubPicImpl.h"

//
// CSubPicImpl
//

CSubPicImpl::CSubPicImpl()
	: CUnknown(L"CSubPicImpl", nullptr)
{
}

STDMETHODIMP CSubPicImpl::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	return
		QI(ISubPic)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPic

STDMETHODIMP_(REFERENCE_TIME) CSubPicImpl::GetStart()
{
	return m_rtStart;
}

STDMETHODIMP_(REFERENCE_TIME) CSubPicImpl::GetStop()
{
	return m_rtStop;
}

STDMETHODIMP_(REFERENCE_TIME) CSubPicImpl::GetSegmentStart()
{
	return m_rtSegmentStart >= 0 ? m_rtSegmentStart : m_rtStart;
}

STDMETHODIMP_(REFERENCE_TIME) CSubPicImpl::GetSegmentStop()
{
	return m_rtSegmentStop >= 0 ? m_rtSegmentStop : m_rtStop;
}

STDMETHODIMP_(void) CSubPicImpl::SetSegmentStart(REFERENCE_TIME rtStart)
{
	m_rtSegmentStart = rtStart;
}

STDMETHODIMP_(void) CSubPicImpl::SetSegmentStop(REFERENCE_TIME rtStop)
{
	m_rtSegmentStop = rtStop;
}

STDMETHODIMP_(void) CSubPicImpl::SetStart(REFERENCE_TIME rtStart)
{
	m_rtStart = rtStart;
}

STDMETHODIMP_(void) CSubPicImpl::SetStop(REFERENCE_TIME rtStop)
{
	m_rtStop = rtStop;
}

STDMETHODIMP CSubPicImpl::CopyTo(ISubPic* pSubPic)
{
	CheckPointer(pSubPic, E_POINTER);

	pSubPic->SetStart(m_rtStart);
	pSubPic->SetStop(m_rtStop);
	pSubPic->SetSegmentStart(m_rtSegmentStart);
	pSubPic->SetSegmentStop(m_rtSegmentStop);
	pSubPic->SetDirtyRect(m_rcDirty);
	pSubPic->SetSize(m_size, m_vidrect);
	pSubPic->SetVirtualTextureSize(m_virtualTextureSize, m_virtualTextureTopLeft);
	pSubPic->SetInverseAlpha(m_bInvAlpha);

	return S_OK;
}

STDMETHODIMP CSubPicImpl::GetDirtyRect(RECT* pDirtyRect)
{
	return pDirtyRect ? *pDirtyRect = m_rcDirty, S_OK : E_POINTER;
}

STDMETHODIMP CSubPicImpl::GetSourceAndDest(
	RECT rcWindow, RECT rcVideo,
	RECT* pRcSource, RECT* pRcDest,
	BOOL bPositionRelative, CPoint ShiftPos,
	int xOffsetInPixels, const BOOL bUseSpecialCase) const
{
	if (m_rcDirty.IsRectEmpty()) {
		// for some reason needed for XySubFilter
		return E_ABORT;
	}

	CheckPointer(pRcSource, E_POINTER);
	CheckPointer(pRcDest, E_POINTER);

	if (m_size.cx > 0 && m_size.cy > 0) {
		double scaleX = 1.0, scaleY = 1.0;

		const CRect rcTarget = bPositionRelative || m_eSubtitleType != SUBTITLE_TYPE::ST_TEXT ? rcVideo : rcWindow;
		const CSize szTarget = rcTarget.Size();
		const bool bNeedSpecialCase = !!bUseSpecialCase && (m_eSubtitleType == SUBTITLE_TYPE::ST_HDMV || m_eSubtitleType == SUBTITLE_TYPE::ST_DVB || m_eSubtitleType == SUBTITLE_TYPE::ST_XYSUBPIC) && m_virtualTextureSize.cx > 720;
		bool bMatchesAR = true;
		if (bNeedSpecialCase) {
			const double subtitleAR	= double(m_virtualTextureSize.cx) / m_virtualTextureSize.cy;
			const double videoAR	= double(szTarget.cx) / szTarget.cy;

			const auto diffAR = std::abs(videoAR - subtitleAR);
			bMatchesAR = diffAR < 0.001;
			if (bMatchesAR || videoAR > subtitleAR) {
				scaleX = scaleY = double(szTarget.cx) / m_virtualTextureSize.cx;
			} else {
				scaleX = scaleY = double(szTarget.cy) / m_virtualTextureSize.cy;
			}
		} else {
			scaleX = double(szTarget.cx) / m_virtualTextureSize.cx;
			scaleY = double(szTarget.cy) / m_virtualTextureSize.cy;
		}
		CPoint offset = rcTarget.TopLeft();

		CRect rcTemp = m_rcDirty;
		*pRcSource = rcTemp;

		rcTemp.OffsetRect(m_virtualTextureTopLeft + CPoint(xOffsetInPixels, 0));
		rcTemp = CRect(lround(rcTemp.left   * scaleX),
					   lround(rcTemp.top    * scaleY),
					   lround(rcTemp.right  * scaleX),
					   lround(rcTemp.bottom * scaleY));
		rcTemp.OffsetRect(offset);

		if (bNeedSpecialCase && !bMatchesAR) {
			const auto extraHeight = szTarget.cy - m_virtualTextureSize.cy * scaleY;
			const auto extraWidth = szTarget.cx - m_virtualTextureSize.cx * scaleX;
			if (extraHeight != 0.0 || extraWidth != 0.0) {
				offset.SetPoint(lround(extraWidth / 2.), lround(extraHeight / 2.));
				rcTemp.OffsetRect(offset);
			}

			// shift bitmap subtitles if they go beyond the right/bottom border of the frame AND window
			offset.SetPoint(0, 0);
			const CSize szFixCrop(std::max(rcTarget.right, rcWindow.right), std::max(rcTarget.bottom, rcWindow.bottom));
			if (rcTemp.right > szFixCrop.cx) {
				offset.x -= (rcTemp.right - szFixCrop.cx);
			}
			if (rcTemp.bottom > szFixCrop.cy) {
				offset.y -= (rcTemp.bottom - szFixCrop.cy);
			}
			if (offset.x != 0 || offset.y != 0) {
				rcTemp.OffsetRect(offset);
			}
		}

		rcTemp.OffsetRect(ShiftPos);
		*pRcDest = rcTemp;

		return S_OK;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CSubPicImpl::SetDirtyRect(RECT* pDirtyRect)
{
	return pDirtyRect ? m_rcDirty = *pDirtyRect, S_OK : E_POINTER;
}

STDMETHODIMP CSubPicImpl::GetMaxSize(SIZE* pMaxSize)
{
	return pMaxSize ? *pMaxSize = m_maxsize, S_OK : E_POINTER;
}

STDMETHODIMP CSubPicImpl::SetSize(SIZE size, RECT vidrect)
{
	m_size = size;
	m_vidrect = vidrect;

	if (m_size.cx > m_maxsize.cx) {
		m_size.cy = MulDiv(m_size.cy, m_maxsize.cx, m_size.cx);
		m_size.cx = m_maxsize.cx;
	}

	if (m_size.cy > m_maxsize.cy) {
		m_size.cx = MulDiv(m_size.cx, m_maxsize.cy, m_size.cy);
		m_size.cy = m_maxsize.cy;
	}

	if (m_size.cx != size.cx || m_size.cy != size.cy) {
		m_vidrect.top    = MulDiv(m_vidrect.top,    m_size.cx, size.cx);
		m_vidrect.bottom = MulDiv(m_vidrect.bottom, m_size.cx, size.cx);
		m_vidrect.left   = MulDiv(m_vidrect.left,   m_size.cy, size.cy);
		m_vidrect.right  = MulDiv(m_vidrect.right,  m_size.cy, size.cy);
	}
	m_virtualTextureSize = m_size;

	return S_OK;
}

STDMETHODIMP CSubPicImpl::GetSize(SIZE* pSize)
{
	return pSize ? *pSize = m_size, S_OK : E_POINTER;
}


STDMETHODIMP CSubPicImpl::SetVirtualTextureSize (const SIZE pSize, const POINT pTopLeft)
{
	m_virtualTextureSize.SetSize(pSize.cx, pSize.cy);
	m_virtualTextureTopLeft.SetPoint(pTopLeft.x, pTopLeft.y);

	return S_OK;
}

STDMETHODIMP CSubPicImpl::SetType(SUBTITLE_TYPE subtitleType)
{
	m_eSubtitleType = subtitleType;

	return S_OK;
}

STDMETHODIMP CSubPicImpl::GetType(SUBTITLE_TYPE* pSubtitleType)
{
	CheckPointer(pSubtitleType, E_POINTER);

	*pSubtitleType = m_eSubtitleType;

	return S_OK;
}

STDMETHODIMP_(void) CSubPicImpl::SetInverseAlpha(bool bInverted)
{
	m_bInvAlpha = bInverted;
}

//
// ISubPicAllocatorImpl
//

CSubPicAllocatorImpl::CSubPicAllocatorImpl(SIZE cursize, bool fDynamicWriteOnly)
	: CUnknown(L"ISubPicAllocatorImpl", nullptr)
	, m_cursize(cursize)
	, m_curvidrect(CPoint(0, 0), m_cursize)
	, m_fDynamicWriteOnly(fDynamicWriteOnly)
{
}

STDMETHODIMP CSubPicAllocatorImpl::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	return
		QI(ISubPicAllocator)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicAllocator

STDMETHODIMP CSubPicAllocatorImpl::SetCurSize(SIZE cursize)
{
	m_cursize = cursize;
	return S_OK;
}

STDMETHODIMP CSubPicAllocatorImpl::SetCurVidRect(RECT curvidrect)
{
	m_curvidrect = curvidrect;
	return S_OK;
}

STDMETHODIMP CSubPicAllocatorImpl::GetStatic(ISubPic** ppSubPic)
{
	CheckPointer(ppSubPic, E_POINTER);

	{
		CAutoLock cAutoLock(&m_staticLock);

		CSize size(0, 0);
		if (m_pStatic && (FAILED(m_pStatic->GetSize(&size)) || size.cx != m_cursize.cx) || (size.cy != m_cursize.cy)) {
			m_pStatic.Release();
		}

		if (!m_pStatic) {
			if (!Alloc(true, &m_pStatic) || !m_pStatic) {
				return E_OUTOFMEMORY;
			}
		}

		*ppSubPic = m_pStatic;
	}

	(*ppSubPic)->AddRef();
	(*ppSubPic)->SetSize(m_cursize, m_curvidrect);

	return S_OK;
}

STDMETHODIMP CSubPicAllocatorImpl::AllocDynamic(ISubPic** ppSubPic)
{
	CheckPointer(ppSubPic, E_POINTER);

	if (!Alloc(false, ppSubPic) || !*ppSubPic) {
		return E_OUTOFMEMORY;
	}

	(*ppSubPic)->SetSize(m_cursize, m_curvidrect);

	return S_OK;
}

STDMETHODIMP_(bool) CSubPicAllocatorImpl::IsDynamicWriteOnly()
{
	return m_fDynamicWriteOnly;
}

STDMETHODIMP CSubPicAllocatorImpl::ChangeDevice(IUnknown* pDev)
{
	return Reset();
}

STDMETHODIMP CSubPicAllocatorImpl::Reset()
{
	CAutoLock cAutoLock(&m_staticLock);

	m_pStatic.Release();
	return S_OK;
}

STDMETHODIMP_(void) CSubPicAllocatorImpl::SetInverseAlpha(bool bInverted)
{
	m_bInvAlpha = bInverted;
}
