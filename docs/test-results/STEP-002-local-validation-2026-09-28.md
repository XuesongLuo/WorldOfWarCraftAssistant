# STEP-002 本地验证记录

- 日期：2026-09-28
- 本地结论：C++ 配置、Debug/Release 构建、Catch2 测试、最小程序和仓库检查通过
- 步骤结论：实现完成；远端 CI 尚未执行，因此 STEP-002 验证状态保持未完成

## 工具链

| 组件 | 实测版本 |
|---|---|
| Visual Studio Build Tools | 17.14.37710.0 |
| MSVC Tools | 14.44.35207 |
| MSVC 编译器 | 19.44.35229.0 |
| Windows SDK | 10.0.26100.0 |
| CMake | 3.31.6-msvc6 |
| Ninja | 1.12.1 |

精确基线已写入 `eng/toolchain.json`。Visual Studio Installer 因项目路径过长拒绝首次目标目录，最终使用专用短路径 `C:\WOWAI\vsbt`；项目脚本只使用项目 `.tools` 或该登记路径。

## 正常路径

执行并通过：

```powershell
.\scripts\Invoke-CMake.ps1 --preset windows-msvc-debug
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-debug --target verify
ctest --preset windows-msvc-debug --output-on-failure
.\out\build\windows-msvc-debug\companion\Debug\wowai_companion.exe
```

结果：2/2 Catch2 测试通过；`verify` 同时通过 STEP-001 和仓库骨架检查；最小程序输出 `World of Warcraft AI Assistant 0.1.0-dev`，退出码为 0。

另以无本地 Catch2 源码覆盖的 `windows-msvc-release --fresh` 从 GitHub 官方 codeload 下载归档，URL 的 SHA-256 校验通过，Release 构建和 2/2 测试通过。这验证了没有 `.tools` 缓存时的依赖恢复路径。

## 失败路径与修复

1. 缺少仓库根目录：`verify-repository.ps1 -RepositoryRoot .\__missing_repository_fixture__` 退出码为 1，并明确报告缺少 `README.md`。
2. Codex Desktop 宿主同时提供 `Path`/`PATH`，MSBuild 报重复环境键：新增 `Invoke-CMake.ps1`，为 CMake 子进程规范为单一大写 `PATH`。
3. Catch2 并行编译写入同一 PDB，MSVC 报 C1041：对 Catch2 目标启用 `/FS`，重新构建通过。
4. CTest 最初从根目录找不到子目录测试：把 `enable_testing()` 提升到顶层，复测得到 2/2。
5. GitHub archive 重定向连接被重置：改为官方 codeload 直链，保持同一版本和 SHA-256，干净 Release 配置通过。

## 尚未完成的验证

- 仓库尚无 Git remote，`.github/workflows/ci.yml` 无法在真实 GitHub Actions runner 上执行。
- A-02 仍需在 CI 中以故意格式错误验证门禁失败。
- DEP-03 仍需比较本地与 CI 工具链报告；Node/npm 项目级锁定属于 STEP-003。
