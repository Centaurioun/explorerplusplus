// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "Bookmarks/UI/BookmarkListView.h"
#include "DarkModeDialogBase.h"
#include "ResourceHelper.h"
#include "../Helper/DialogSettings.h"
#include "../Helper/ResizableDialog.h"
#include <boost/signals2.hpp>
#include <unordered_set>

class BookmarkNavigationController;
class BookmarkTree;
class BookmarkTreeView;
class CoreInterface;
class IconFetcher;
class ManageBookmarksDialog;
class Navigator;
class WindowSubclassWrapper;

class ManageBookmarksDialogPersistentSettings : public DialogSettings
{
public:
	static ManageBookmarksDialogPersistentSettings &GetInstance();

private:
	friend ManageBookmarksDialog;

	static const TCHAR SETTINGS_KEY[];
	static const int DEFAULT_MANAGE_BOOKMARKS_COLUMN_WIDTH = 180;

	ManageBookmarksDialogPersistentSettings();

	ManageBookmarksDialogPersistentSettings(const ManageBookmarksDialogPersistentSettings &);
	ManageBookmarksDialogPersistentSettings &operator=(
		const ManageBookmarksDialogPersistentSettings &);

	void SetupDefaultColumns();

	std::vector<BookmarkListView::Column> m_listViewColumns;

	bool m_bInitialized;
	std::unordered_set<std::wstring> m_setExpansion;
};

class ManageBookmarksDialog : public DarkModeDialogBase
{
public:
	ManageBookmarksDialog(HINSTANCE hInstance, HWND hParent, CoreInterface *coreInterface,
		Navigator *navigator, IconFetcher *iconFetcher, BookmarkTree *bookmarkTree);
	~ManageBookmarksDialog();

protected:
	INT_PTR OnInitDialog() override;
	INT_PTR OnAppCommand(HWND hwnd, UINT uCmd, UINT uDevice, DWORD dwKeys) override;
	INT_PTR OnCommand(WPARAM wParam, LPARAM lParam) override;
	INT_PTR OnClose() override;
	INT_PTR OnDestroy() override;
	INT_PTR OnNcDestroy() override;

	void SaveState() override;

	virtual wil::unique_hicon GetDialogIcon(int iconWidth, int iconHeight) const override;

private:
	static const int TOOLBAR_ID_BACK = 10000;
	static const int TOOLBAR_ID_FORWARD = 10001;
	static const int TOOLBAR_ID_ORGANIZE = 10002;
	static const int TOOLBAR_ID_VIEWS = 10003;

	ManageBookmarksDialog &operator=(const ManageBookmarksDialog &mbd);

	void GetResizableControlInformation(BaseDialog::DialogSizeConstraint &dsc,
		std::list<ResizableDialog::Control> &controlList) override;

	void SetupToolbar();
	void SetupTreeView();
	void SetupListView();

	LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	std::optional<LRESULT> OnToolbarCustomDraw(NMTBCUSTOMDRAW *customDraw);

	void OnTreeViewSelectionChanged(BookmarkItem *bookmarkFolder);
	void OnListViewNavigation(BookmarkItem *bookmarkFolder, bool addHistoryEntry);

	void UpdateToolbarState();

	LRESULT HandleMenuOrAccelerator(WPARAM wParam);

	void OnTbnDropDown(NMTOOLBAR *nmtb);

	// View menu
	void ShowViewMenu();
	void SetViewMenuItemStates(HMENU menu);
	void OnViewMenuItemSelected(int menuItemId);

	// Organize menu
	void ShowOrganizeMenu();
	void SetOrganizeMenuItemStates(HMENU menu);
	void OnOrganizeMenuItemSelected(int menuItemId);
	void OnNewBookmark();
	void OnNewFolder();
	void OnCopy(bool cut);
	void OnPaste();
	void OnDelete();
	void OnSelectAll();

	void OnOk();
	void OnCancel();

	HWND m_toolbarParent;
	HWND m_hToolbar;
	wil::unique_himagelist m_imageListToolbar;
	IconImageListMapping m_imageListToolbarMappings;

	CoreInterface *m_coreInterface;
	Navigator *m_navigator;
	IconFetcher *m_iconFetcher;

	BookmarkTree *m_bookmarkTree;

	BookmarkItem *m_currentBookmarkFolder;

	BookmarkTreeView *m_bookmarkTreeView;
	BookmarkListView *m_bookmarkListView;

	std::unique_ptr<BookmarkNavigationController> m_navigationController;

	std::vector<std::unique_ptr<WindowSubclassWrapper>> m_windowSubclasses;
	std::vector<boost::signals2::scoped_connection> m_connections;

	ManageBookmarksDialogPersistentSettings *m_persistentSettings;
};
