# ADR-015：C++ 第三方库与链接策略

- 状态：已接受（STEP-006）
- 日期：2026-09-28
- 负责人：Windows 客户端技术负责人
- 决策截止点：STEP-006 开始前
- 关联：DEP-01、DEP-05、DEP-06、A-03、A-12

## 背景

C++ 伴侣程序需要可靠的资源管理、JSON、SQLite、日志和测试能力，同时应控制供应链、安装体积和安全更新成本。

## 默认 PoC 假设

采用 C++20、MSVC v143、Windows 11 SDK、CMake 和 vcpkg manifest。直接依赖限定为 WIL、nlohmann-json、sqlite3、spdlog 和 Catch2；图形、捕获、WIC、WinHTTP 与凭据能力使用 Windows SDK。PoC 使用 x64 和 `/MDd`（Debug）/`/MD`（Release），由最终安装包携带匹配的 VC++ Redistributable。WebView2 SDK 仅在 ADR-014 继续采用 WebView2 时加入。

## 决策

2026-09-30 在 STEP-006 开始时接受默认 PoC 假设。最小外壳首先使用 WIL 管理 Win32、COM
和未来图形接口资源；不为已由 Windows SDK 提供的生命周期能力新增第三方依赖。Debug 与
Release 构建均继续使用 `x64-windows` triplet，对应 `/MDd` 与 `/MD`。

## 验证与接受条件

- vcpkg baseline、目标架构、MSVC 工具集、Windows SDK 和运行库策略全部锁定。
- 干净环境可还原相同依赖树；NOTICE、许可证和 SBOM 可生成。
- 未使用依赖不进入构建或安装包。
- 路径含空格和非 ASCII 字符时可配置、构建和测试。

## 回退方案

若某第三方库存在许可证、高危漏洞或维护问题，优先替换为 Windows API 或小型自有封装；若动态运行库分发验证失败，再单独评估 `/MT`，不得在没有许可证和安全更新评审时切换。
