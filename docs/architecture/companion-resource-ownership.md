# C++ 伴侣程序资源所有权

STEP-006 的最小外壳采用“创建者唯一拥有、析构逆序释放”的规则。业务状态不保存裸资源句柄；
Win32 回调中出现的 `HWND` 仅为借用引用，不转移所有权。

| 资源 | 所有者 | RAII 类型或释放动作 | 释放时机 |
|---|---|---|---|
| 单实例互斥体、线程/事件句柄 | 创建它的组件 | `wil::unique_handle` | 组件析构或异常展开 |
| COM apartment | UI 主线程 | `wil::unique_couninitialize_call` | 消息循环退出后 |
| 主窗口 | `ApplicationShell` | `wil::unique_hwnd` | 正常退出或构造失败展开 |
| 窗口类 | `ApplicationShell::WindowClassRegistration` | 析构调用 `UnregisterClassW` | 主窗口销毁后 |
| 托盘图标 | `ApplicationShell::TrayIcon` | 析构调用 `Shell_NotifyIconW(NIM_DELETE)` | 主窗口销毁前 |
| 弹出菜单、独占图标 | 创建它的 UI 作用域 | `wil::unique_hmenu`、`wil::unique_hicon` | 当前 UI 操作结束 |
| Direct3D、DXGI、DirectComposition 等图形接口 | 后续图形组件 | `wil::com_ptr<T>` | 图形组件析构或设备重建 |

退出统一经过主窗口 `WM_DESTROY`：先删除托盘图标，再结束消息循环。后续加入浮层、捕获和
Codex 子进程时，必须在销毁主窗口之前取消请求、释放图形资源，并只关闭本进程创建的子进程。

`resources.hpp` 是平台资源类型的统一入口。新代码不得把拥有型裸 `HANDLE`、`HWND` 或 COM
接口指针存入成员；确需自定义资源时应使用 WIL `unique_any` 或等价的不可复制 RAII 类。
