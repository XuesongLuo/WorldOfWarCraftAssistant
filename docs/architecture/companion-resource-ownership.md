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
| 只读客户区捕获的 DC 与位图 | `WindowCapture` 当前调用栈 | `wil::unique_hdc`、`wil::unique_hbitmap` | 单帧检测结束或异常展开 |
| TypeScript Host 进程、匿名管道和 Job Object | `HostProcess` | `wil::unique_handle`；Job 使用 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` | 先关闭 stdin 并限时等待；超时仅终止所属 Job 进程树 |
| Host stderr 排空线程 | `HostProcess` | `std::thread` | 子进程退出、stderr 管道 EOF 后 join |
| Codex App Server 子进程 | TypeScript Host；进程树最终由 C++ `HostProcess` Job 拥有 | Node `ChildProcess` + stdin/stdout/stderr 管道 | Host 先关闭 App Server stdin 并限时等待；Host 被终止时 Job 同步清理后代 |
| 本地问答工作线程 | `ApplicationShell` | `std::jthread` | 退出时先 join，再释放 `AssistantSession` 与覆盖层；同一时刻仅允许一个请求 |
| 跨线程覆盖层消息 | `OverlayWindow` | 堆分配 JSON + 私有 `WM_APP` 消息 | UI 线程消费后释放；窗口销毁前清空仍排队消息 |

退出统一经过主窗口 `WM_DESTROY`：先删除托盘图标，再结束消息循环。后续加入浮层、捕获和
Codex 子进程时，必须在销毁主窗口之前取消请求、释放图形资源，并只关闭本进程创建的子进程。

STEP-009 的 `HostProcess` 通过挂起创建避免“子进程先逃离 Job”的竞态；成功加入 Job 后才恢复
主线程。父进程持有的管道端禁止继承，子进程退出码、协议超时和 stderr 诊断分别报告。stderr
最多保留 64 KiB，stdout 只允许版本化 JSONL，避免诊断文字污染协议流。

STEP-010 中 App Server stdout 只进入 TypeScript 内部解析器，不直接转发给 C++；App Server
stderr 仍只进入 Host stderr 并设置 64 KiB 上限。Node 子进程继承 C++ 已建立的不可逃逸 Job，
正常关闭顺序为取消 turn、关闭 App Server stdin、等待/终止 App Server、再退出 Host。

STEP-011 中 `ApplicationShell` 唯一拥有 `AssistantSession` 和一个本地问答线程。退出顺序为停止
UI 接收新输入、等待当前请求、释放会话（从而关闭 Host/Job），最后销毁覆盖层。工作线程不得
直接访问 WebView2；它只向覆盖层窗口投递拥有型 JSON 消息，由 UI 线程消费或在析构时回收。

`resources.hpp` 是平台资源类型的统一入口。新代码不得把拥有型裸 `HANDLE`、`HWND` 或 COM
接口指针存入成员；确需自定义资源时应使用 WIL `unique_any` 或等价的不可复制 RAII 类。
