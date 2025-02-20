// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "ShellBrowser.h"
#include "Config.h"
#include "DarkModeHelper.h"
#include "ItemData.h"
#include "ListViewEdit.h"
#include "MainResource.h"
#include "ResourceHelper.h"
#include "SelectColumnsDialog.h"
#include "SetFileAttributesDialog.h"
#include "ShellNavigationController.h"
#include "../Helper/CachedIcons.h"
#include "../Helper/DragDropHelper.h"
#include "../Helper/Helper.h"
#include "../Helper/IconFetcher.h"
#include "../Helper/ListViewHelper.h"
#include "../Helper/ShellHelper.h"
#include <boost/format.hpp>
#include <wil/common.h>

const std::vector<ColumnType> COMMON_REAL_FOLDER_COLUMNS = { ColumnType::Name, ColumnType::Type,
	ColumnType::Size, ColumnType::DateModified, ColumnType::Authors, ColumnType::Title };

const std::vector<ColumnType> COMMON_CONTROL_PANEL_COLUMNS = { ColumnType::Name,
	ColumnType::VirtualComments };

const std::vector<ColumnType> COMMON_MY_COMPUTER_COLUMNS = { ColumnType::Name, ColumnType::Type,
	ColumnType::TotalSize, ColumnType::FreeSpace, ColumnType::VirtualComments,
	ColumnType::FileSystem };

const std::vector<ColumnType> COMMON_NETWORK_CONNECTIONS_COLUMNS = { ColumnType::Name,
	ColumnType::Type, ColumnType::NetworkAdaptorStatus, ColumnType::Owner };

const std::vector<ColumnType> COMMON_NETWORK_COLUMNS = { ColumnType::Name,
	ColumnType::VirtualComments };

const std::vector<ColumnType> COMMON_PRINTERS_COLUMNS = { ColumnType::Name,
	ColumnType::PrinterNumDocuments, ColumnType::PrinterStatus, ColumnType::PrinterComments,
	ColumnType::PrinterLocation };

const std::vector<ColumnType> COMMON_RECYCLE_BIN_COLUMNS = { ColumnType::Name,
	ColumnType::OriginalLocation, ColumnType::DateDeleted, ColumnType::Size, ColumnType::Type,
	ColumnType::DateModified };

std::vector<ColumnType> GetColumnHeaderMenuList(const std::wstring &directory);

