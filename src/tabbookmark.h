#ifndef TABBOOKMARK_H_
#define TABBOOKMARK_H_

#include "iaccessible.h"
#include <windowsx.h>
#include <algorithm>  // 用于clamp函数

HHOOK mouse_hook = nullptr;

#define KEY_PRESSED 0x8000

// 增加平滑滚动参数
#ifndef CUSTOM_WHEEL_DELTA
#define CUSTOM_WHEEL_DELTA 1    // 保持标准滚动量
#define SMOOTH_FACTOR 0.5f        // 提高平滑因子（原0.2）
#define SCROLL_THRESHOLD 0.1f     // 降低滚动阈值（原0.5）
#endif
bool IsPressed(int key) {
  return key && (::GetKeyState(key) & KEY_PRESSED) != 0;
}

// Compared with `IsOnlyOneTab`, this function additionally implements tick
// fault tolerance to prevent users from directly closing the window when
// they click too fast.
bool IsNeedKeep(NodePtr top_container_view) {
  if (!IsKeepLastTab()) {
    return false;
  }

  auto tab_count = GetTabCount(top_container_view);
  bool keep_tab = (tab_count == 1);

  static auto last_closing_tab_tick = GetTickCount64();
  auto tick = GetTickCount64() - last_closing_tab_tick;
  last_closing_tab_tick = GetTickCount64();

  if (tick > 50 && tick <= 250 && tab_count == 2) {
    keep_tab = true;
  }

  return keep_tab;
}

// If the top_container_view is not found at the first time, try to close the
// find-in-page bar and find the top_container_view again.
NodePtr HandleFindBar(HWND hwnd, POINT pt) {
  // If the mouse is clicked directly on the find-in-page bar, follow Chrome's
  // original logic. Otherwise, clicking the button on the find-in-page bar may
  // directly close the find-in-page bar.
  if (IsOnDialog(hwnd, pt)) {
    return nullptr;
  }
  NodePtr top_container_view = GetTopContainerView(hwnd);
  if (!top_container_view) {
    ExecuteCommand(IDC_CLOSE_FIND_OR_STOP, hwnd);
    top_container_view = GetTopContainerView(hwnd);
    if (!top_container_view) {
      return nullptr;
    }
  }
  return top_container_view;
}

class IniConfig {
 public:
  IniConfig()
      : is_double_click_close(IsDoubleClickClose()),
        is_right_click_close(IsRightClickClose()),
        is_wheel_tab(IsWheelTab()),
        is_wheel_tab_when_press_right_button(IsWheelTabWhenPressRightButton()),
        is_bookmark_new_tab(IsBookmarkNewTab()),
        is_open_url_new_tab(IsOpenUrlNewTabFun()) {}

  bool is_double_click_close;
  bool is_right_click_close;
  bool is_wheel_tab;
  bool is_wheel_tab_when_press_right_button;
  std::string is_bookmark_new_tab;
  std::string is_open_url_new_tab;
};

IniConfig config;

// Use the mouse wheel to switch tabs
bool HandleMouseWheel(WPARAM wParam, LPARAM lParam, PMOUSEHOOKSTRUCT pmouse) {
  if (wParam != WM_MOUSEWHEEL ||
      (!config.is_wheel_tab && !config.is_wheel_tab_when_press_right_button)) {
    return false;
  }

  HWND hwnd = GetFocus();
  NodePtr top_container_view = GetTopContainerView(hwnd);

  PMOUSEHOOKSTRUCTEX pwheel = (PMOUSEHOOKSTRUCTEX)lParam;
  int zDelta = GET_WHEEL_DELTA_WPARAM(pwheel->mouseData);

  // If the mouse wheel is used to switch tabs when the mouse is on the tab bar.
  if (config.is_wheel_tab && IsOnTheTabBar(top_container_view, pmouse->pt)) {
    hwnd = GetTopWnd(hwnd);
    if (zDelta > 0) {
      ExecuteCommand(IDC_SELECT_PREVIOUS_TAB, hwnd);
    } else {
      ExecuteCommand(IDC_SELECT_NEXT_TAB, hwnd);
    }
    return true;
  }

  // If it is used to switch tabs when the right button is held.
  if (config.is_wheel_tab_when_press_right_button && IsPressed(VK_RBUTTON)) {
    hwnd = GetTopWnd(hwnd);
    if (zDelta > 0) {
      ExecuteCommand(IDC_SELECT_PREVIOUS_TAB, hwnd);
    } else {
      ExecuteCommand(IDC_SELECT_NEXT_TAB, hwnd);
    }
    return true;
  }

  return false;
}

