// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "DarkModeDialogBase.h"
#include "DarkModeButton.h"
#include "DarkModeHelper.h"
#include "MainResource.h"
#include <VSStyle.h>

using namespace DarkModeButton;

DarkModeDialogBase::DarkModeDialogBase(HINSTANCE hInstance, int iResource, HWND hParent,
	bool bResizable) :
	BaseDialog(hInstance, iResource, hParent, bResizable)
{
}

void DarkModeDialogBase::OnInitDialogBase()
{
	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return;
	}

	darkModeHelper.AllowDarkModeForWindow(m_hDlg, true);

	BOOL dark = TRUE;
	DarkModeHelper::WINDOWCOMPOSITIONATTRIBDATA compositionData = {
		DarkModeHelper::WCA_USEDARKMODECOLORS, &dark, sizeof(dark)
	};
	darkModeHelper.SetWindowCompositionAttribute(m_hDlg, &compositionData);

	AllowDarkModeForControls({ IDOK, IDCANCEL });

	m_windowSubclasses.push_back(std::make_unique<WindowSubclassWrapper>(m_hDlg,
		std::bind_front(&DarkModeDialogBase::DialogWndProc, this)));

	darkModeHelper.SetDarkModeForControl(m_tipWnd);
}

LRESULT CALLBACK DarkModeDialogBase::DialogWndProc(HWND hwnd, UINT msg, WPARAM wParam,
	LPARAM lParam)
{
	switch (msg)
	{
	case WM_NOTIFY:
		switch (reinterpret_cast<LPNMHDR>(lParam)->code)
		{
		case NM_CUSTOMDRAW:
			return OnCustomDraw(reinterpret_cast<NMCUSTOMDRAW *>(lParam));
		}
		break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT DarkModeDialogBase::OnCustomDraw(const NMCUSTOMDRAW *customDraw)
{
	bool isStoredCheckbox =
		(m_checkboxControlIds.count(static_cast<int>(customDraw->hdr.idFrom)) == 1);
	bool isStoredRadioButton =
		(m_radioButtonControlIds.count(static_cast<int>(customDraw->hdr.idFrom)) == 1);

	if (!isStoredCheckbox && !isStoredRadioButton)
	{
		return CDRF_DODEFAULT;
	}

	switch (customDraw->dwDrawStage)
	{
	case CDDS_PREPAINT:
		DrawButtonText(customDraw, isStoredCheckbox ? ButtonType::Checkbox : ButtonType::Radio);
		return CDRF_SKIPDEFAULT;
	}

	return CDRF_DODEFAULT;
}

void DarkModeDialogBase::AllowDarkModeForControls(const std::vector<int> &controlIds)
{
	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return;
	}

	for (int controlId : controlIds)
	{
		if (HWND control = GetDlgItem(m_hDlg, controlId))
		{
			darkModeHelper.SetDarkModeForControl(control);
		}
	}
}

void DarkModeDialogBase::AllowDarkModeForListView(int controlId)
{
	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return;
	}

	if (HWND control = GetDlgItem(m_hDlg, controlId))
	{
		darkModeHelper.SetListViewDarkModeColors(control);

		m_windowSubclasses.push_back(std::make_unique<WindowSubclassWrapper>(control,
			std::bind_front(&DarkModeDialogBase::ListViewWndProc, this)));
	}
}

LRESULT CALLBACK DarkModeDialogBase::ListViewWndProc(HWND hwnd, UINT msg, WPARAM wParam,
	LPARAM lParam)
{
	switch (msg)
	{
	case WM_NOTIFY:
		if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == ListView_GetHeader(hwnd))
		{
			switch (reinterpret_cast<LPNMHDR>(lParam)->code)
			{
			case NM_CUSTOMDRAW:
			{
				if (DarkModeHelper::GetInstance().IsDarkModeEnabled())
				{
					auto *customDraw = reinterpret_cast<NMCUSTOMDRAW *>(lParam);

					switch (customDraw->dwDrawStage)
					{
					case CDDS_PREPAINT:
						return CDRF_NOTIFYITEMDRAW;

					case CDDS_ITEMPREPAINT:
						SetTextColor(customDraw->hdc, DarkModeHelper::TEXT_COLOR);
						return CDRF_NEWFONT;
					}
				}
			}
			break;
			}
		}
		break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void DarkModeDialogBase::AllowDarkModeForCheckboxes(const std::vector<int> &controlIds)
{
	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return;
	}

	m_checkboxControlIds = std::unordered_set(controlIds.begin(), controlIds.end());
}