LRESULT CALLBACK ShellBrowser::ListViewProcStub(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	UNREFERENCED_PARAMETER(uIdSubclass);

	auto *shellBrowser = reinterpret_cast<ShellBrowser *>(dwRefData);
	return shellBrowser->ListViewProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ShellBrowser::ListViewProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (m_getDragImageMessage != 0 && uMsg == m_getDragImageMessage)
	{
		// The listview control has built-in handling for this message (DI_GETDRAGIMAGE). It will,
		// by default, build an image based on the item being dragged. However, that's undesirable
		// here. When using SHDoDragDrop(), the drag image will be set up by that method. If the
		// listview is allowed to process the DI_GETDRAGIMAGE message, it will set the default
		// image. So, returning FALSE here allows SHDoDragDrop() to set up the image itself.
		return FALSE;
	}

	switch (uMsg)
	{
	case WM_MBUTTONDOWN:
	{
		POINT pt;
		POINTSTOPOINT(pt, MAKEPOINTS(lParam));
		OnListViewMButtonDown(&pt);
	}
	break;

	case WM_MBUTTONUP:
	{
		POINT pt;
		POINTSTOPOINT(pt, MAKEPOINTS(lParam));
		OnListViewMButtonUp(&pt, static_cast<UINT>(wParam));
	}
	break;

	// Note that the specific HANDLE_WM_RBUTTONDOWN message cracker is used here, rather than the
	// more generic message cracker HANDLE_MSG because it's important that the listview control
	// itself receive this message. Returning 0 would prevent that from happening.
	case WM_RBUTTONDOWN:
		HANDLE_WM_RBUTTONDOWN(hwnd, wParam, lParam, OnRButtonDown);
		break;

	case WM_CLIPBOARDUPDATE:
		OnClipboardUpdate();
		return 0;

	case WM_TIMER:
		if (wParam == PROCESS_SHELL_CHANGES_TIMER_ID)
		{
			OnProcessShellChangeNotifications();
		}
		break;

	case WM_NOTIFY:
		if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == ListView_GetHeader(m_hListView))
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

	case WM_APP_COLUMN_RESULT_READY:
		ProcessColumnResult(static_cast<int>(wParam));
		break;

	case WM_APP_THUMBNAIL_RESULT_READY:
		ProcessThumbnailResult(static_cast<int>(wParam));
		break;

	case WM_APP_INFO_TIP_READY:
		ProcessInfoTipResult(static_cast<int>(wParam));
		break;

	case WM_APP_SHELL_NOTIFY:
		OnShellNotify(wParam, lParam);
		break;
	}

	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ShellBrowser::ListViewParentProcStub(HWND hwnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	UNREFERENCED_PARAMETER(uIdSubclass);

	auto *shellBrowser = reinterpret_cast<ShellBrowser *>(dwRefData);
	return shellBrowser->ListViewParentProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ShellBrowser::ListViewParentProc(HWND hwnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_NOTIFY:
		if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == m_hListView)
		{
			switch (reinterpret_cast<LPNMHDR>(lParam)->code)
			{
			case LVN_BEGINDRAG:
				OnListViewBeginDrag(reinterpret_cast<NMLISTVIEW *>(lParam));
				break;

			case LVN_BEGINRDRAG:
				OnListViewBeginRightClickDrag(reinterpret_cast<NMLISTVIEW *>(lParam));
				break;

			case LVN_GETDISPINFO:
				OnListViewGetDisplayInfo(lParam);
				break;

			case LVN_GETINFOTIP:
				return OnListViewGetInfoTip(reinterpret_cast<NMLVGETINFOTIP *>(lParam));

			case LVN_INSERTITEM:
				OnListViewItemInserted(reinterpret_cast<NMLISTVIEW *>(lParam));
				break;

			case LVN_ITEMCHANGED:
				OnListViewItemChanged(reinterpret_cast<NMLISTVIEW *>(lParam));
				break;

			case LVN_KEYDOWN:
				OnListViewKeyDown(reinterpret_cast<NMLVKEYDOWN *>(lParam));
				break;

			case LVN_COLUMNCLICK:
				ColumnClicked(reinterpret_cast<NMLISTVIEW *>(lParam)->iSubItem);
				break;

			case LVN_BEGINLABELEDIT:
				return OnListViewBeginLabelEdit(reinterpret_cast<NMLVDISPINFO *>(lParam));

			case LVN_ENDLABELEDIT:
				return OnListViewEndLabelEdit(reinterpret_cast<NMLVDISPINFO *>(lParam));

			case LVN_DELETEALLITEMS:
				// Respond to the notification in order to speed up calls to ListView_DeleteAllItems
				// per http://www.verycomputer.com/5_0c959e6a4fd713e2_1.htm
				return TRUE;
			}
		}
		else if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == ListView_GetHeader(m_hListView))
		{
			switch (reinterpret_cast<LPNMHDR>(lParam)->code)
			{
			case NM_RCLICK:
			{
				DWORD messagePos = GetMessagePos();
				OnListViewHeaderRightClick(MAKEPOINTS(messagePos));
			}
			break;
			}
		}
		break;
	}

	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void ShellBrowser::OnListViewMButtonDown(const POINT *pt)
{
	LV_HITTESTINFO ht;
	ht.pt = *pt;
	ListView_HitTest(m_hListView, &ht);

	if (ht.flags != LVHT_NOWHERE && ht.iItem != -1)
	{
		m_middleButtonItem = ht.iItem;

		ListView_SetItemState(m_hListView, ht.iItem, LVIS_FOCUSED, LVIS_FOCUSED);
	}
	else
	{
		m_middleButtonItem = -1;
	}
}

void ShellBrowser::OnListViewMButtonUp(const POINT *pt, UINT keysDown)
{
	LV_HITTESTINFO ht;
	ht.pt = *pt;
	ListView_HitTest(m_hListView, &ht);

	if (ht.flags == LVHT_NOWHERE)
	{
		return;
	}

	// Only open an item if it was the one on which the middle mouse button was
	// initially clicked on.
	if (ht.iItem != m_middleButtonItem)
	{
		return;
	}

	const ItemInfo_t &itemInfo = GetItemByIndex(m_middleButtonItem);

	if (!WI_IsAnyFlagSet(itemInfo.wfd.dwFileAttributes,
			FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE))
	{
		return;
	}

	bool switchToNewTab = m_config->openTabsInForeground;

	if (WI_IsFlagSet(keysDown, MK_SHIFT))
	{
		switchToNewTab = !switchToNewTab;
	}

	m_tabNavigation->CreateNewTab(itemInfo.pidlComplete.get(), switchToNewTab);
}

void ShellBrowser::OnRButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags)
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(doubleClick);

	// If shift is held down while right-clicking an item, it appears the listview control won't
	// select the item. Which is why the functionality is implemented here.
	if (WI_IsFlagSet(keyFlags, MK_SHIFT))
	{
		LVHITTESTINFO hitTestInfo = {};
		hitTestInfo.pt = { x, y };
		int itemAtPoint = ListView_HitTest(m_hListView, &hitTestInfo);

		if (itemAtPoint != -1
			&& ListView_GetItemState(m_hListView, itemAtPoint, LVIS_SELECTED) != LVIS_SELECTED)
		{
			ListViewHelper::SelectAllItems(m_hListView, FALSE);
			ListViewHelper::FocusItem(m_hListView, itemAtPoint, TRUE);
			ListViewHelper::SelectItem(m_hListView, itemAtPoint, TRUE);
		}
	}
}