// Double-click to close tab.
int HandleDoubleClick(WPARAM wParam, PMOUSEHOOKSTRUCT pmouse) {
  if (wParam != WM_LBUTTONDBLCLK || !config.is_double_click_close) {
    return 0;
  }

  POINT pt = pmouse->pt;
  HWND hwnd = WindowFromPoint(pt);
  NodePtr top_container_view = HandleFindBar(hwnd, pt);
  if (!top_container_view) {
    return 0;
  }

  bool is_on_one_tab = IsOnOneTab(top_container_view, pt);
  bool is_on_close_button = IsOnCloseButton(top_container_view, pt);
  bool is_only_one_tab = IsOnlyOneTab(top_container_view);
  if (!is_on_one_tab || is_on_close_button) {
    return 0;
  }
  if (is_only_one_tab) {
    ExecuteCommand(IDC_NEW_TAB, hwnd);
    ExecuteCommand(IDC_WINDOW_CLOSE_OTHER_TABS, hwnd);
  } else {
    ExecuteCommand(IDC_CLOSE_TAB, hwnd);
  }
  return 1;
}

// Right-click to close tab (Hold Shift to show the original menu).
int HandleRightClick(WPARAM wParam, PMOUSEHOOKSTRUCT pmouse) {
  if (wParam != WM_RBUTTONUP || IsPressed(VK_SHIFT) ||
      !config.is_right_click_close) {
    return 0;
  }

  POINT pt = pmouse->pt;
  HWND hwnd = WindowFromPoint(pt);
  NodePtr top_container_view = HandleFindBar(hwnd, pt);
  if (!top_container_view) {
    return 0;
  }

  bool is_on_one_tab = IsOnOneTab(top_container_view, pt);
  bool keep_tab = IsNeedKeep(top_container_view);

  if (is_on_one_tab) {
    if (keep_tab) {
      ExecuteCommand(IDC_NEW_TAB, hwnd);
      ExecuteCommand(IDC_WINDOW_CLOSE_OTHER_TABS, hwnd);
    } else {
      // Attempt new SendKey function which includes a `dwExtraInfo`
      // value (MAGIC_CODE).
      SendKey(VK_MBUTTON);
    }
    return 1;
  }
  return 0;
}

// Preserve the last tab when the middle button is clicked on the tab.
int HandleMiddleClick(WPARAM wParam, PMOUSEHOOKSTRUCT pmouse) {
  if (wParam != WM_MBUTTONUP) {
    return 0;
  }

  POINT pt = pmouse->pt;
  HWND hwnd = WindowFromPoint(pt);
  NodePtr top_container_view = HandleFindBar(hwnd, pt);
  if (!top_container_view) {
    return 0;
  }

  bool is_on_one_tab = IsOnOneTab(top_container_view, pt);
  bool keep_tab = IsNeedKeep(top_container_view);

  if (is_on_one_tab && keep_tab) {
    ExecuteCommand(IDC_NEW_TAB, hwnd);
    ExecuteCommand(IDC_WINDOW_CLOSE_OTHER_TABS, hwnd);
    return 1;
  }

  return 0;
}