void DarkModeDialogBase::AllowDarkModeForRadioButtons(const std::vector<int> &controlIds)
{
	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return;
	}

	m_radioButtonControlIds = std::unordered_set(controlIds.begin(), controlIds.end());
}

void DarkModeDialogBase::AllowDarkModeForGroupBoxes(const std::vector<int> &controlIds)
{
	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return;
	}

	for (int controlId : controlIds)
	{
		if (HWND groupBox = GetDlgItem(m_hDlg, controlId))
		{
			m_darkModeGroupBoxes.push_back(std::make_unique<DarkModeGroupBox>(groupBox));
		}
	}
}

void DarkModeDialogBase::AllowDarkModeForComboBoxes(const std::vector<int> &controlIds)
{
	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return;
	}

	for (int controlId : controlIds)
	{
		if (HWND comboBox = GetDlgItem(m_hDlg, controlId))
		{
			darkModeHelper.SetDarkModeForComboBox(comboBox);
		}
	}
}

INT_PTR DarkModeDialogBase::OnCtlColorDlg(HWND hwnd, HDC hdc)
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(hdc);

	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return FALSE;
	}

	return reinterpret_cast<INT_PTR>(darkModeHelper.GetBackgroundBrush());
}

INT_PTR DarkModeDialogBase::OnCtlColorStatic(HWND hwnd, HDC hdc)
{
	auto defaultRes = OnCtlColor(hwnd, hdc);

	auto res = OnCtlColorStaticExtra(hwnd, hdc);

	if (res)
	{
		return res;
	}

	return defaultRes;
}

INT_PTR DarkModeDialogBase::OnCtlColorEdit(HWND hwnd, HDC hdc)
{
	auto defaultRes = OnCtlColor(hwnd, hdc);

	auto res = OnCtlColorEditExtra(hwnd, hdc);

	if (res)
	{
		return res;
	}

	return defaultRes;
}

INT_PTR DarkModeDialogBase::OnCtlColorListBox(HWND hwnd, HDC hdc)
{
	auto defaultRes = OnCtlColor(hwnd, hdc);

	auto res = OnCtlColorListBoxExtra(hwnd, hdc);

	if (res)
	{
		return res;
	}

	return defaultRes;
}

INT_PTR DarkModeDialogBase::OnCtlColor(HWND hwnd, HDC hdc)
{
	UNREFERENCED_PARAMETER(hwnd);

	auto &darkModeHelper = DarkModeHelper::GetInstance();

	if (!darkModeHelper.IsDarkModeEnabled())
	{
		return FALSE;
	}

	SetBkColor(hdc, DarkModeHelper::BACKGROUND_COLOR);
	SetTextColor(hdc, DarkModeHelper::TEXT_COLOR);

	return reinterpret_cast<INT_PTR>(darkModeHelper.GetBackgroundBrush());
}

INT_PTR DarkModeDialogBase::OnCtlColorStaticExtra(HWND hwnd, HDC hdc)
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(hdc);

	return FALSE;
}

INT_PTR DarkModeDialogBase::OnCtlColorEditExtra(HWND hwnd, HDC hdc)
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(hdc);

	return FALSE;
}

INT_PTR DarkModeDialogBase::OnCtlColorListBoxExtra(HWND hwnd, HDC hdc)
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(hdc);

	return FALSE;
}

int DarkModeDialogBase::GetGripperControlId()
{
	return IDC_GRIPPER;
}