void ShellBrowser::OnListViewGetDisplayInfo(LPARAM lParam)
{
	NMLVDISPINFO *pnmv = nullptr;
	LVITEM *plvItem = nullptr;

	pnmv = (NMLVDISPINFO *) lParam;
	plvItem = &pnmv->item;

	int internalIndex = static_cast<int>(plvItem->lParam);

	/* Construct an image here using the items
	actual icon. This image will be shown initially.
	If the item also has a thumbnail image, this
	will be found later, and will overwrite any
	image settings made here.
	Note that the initial icon image MUST be drawn
	first, or else it may be possible for the
	thumbnail to be drawn before the initial
	image. */
	if (m_folderSettings.viewMode == +ViewMode::Thumbnails
		&& (plvItem->mask & LVIF_IMAGE) == LVIF_IMAGE)
	{
		const ItemInfo_t &itemInfo = m_itemInfoMap.at(internalIndex);
		auto cachedThumbnailIndex = GetCachedThumbnailIndex(itemInfo);

		if (cachedThumbnailIndex)
		{
			plvItem->iImage = *cachedThumbnailIndex;
		}
		else
		{
			plvItem->iImage = GetIconThumbnail(internalIndex);
		}

		plvItem->mask |= LVIF_DI_SETITEM;

		QueueThumbnailTask(internalIndex);

		return;
	}

	if (m_folderSettings.viewMode == +ViewMode::Details && (plvItem->mask & LVIF_TEXT) == LVIF_TEXT)
	{
		auto columnType = GetColumnTypeByIndex(plvItem->iSubItem);
		assert(columnType);

		QueueColumnTask(internalIndex, *columnType);
	}

	if ((plvItem->mask & LVIF_IMAGE) == LVIF_IMAGE)
	{
		const ItemInfo_t &itemInfo = m_itemInfoMap.at(internalIndex);
		auto cachedIconIndex = GetCachedIconIndex(itemInfo);

		if (cachedIconIndex)
		{
			// The icon retrieval method specifies the
			// SHGFI_OVERLAYINDEX value. That means that cached icons
			// will have an overlay index stored in the upper eight bits
			// of the icon value. While setting the icon and
			// stateMask/state values in one go with ListView_SetItem()
			// works, there's no direct way to specify the
			// stateMask/state values here.
			// If you don't mask out the upper eight bits here, no icon
			// will be shown. You can call ListView_SetItem() at this
			// point, but that seemingly doesn't repaint the item
			// correctly (you have to call ListView_Update() to force
			// the item to be redrawn).
			// Rather than doing that, only the icon is set here. Any
			// overlay will be added by the icon retrieval task
			// (scheduled below).
			plvItem->iImage = (*cachedIconIndex & 0x0FFF);
		}
		else
		{
			if ((itemInfo.wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				== FILE_ATTRIBUTE_DIRECTORY)
			{
				plvItem->iImage = m_iFolderIcon;
			}
			else
			{
				plvItem->iImage = m_iFileIcon;
			}
		}

		m_iconFetcher->QueueIconTask(itemInfo.pidlComplete.get(),
			[this, internalIndex](int iconIndex)
			{
				ProcessIconResult(internalIndex, iconIndex);
			});
	}

	plvItem->mask |= LVIF_DI_SETITEM;
}

std::optional<int> ShellBrowser::GetCachedIconIndex(const ItemInfo_t &itemInfo)
{
	auto cachedItr = m_cachedIcons->findByPath(itemInfo.parsingName);

	if (cachedItr == m_cachedIcons->end())
	{
		return std::nullopt;
	}

	return cachedItr->iconIndex;
}

void ShellBrowser::ProcessIconResult(int internalIndex, int iconIndex)
{
	auto index = LocateItemByInternalIndex(internalIndex);

	if (!index)
	{
		return;
	}

	LVITEM lvItem;
	lvItem.mask = LVIF_IMAGE | LVIF_STATE;
	lvItem.iItem = *index;
	lvItem.iSubItem = 0;
	lvItem.iImage = iconIndex;
	lvItem.stateMask = LVIS_OVERLAYMASK;
	lvItem.state = INDEXTOOVERLAYMASK(iconIndex >> 24);
	ListView_SetItem(m_hListView, &lvItem);
}

LRESULT ShellBrowser::OnListViewGetInfoTip(NMLVGETINFOTIP *getInfoTip)
{
	if (m_config->showInfoTips)
	{
		int internalIndex = GetItemInternalIndex(getInfoTip->iItem);
		QueueInfoTipTask(internalIndex, getInfoTip->pszText);
	}

	StringCchCopy(getInfoTip->pszText, getInfoTip->cchTextMax, EMPTY_STRING);

	return 0;
}

void ShellBrowser::QueueInfoTipTask(int internalIndex, const std::wstring &existingInfoTip)
{
	int infoTipResultId = m_infoTipResultIDCounter++;

	BasicItemInfo_t basicItemInfo = getBasicItemInfo(internalIndex);
	Config configCopy = *m_config;
	bool virtualFolder = InVirtualFolder();

	auto result = m_infoTipsThreadPool.push(
		[this, infoTipResultId, internalIndex, basicItemInfo, configCopy, virtualFolder,
			existingInfoTip](int id)
		{
			UNREFERENCED_PARAMETER(id);

			auto result = GetInfoTipAsync(m_hListView, infoTipResultId, internalIndex,
				basicItemInfo, configCopy, m_hResourceModule, virtualFolder);

			// If the item name is truncated in the listview,
			// existingInfoTip will contain that value. Therefore, it's
			// important that the rest of the infotip is concatenated onto
			// that value if it's there.
			if (result && !existingInfoTip.empty())
			{
				result->infoTip = existingInfoTip + L"\n" + result->infoTip;
			}

			return result;
		});

	m_infoTipResults.insert({ infoTipResultId, std::move(result) });
}

std::optional<ShellBrowser::InfoTipResult> ShellBrowser::GetInfoTipAsync(HWND listView,
	int infoTipResultId, int internalIndex, const BasicItemInfo_t &basicItemInfo,
	const Config &config, HINSTANCE instance, bool virtualFolder)
{
	std::wstring infoTip;

	/* Use Explorer infotips if the option is selected, or this is a
	virtual folder. Otherwise, show the modified date. */
	if ((config.infoTipType == InfoTipType::System) || virtualFolder)
	{
		std::wstring infoTipText;
		HRESULT hr = GetItemInfoTip(basicItemInfo.pidlComplete.get(), infoTipText);

		if (FAILED(hr))
		{
			return std::nullopt;
		}

		infoTip = infoTipText;
	}
	else
	{
		TCHAR dateModified[64];
		LoadString(instance, IDS_GENERAL_DATEMODIFIED, dateModified, SIZEOF_ARRAY(dateModified));

		TCHAR fileModificationText[256];
		BOOL fileTimeResult =
			CreateFileTimeString(&basicItemInfo.wfd.ftLastWriteTime, fileModificationText,
				SIZEOF_ARRAY(fileModificationText), config.globalFolderSettings.showFriendlyDates);

		if (!fileTimeResult)
		{
			return std::nullopt;
		}

		infoTip = str(boost::wformat(_T("%s: %s")) % dateModified % fileModificationText);
	}

	PostMessage(listView, WM_APP_INFO_TIP_READY, infoTipResultId, 0);

	InfoTipResult result;
	result.itemInternalIndex = internalIndex;
	result.infoTip = infoTip;

	return result;
}

void ShellBrowser::ProcessInfoTipResult(int infoTipResultId)
{
	auto itr = m_infoTipResults.find(infoTipResultId);

	if (itr == m_infoTipResults.end())
	{
		return;
	}

	auto result = itr->second.get();
	m_infoTipResults.erase(itr);

	if (!result)
	{
		return;
	}

	auto index = LocateItemByInternalIndex(result->itemInternalIndex);

	if (!index)
	{
		return;
	}

	TCHAR infoTipText[256];
	StringCchCopy(infoTipText, SIZEOF_ARRAY(infoTipText), result->infoTip.c_str());

	LVSETINFOTIP infoTip;
	infoTip.cbSize = sizeof(infoTip);
	infoTip.dwFlags = 0;
	infoTip.iItem = *index;
	infoTip.iSubItem = 0;
	infoTip.pszText = infoTipText;
	ListView_SetInfoTip(m_hListView, &infoTip);
}

void ShellBrowser::OnListViewItemInserted(const NMLISTVIEW *itemData)
{
	if (m_folderSettings.showInGroups)
	{
		auto groupId = GetItemGroupId(itemData->iItem);

		if (groupId)
		{
			OnItemAddedToGroup(*groupId);
		}
	}
}

void ShellBrowser::OnListViewItemChanged(const NMLISTVIEW *changeData)
{
	if (changeData->uChanged != LVIF_STATE)
	{
		return;
	}

	if (m_config->checkBoxSelection && (LVIS_STATEIMAGEMASK & changeData->uNewState) != 0)
	{
		bool checked = ((changeData->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
		ListViewHelper::SelectItem(m_hListView, changeData->iItem, checked);
	}

	bool previouslySelected = WI_IsFlagSet(changeData->uOldState, LVIS_SELECTED);
	bool currentlySelected = WI_IsFlagSet(changeData->uNewState, LVIS_SELECTED);

	if (previouslySelected == currentlySelected)
	{
		return;
	}

	if (m_config->checkBoxSelection)
	{
		if (!previouslySelected && currentlySelected)
		{
			ListView_SetCheckState(m_hListView, changeData->iItem, TRUE);
		}
		else if (previouslySelected && !currentlySelected)
		{
			ListView_SetCheckState(m_hListView, changeData->iItem, FALSE);
		}
	}

	UpdateFileSelectionInfo(static_cast<int>(changeData->lParam), currentlySelected);

	listViewSelectionChanged.m_signal();
}

void ShellBrowser::UpdateFileSelectionInfo(int internalIndex, BOOL selected)
{
	ULARGE_INTEGER ulFileSize;
	BOOL isFolder;

	isFolder = (m_itemInfoMap.at(internalIndex).wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		== FILE_ATTRIBUTE_DIRECTORY;

	ulFileSize.LowPart = m_itemInfoMap.at(internalIndex).wfd.nFileSizeLow;
	ulFileSize.HighPart = m_itemInfoMap.at(internalIndex).wfd.nFileSizeHigh;

	if (selected)
	{
		if (isFolder)
		{
			m_directoryState.numFoldersSelected++;
		}
		else
		{
			m_directoryState.numFilesSelected++;
		}

		m_directoryState.fileSelectionSize.QuadPart += ulFileSize.QuadPart;
	}
	else
	{
		if (isFolder)
		{
			m_directoryState.numFoldersSelected--;
		}
		else
		{
			m_directoryState.numFilesSelected--;
		}

		m_directoryState.fileSelectionSize.QuadPart -= ulFileSize.QuadPart;
	}
}

void ShellBrowser::OnListViewKeyDown(const NMLVKEYDOWN *lvKeyDown)
{
	switch (lvKeyDown->wVKey)
	{
	case 'A':
		if (IsKeyDown(VK_CONTROL) && !IsKeyDown(VK_SHIFT) && !IsKeyDown(VK_MENU))
		{
			ListViewHelper::SelectAllItems(m_hListView, TRUE);
			SetFocus(m_hListView);
		}
		break;

	case 'C':
		if (IsKeyDown(VK_CONTROL) && !IsKeyDown(VK_SHIFT) && !IsKeyDown(VK_MENU))
		{
			CopySelectedItemsToClipboard(true);
		}
		break;

	case 'I':
		if (IsKeyDown(VK_CONTROL) && !IsKeyDown(VK_SHIFT) && !IsKeyDown(VK_MENU))
		{
			ListViewHelper::InvertSelection(m_hListView);
			SetFocus(m_hListView);
		}
		break;

	case 'X':
		if (IsKeyDown(VK_CONTROL) && !IsKeyDown(VK_SHIFT) && !IsKeyDown(VK_MENU))
		{
			CopySelectedItemsToClipboard(false);
		}
		break;

	case VK_BACK:
		if (IsKeyDown(VK_CONTROL) && !IsKeyDown(VK_SHIFT) && !IsKeyDown(VK_MENU))
		{
			TCHAR root[MAX_PATH];
			HRESULT hr =
				StringCchCopy(root, SIZEOF_ARRAY(root), m_directoryState.directory.c_str());

			if (SUCCEEDED(hr))
			{
				BOOL bRes = PathStripToRoot(root);

				if (bRes)
				{
					m_navigationController->BrowseFolder(root);
				}
			}
		}
		else
		{
			m_navigationController->GoUp();
		}
		break;

	case VK_DELETE:
		if (IsKeyDown(VK_SHIFT))
		{
			DeleteSelectedItems(true);
		}
		else
		{
			DeleteSelectedItems(false);
		}
		break;
	}
}

const ShellBrowser::ItemInfo_t &ShellBrowser::GetItemByIndex(int index) const
{
	int internalIndex = GetItemInternalIndex(index);
	return m_itemInfoMap.at(internalIndex);
}

ShellBrowser::ItemInfo_t &ShellBrowser::GetItemByIndex(int index)
{
	int internalIndex = GetItemInternalIndex(index);
	return m_itemInfoMap.at(internalIndex);
}

int ShellBrowser::GetItemInternalIndex(int item) const
{
	LVITEM lvItem;
	lvItem.mask = LVIF_PARAM;
	lvItem.iItem = item;
	lvItem.iSubItem = 0;
	BOOL res = ListView_GetItem(m_hListView, &lvItem);

	if (!res)
	{
		throw std::runtime_error("Item lookup failed");
	}

	return static_cast<int>(lvItem.lParam);
}

void ShellBrowser::MarkItemAsCut(int item, bool cut)
{
	const auto &itemInfo = GetItemByIndex(item);

	// If the file is hidden, prevent changes to its visibility state.
	if (WI_IsFlagSet(itemInfo.wfd.dwFileAttributes, FILE_ATTRIBUTE_HIDDEN))
	{
		return;
	}

	if (cut)
	{
		ListView_SetItemState(m_hListView, item, LVIS_CUT, LVIS_CUT);
	}
	else
	{
		ListView_SetItemState(m_hListView, item, 0, LVIS_CUT);
	}
}

void ShellBrowser::ShowPropertiesForSelectedFiles() const
{
	std::vector<unique_pidl_child> pidls;
	std::vector<PCITEMID_CHILD> rawPidls;

	int item = -1;

	while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1)
	{
		auto pidl = GetItemChildIdl(item);

		rawPidls.push_back(pidl.get());
		pidls.push_back(std::move(pidl));
	}

	auto pidlDirectory = GetDirectoryIdl();
	ShowMultipleFileProperties(pidlDirectory.get(), rawPidls, m_hOwner);
}

void ShellBrowser::OnListViewHeaderRightClick(const POINTS &cursorPos)
{
	wil::unique_hmenu headerPopupMenu(
		LoadMenu(m_hResourceModule, MAKEINTRESOURCE(IDR_HEADER_MENU)));
	HMENU headerMenu = GetSubMenu(headerPopupMenu.get(), 0);

	auto commonColumns = GetColumnHeaderMenuList(m_directoryState.directory.c_str());

	std::unordered_map<int, ColumnType> menuItemMappings;
	int totalInserted = 0;
	int commonColumnPosition = 0;

	for (const auto &column : *m_pActiveColumns)
	{
		auto itr = std::find(commonColumns.begin(), commonColumns.end(), column.type);
		bool inCommonColumns = (itr != commonColumns.end());

		if (!column.bChecked && !inCommonColumns)
		{
			continue;
		}

		MENUITEMINFO mii;
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_STRING | MIIM_STATE | MIIM_ID;

		std::wstring columnText =
			ResourceHelper::LoadString(m_hResourceModule, LookupColumnNameStringIndex(column.type));

		if (column.bChecked)
		{
			mii.fState = MFS_CHECKED;
		}
		else
		{
			mii.fState = MFS_ENABLED;
		}

		int currentPosition;

		if (inCommonColumns)
		{
			// The common columns always appear first, whether they're checked
			// or not.
			currentPosition = commonColumnPosition;
			commonColumnPosition++;
		}
		else
		{
			currentPosition = totalInserted;
		}

		int id = totalInserted + 1;

		mii.dwTypeData = columnText.data();
		mii.wID = id;
		InsertMenuItem(headerMenu, currentPosition, TRUE, &mii);

		menuItemMappings.insert({ id, column.type });

		totalInserted++;
	}

	int cmd =
		TrackPopupMenu(headerMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_VERTICAL | TPM_RETURNCMD,
			cursorPos.x, cursorPos.y, 0, m_hListView, nullptr);

	if (cmd == 0)
	{
		return;
	}

	OnListViewHeaderMenuItemSelected(cmd, menuItemMappings);
}

std::vector<ColumnType> GetColumnHeaderMenuList(const std::wstring &directory)
{
	if (CompareVirtualFolders(directory.c_str(), CSIDL_DRIVES))
	{
		return COMMON_MY_COMPUTER_COLUMNS;
	}
	else if (CompareVirtualFolders(directory.c_str(), CSIDL_CONTROLS))
	{
		return COMMON_CONTROL_PANEL_COLUMNS;
	}
	else if (CompareVirtualFolders(directory.c_str(), CSIDL_BITBUCKET))
	{
		return COMMON_RECYCLE_BIN_COLUMNS;
	}
	else if (CompareVirtualFolders(directory.c_str(), CSIDL_CONNECTIONS))
	{
		return COMMON_NETWORK_CONNECTIONS_COLUMNS;
	}
	else if (CompareVirtualFolders(directory.c_str(), CSIDL_NETWORK))
	{
		return COMMON_NETWORK_COLUMNS;
	}
	else if (CompareVirtualFolders(directory.c_str(), CSIDL_PRINTERS))
	{
		return COMMON_PRINTERS_COLUMNS;
	}
	else
	{
		return COMMON_REAL_FOLDER_COLUMNS;
	}
}

void ShellBrowser::OnListViewHeaderMenuItemSelected(int menuItemId,
	const std::unordered_map<int, ColumnType> &menuItemMappings)
{
	if (menuItemId == IDM_HEADER_MORE)
	{
		OnShowMoreColumnsSelected();
	}
	else
	{
		OnColumnMenuItemSelected(menuItemId, menuItemMappings);
	}
}

void ShellBrowser::OnShowMoreColumnsSelected()
{
	SelectColumnsDialog selectColumnsDialog(m_hResourceModule, m_hListView, this,
		m_iconResourceLoader);
	selectColumnsDialog.ShowModalDialog();
}

void ShellBrowser::OnColumnMenuItemSelected(int menuItemId,
	const std::unordered_map<int, ColumnType> &menuItemMappings)
{
	auto currentColumns = GetCurrentColumns();

	ColumnType columnType = menuItemMappings.at(menuItemId);
	auto itr = std::find_if(currentColumns.begin(), currentColumns.end(),
		[columnType](const Column_t &column)
		{
			return column.type == columnType;
		});

	if (itr == currentColumns.end())
	{
		return;
	}

	itr->bChecked = !itr->bChecked;

	SetCurrentColumns(currentColumns);

	// If it was the first column that was changed, need to refresh all columns.
	if (menuItemId == 1)
	{
		m_navigationController->Refresh();
	}
}

void ShellBrowser::SetFileAttributesForSelection()
{
	std::list<NSetFileAttributesDialogExternal::SetFileAttributesInfo> sfaiList;
	int index = -1;

	while ((index = ListView_GetNextItem(m_hListView, index, LVNI_SELECTED)) != -1)
	{
		NSetFileAttributesDialogExternal::SetFileAttributesInfo sfai;

		const ItemInfo_t &item = GetItemByIndex(index);
		sfai.wfd = item.wfd;
		StringCchCopy(sfai.szFullFileName, SIZEOF_ARRAY(sfai.szFullFileName),
			item.parsingName.c_str());

		sfaiList.push_back(sfai);
	}

	SetFileAttributesDialog setFileAttributesDialog(m_hResourceModule, m_hListView, sfaiList);
	setFileAttributesDialog.ShowModalDialog();
}

bool ShellBrowser::TestListViewItemAttributes(int item, SFGAOF attributes) const
{
	SFGAOF commonAttributes = attributes;
	HRESULT hr = GetListViewItemAttributes(item, &commonAttributes);

	if (SUCCEEDED(hr))
	{
		return (commonAttributes & attributes) == attributes;
	}

	return false;
}

HRESULT ShellBrowser::GetListViewSelectionAttributes(SFGAOF *attributes) const
{
	HRESULT hr = E_FAIL;

	/* TODO: This should probably check all selected files. */
	int selectedItem = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);

	if (selectedItem != -1)
	{
		hr = GetListViewItemAttributes(selectedItem, attributes);
	}

	return hr;
}

HRESULT ShellBrowser::GetListViewItemAttributes(int item, SFGAOF *attributes) const
{
	const auto &itemInfo = GetItemByIndex(item);
	return GetItemAttributes(itemInfo.pidlComplete.get(), attributes);
}

std::vector<PCIDLIST_ABSOLUTE> ShellBrowser::GetSelectedItemPidls()
{
	std::vector<PCIDLIST_ABSOLUTE> selectedItemPidls;
	int index = -1;

	while ((index = ListView_GetNextItem(m_hListView, index, LVNI_SELECTED)) != -1)
	{
		const auto &item = GetItemByIndex(index);
		selectedItemPidls.push_back(item.pidlComplete.get());
	}

	return selectedItemPidls;
}

void ShellBrowser::OnListViewBeginDrag(const NMLISTVIEW *info)
{
	StartDrag(info->iItem, info->ptAction);
}

void ShellBrowser::OnListViewBeginRightClickDrag(const NMLISTVIEW *info)
{
	StartDrag(info->iItem, info->ptAction);
}

HRESULT ShellBrowser::StartDrag(int draggedItem, const POINT &startPoint)
{
	std::vector<PCIDLIST_ABSOLUTE> pidls = GetSelectedItemPidls();

	if (pidls.empty())
	{
		return E_UNEXPECTED;
	}

	wil::com_ptr_nothrow<IDataObject> dataObject;
	RETURN_IF_FAILED(CreateDataObjectForShellTransfer(pidls, &dataObject));

	m_performingDrag = true;
	m_draggedDataObject = dataObject.get();
	m_draggedItems = DeepCopyPidls(pidls);

	POINT ptItem;
	ListView_GetItemPosition(m_hListView, draggedItem, &ptItem);

	POINT ptOrigin;
	ListView_GetOrigin(m_hListView, &ptOrigin);

	m_ptDraggedOffset.x = ptOrigin.x + startPoint.x - ptItem.x;
	m_ptDraggedOffset.y = ptOrigin.y + startPoint.y - ptItem.y;

	DWORD finalEffect;
	HRESULT hr = SHDoDragDrop(m_hListView, dataObject.get(), nullptr,
		DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &finalEffect);

	m_draggedItems.clear();
	m_draggedDataObject = nullptr;
	m_performingDrag = false;

	return hr;
}

void ShellBrowser::AutoSizeColumns()
{
	if (m_folderSettings.viewMode != +ViewMode::Details)
	{
		return;
	}

	HWND header = ListView_GetHeader(m_hListView);
	int numColumns = Header_GetItemCount(header);

	if (numColumns == -1)
	{
		return;
	}

	for (int i = 0; i < numColumns; i++)
	{
		ListView_SetColumnWidth(m_hListView, i, LVSCW_AUTOSIZE);
	}
}

BOOL ShellBrowser::OnListViewBeginLabelEdit(const NMLVDISPINFO *dispInfo)
{
	const auto &item = GetItemByIndex(dispInfo->item.iItem);

	SFGAOF attributes = SFGAO_CANRENAME;
	HRESULT hr = GetItemAttributes(item.pidlComplete.get(), &attributes);

	if (FAILED(hr) || WI_IsFlagClear(attributes, SFGAO_CANRENAME))
	{
		return TRUE;
	}

	bool useEditingName = true;

	// The editing name may differ from the display name. For example, the display name of the C:\
	// drive item will be something like "Local Disk (C:)", while its editing name will be "Local
	// Disk". Since the editing name is affected by the file name extensions setting in Explorer, it
	// won't be used if:
	//
	// - Extensions are hidden in Explorer, but shown in Explorer++ (since the editing name would
	//   contain no extension)
	// - Extensions are shown in Explorer, but hidden in Explorer++ (since the editing name would
	//   contain an extension). Note that this case is handled when editing is finished - if
	//   extensions are hidden, the extension will be manually re-added when renaming an item.
	if (!WI_IsFlagSet(item.wfd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
	{
		std::wstring displayName = GetItemDisplayName(dispInfo->item.iItem);

		if (m_config->globalFolderSettings.showExtensions
			|| m_config->globalFolderSettings.hideLinkExtension)
		{
			auto *extension = PathFindExtension(displayName.c_str());

			if (*extension != '\0'
				&& lstrcmp((item.editingName + extension).c_str(), displayName.c_str()) == 0)
			{
				useEditingName = false;
			}
		}
		else
		{
			auto *extension = PathFindExtension(item.editingName.c_str());

			if (*extension != '\0'
				&& lstrcmp((displayName + extension).c_str(), item.editingName.c_str()) == 0)
			{
				useEditingName = false;
			}
		}
	}

	HWND editControl = ListView_GetEditControl(m_hListView);

	if (editControl == nullptr)
	{
		return TRUE;
	}

	// Note that the necessary text is set in the edit control, rather than the listview. This is
	// for the following two reasons:
	//
	// 1. Setting the listview item text after the edit control has already been created won't
	// change the text in the control
	// 2. Even if setting the listview item text did change the edit control text, the text would
	// need to be reverted if the user canceled editing. Setting the edit control text means there's
	// nothing that needs to be changed if editing is canceled.
	if (useEditingName)
	{
		SetWindowText(editControl, item.editingName.c_str());
	}

	ListViewEdit::CreateNew(editControl, m_acceleratorTable,
		WI_IsFlagClear(item.wfd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY));

	return FALSE;
}

BOOL ShellBrowser::OnListViewEndLabelEdit(const NMLVDISPINFO *dispInfo)
{
	// Did the user cancel editing?
	if (dispInfo->item.pszText == nullptr)
	{
		return FALSE;
	}

	std::wstring newFilename = dispInfo->item.pszText;

	if (newFilename.empty())
	{
		return FALSE;
	}

	const auto &item = GetItemByIndex(dispInfo->item.iItem);

	if (newFilename == item.editingName)
	{
		return FALSE;
	}

	if (!WI_IsFlagSet(item.wfd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
	{
		auto *extension = PathFindExtension(item.wfd.cFileName);

		bool extensionHidden = !m_config->globalFolderSettings.showExtensions
			|| (m_config->globalFolderSettings.hideLinkExtension
				&& lstrcmpi(extension, _T(".lnk")) == 0);

		// If file extensions are turned off, the new filename will be incorrect (i.e. it will be
		// missing the extension). Therefore, append the extension manually if it is turned off.
		if (extensionHidden && *extension != '\0')
		{
			newFilename += extension;
		}
	}

	wil::com_ptr_nothrow<IShellFolder> parent;
	PCITEMID_CHILD child;
	HRESULT hr = SHBindToParent(item.pidlComplete.get(), IID_PPV_ARGS(&parent), &child);

	if (FAILED(hr))
	{
		return FALSE;
	}

	SHGDNF flags = SHGDN_INFOLDER;

	// As with GetDisplayNameOf(), the behavior of SetNameOf() is influenced by whether or not file
	// extensions are displayed in Explorer. If extensions are displayed and the SHGDN_INFOLDER name
	// is set, then the name should contain an extension. On the other hand, if extensions aren't
	// displayed and the SHGDN_INFOLDER name is set, then the name shouldn't contain an extension.
	// Given that extensions can be independently hidden and shown in Explorer++, this behavior is
	// undesirable and incompatible.
	// For example, if extensions are hidden in Explorer, but shown in Explorer++, then it wouldn't
	// be possible to change a file's extension. When setting the SHGDN_INFOLDER name, the original
	// extension would always be re-added by the shell.
	// Therefore, if a file is being edited, the parsing name (which will always contain an
	// extension) will be updated.
	if (!m_directoryState.virtualFolder
		&& !WI_IsFlagSet(item.wfd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
	{
		flags |= SHGDN_FORPARSING;
	}

	unique_pidl_child newChild;
	hr =
		parent->SetNameOf(m_hListView, child, newFilename.c_str(), flags, wil::out_param(newChild));

	if (FAILED(hr))
	{
		return FALSE;
	}

	hr = parent->CompareIDs(0, child, newChild.get());

	// It's possible for the rename operation to succeed, but for the item name to remain unchanged.
	// For example, if one or more '.' characters are appended to the end of the item name, the
	// rename operation will succeed, but the name won't actually change. In those sorts of cases,
	// the name the user entered should be removed.
	if (HRESULT_CODE(hr) == 0)
	{
		return FALSE;
	}

	// When an item is changed in any way, a notification will be sent. However, that notification
	// isn't going to be received immediately. In the case where the user has renamed an item, that
	// creates a period of time where the updated name is shown, but the item still internally
	// refers to the original name. That then means that attempting to opening the item (or interact
	// with it more generally) will fail, since the item no longer exists with the original name.
	// Performing an immediate update here means that the user can continue to interact with the
	// item, without having to wait for the rename notification to be processed.
	unique_pidl_absolute pidlNew(ILCombine(m_directoryState.pidlDirectory.get(), newChild.get()));
	UpdateItem(item.pidlComplete.get(), pidlNew.get());

	// The text will be set by UpdateItem. It's not safe to return true here, since items can sorted
	// by UpdateItem, which can result in the index of this item being changed.
	return FALSE;
}