// Open bookmarks in a new tab.
bool HandleBookmark(WPARAM wParam, PMOUSEHOOKSTRUCT pmouse) {
  if (wParam != WM_LBUTTONUP || IsPressed(VK_CONTROL) || IsPressed(VK_SHIFT) ||
      config.is_bookmark_new_tab == "disabled") {
    return false;
  }

  POINT pt = pmouse->pt;
  HWND hwnd = WindowFromPoint(pt);
  NodePtr top_container_view = GetTopContainerView(
      GetFocus());  // Must use `GetFocus()`, otherwise when opening bookmarks
                    // in a bookmark folder (and similar expanded menus),
                    // `top_container_view` cannot be obtained, making it
                    // impossible to correctly determine `is_on_new_tab`. See
                    // #98.

  bool is_on_bookmark = IsOnBookmark(hwnd, pt);
  bool is_on_new_tab = IsOnNewTab(top_container_view);

  if (is_on_bookmark && !is_on_new_tab) {
    if (config.is_bookmark_new_tab == "foreground") {
      SendKey(VK_MBUTTON, VK_SHIFT);
    } else if (config.is_bookmark_new_tab == "background") {
      SendKey(VK_MBUTTON);
    }
    return true;
  }

  return false;
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode != HC_ACTION) {
    return CallNextHookEx(mouse_hook, nCode, wParam, lParam);
  }

  do {
    PMOUSEHOOKSTRUCT pmouse = (PMOUSEHOOKSTRUCT)lParam; // 移动声明到外层
    static LONG lastY = -1;  // 将静态变量声明移到外层作用域
    static float remainder = 0;  // 新增剩余量用于平滑滚动
    if (wParam == WM_NCMOUSEMOVE) {
      break;
    }

    // 新增边缘滚动检测
    if (wParam == WM_MOUSEMOVE) {
      HWND hwnd = WindowFromPoint(pmouse->pt);
      RECT rect;
      GetClientRect(hwnd, &rect);
      
      POINT client_pt = pmouse->pt;
      ScreenToClient(hwnd, &client_pt);

      // 获取文档总高度
      LONG sHeight = 0;
      if (NodePtr document = FindElementWithRole(GetChromeWidgetWin(hwnd), ROLE_SYSTEM_DOCUMENT)) {
        GetAccessibleSize(document, [&sHeight](RECT docRect) {
          sHeight = docRect.bottom;
        });
      }
      
      // 计算视口高度（需要减去滚动条宽度）
      LONG scrollbarWidth = GetSystemMetrics(SM_CXVSCROLL);
      LONG scrollboxHeight = rect.bottom - 2 * scrollbarWidth;
      
      // 动态计算滚动量系数
      float wheelFactor = (scrollboxHeight > 0 && sHeight > 0) ? 
                         static_cast<float>(sHeight) / scrollboxHeight : 
                         1.0f;
      int dynamicWheelDelta = static_cast<int>(CUSTOM_WHEEL_DELTA * wheelFactor);
      
      if (client_pt.x >= rect.right - 8) {
        if (lastY == -1) {
          lastY = client_pt.y;
          remainder = 0;  // 重置剩余量
        }
        
        // 带插值的平滑计算
        LONG delta = lastY - client_pt.y;
        float smoothedDelta = (delta + remainder) * SMOOTH_FACTOR;
        
        // 分离整数和小数部分
        int actualScroll = static_cast<int>(smoothedDelta);
        remainder = smoothedDelta - actualScroll;
        
        // 当余量超过阈值时强制滚动
        if (abs(remainder) >= SCROLL_THRESHOLD) {
          actualScroll += (remainder > 0) ? 1 : -1;
          remainder -= (remainder > 0) ? 1 : -1;
        }

        if (actualScroll != 0) {
          int scrollAmount = actualScroll * dynamicWheelDelta;  // 使用动态计算值
          SendMessage(hwnd, WM_MOUSEWHEEL, 
                     MAKEWPARAM(0, scrollAmount),
                     MAKELPARAM(pmouse->pt.x, pmouse->pt.y));
        }
        
        lastY = client_pt.y;
      } else {
        lastY = -1;
        remainder = 0;  // 离开时重置剩余量
      }
      break;
    }

    // Defining a `dwExtraInfo` value to prevent hook the message sent by
    // Chrome++ itself.
    if (pmouse->dwExtraInfo == MAGIC_CODE) {
      break;
    }

    if (wParam == WM_LBUTTONUP){
    HWND hwnd = WindowFromPoint(pmouse->pt);
    NodePtr TopContainerView = GetTopContainerView(hwnd);

    bool isOmniboxFocus = IsOmniboxFocus(TopContainerView);

    if (TopContainerView){
     }

    // 单击地址栏展开下拉菜单
    if (isOmniboxFocus){
      keybd_event(VK_PRIOR,0,0,0);
     }
    }

    if (HandleMouseWheel(wParam, lParam, pmouse)) {
      return 1;
    }

    if (HandleDoubleClick(wParam, pmouse) != 0) {
      // Do not return 1. Returning 1 could cause the keep_tab to fail
      // or trigger double-click operations consecutively when the user
      // double-clicks on the tab page rapidly and repeatedly.
    }

    if (HandleRightClick(wParam, pmouse) != 0) {
      return 1;
    }

    if (HandleMiddleClick(wParam, pmouse) != 0) {
      return 1;
    }

    if (HandleBookmark(wParam, pmouse)) {
      return 1;
    }
  } while (0);
  return CallNextHookEx(mouse_hook, nCode, wParam, lParam);
}

