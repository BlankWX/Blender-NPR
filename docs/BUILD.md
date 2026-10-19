# Windows 构建指南

本指南适用于仓库源码和 Release 源码包。两者均使用 `source/` 存放 Blender 源码，以下命令在包含 `source/` 与 `docs/` 的项目根目录运行。

## 工具链

- Windows x64
- Visual Studio 2026 C/C++ 开发工具与 Windows SDK
- CMake（支持 `Visual Studio 18 2026` 生成器）
- Git、Git LFS 和 Python

平台依赖说明见 [Blender 官方 Windows 构建文档](https://developer.blender.org/docs/handbook/building_blender/windows/)。不同工具链版本可能需要调整 CMake 生成器；当前发布版本使用 `Visual Studio 18 2026`。

## 源码与依赖版本

| 项目 | 版本／提交 |
| --- | --- |
| Blender | 5.2.2 LTS |
| 上游源码基点 | `d13f752e3b9c4f8c261cda552b1021f8bcc0382c` |
| Windows x64 依赖库基点 | `60d6e96b917568278d400a4024c98da0fb777338` |

`source/` 已包含 NPR 修改。上游基点用于追溯版本，不能代替本项目源码。

预编译开发库需要单独下载；运行版自带的 DLL 不等同于编译所需的开发库。

## 获取依赖

在 PowerShell 中运行：

```powershell
git lfs install
git clone https://projects.blender.org/blender/lib-windows_x64.git windows-libs
git -C windows-libs checkout 60d6e96b917568278d400a4024c98da0fb777338
git -C windows-libs lfs pull
```

## 配置、编译与安装

```powershell
cmake -S source -B build -G "Visual Studio 18 2026" -A x64 -C docs/build-options.cmake "-DLIBDIR=$((Resolve-Path windows-libs).Path)" "-DCMAKE_INSTALL_PREFIX=$((Get-Location).Path)/install"
cmake --build build --config Release --parallel 4
cmake --install build --config Release
```

`build-options.cmake` 记录当前发行构建的 `WITH_*` 功能开关。源码中的 `build_files/` 和 `make.bat` 保留了上游构建工具。

## 验证状态

上述步骤根据发行版本的实际配置整理，尚未完成全新环境下的完整重编译验证。发行版本所用的依赖库工作树存在本地修改，因此仅检出所列基点并不能保证得到与发行版字节一致的程序。

构建时应重新生成 CMake 缓存，避免复用其他机器上的绝对路径。版本记录见 [BUILD_INFO.json](BUILD_INFO.json)。