int HandleKeepTab(WPARAM wParam) {
  if (!(wParam == 'W' && IsPressed(VK_CONTROL) && !IsPressed(VK_SHIFT)) &&
      !(wParam == VK_F4 && IsPressed(VK_CONTROL))) {
    return 0;
  }

  HWND hwnd = GetFocus();
  wchar_t name[256] = {0};
  GetClassName(hwnd, name, 255);
  if (wcsstr(name, L"Chrome_WidgetWin_") == nullptr) {
    return 0;
  }

  if (IsFullScreen(hwnd)) {
    // Have to exit full screen to find the tab.
    ExecuteCommand(IDC_FULLSCREEN, hwnd);
  }

  HWND tmp_hwnd = hwnd;
  hwnd = GetAncestor(tmp_hwnd, GA_ROOTOWNER);
  ExecuteCommand(IDC_CLOSE_FIND_OR_STOP, tmp_hwnd);

  NodePtr top_container_view = GetTopContainerView(hwnd);
  if (!IsNeedKeep(top_container_view)) {
    return 0;
  }

  ExecuteCommand(IDC_NEW_TAB, hwnd);
  ExecuteCommand(IDC_WINDOW_CLOSE_OTHER_TABS, hwnd);
  return 1;
}

int HandleOpenUrlNewTab(WPARAM wParam) {
  if (!(config.is_open_url_new_tab != "disabled" && wParam == VK_RETURN &&
        !IsPressed(VK_MENU))) {
    return 0;
  }

  NodePtr top_container_view = GetTopContainerView(GetForegroundWindow());
  if (IsOmniboxFocus(top_container_view) && !IsOnNewTab(top_container_view)) {
    if (config.is_open_url_new_tab == "foreground") {
      SendKey(VK_MENU, VK_RETURN);
    } else if (config.is_open_url_new_tab == "background") {
      SendKey(VK_SHIFT, VK_MENU, VK_RETURN);
    }
    return 1;
  }
  return 0;
}

HHOOK keyboard_hook = nullptr;
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && !(lParam & 0x80000000))  // pressed
  {
    if (HandleKeepTab(wParam) != 0) {
      return 1;
    }

    if (HandleOpenUrlNewTab(wParam) != 0) {
      return 1;
    }
    
    if (wParam == VK_PRIOR && IsPressed(VK_CONTROL)){
      return 1;
    }
    if (wParam == VK_NEXT && IsPressed(VK_CONTROL)){
      return 1;
    }
  }
  return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}

void TabBookmark() {
  mouse_hook =
      SetWindowsHookEx(WH_MOUSE, MouseProc, hInstance, GetCurrentThreadId());
  keyboard_hook = SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, hInstance,
                                   GetCurrentThreadId());
}

#endif  // TABBOOKMARK_H_
