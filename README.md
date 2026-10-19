# Blender NPR

当前版本：**1.0.1**（Blender **5.2.2 LTS**）。本次更新屏幕空间描边与边缘光的轮廓距离算法，详情见 [更新说明](CHANGELOG.md)。

`main` 用于最新源码；`release/blender-5.2.2-npr-1.0.1` 保存此版本源码。

面向风格化角色渲染的 Blender 自定义版本，基于 **Blender 5.2.2 LTS**，为 **EEVEE** 增加原生 NPR 材质节点，覆盖二分光照、描边、边缘光、SDF 面部阴影、角色投影与眼透效果。

这些功能集成在 Blender 源码中，需要使用本项目提供的程序或自行编译，不能作为插件安装到标准版 Blender。

## 功能

材质节点编辑器的 **添加 → NPR** 菜单提供以下节点：

| 节点 | 用途 |
| --- | --- |
| Half Lambert | 风格化二分光照 |
| Edge Outline | 角色描边 |
| Edge Rim | 边缘光 |
| SDF Driver | 使用 SDF 贴图驱动面部明暗分界 |
| Shadow Caster | 标记头发、衣物等角色阴影投射者 |
| Character Shadow | 接收头发、衣物投射到角色表面的阴影 |
| Eye See Through | 眼透效果 |
| Eye See Through Source | 配合眼透效果标记来源 |

## 下载与运行

当前提供 **Windows x64** 便携版。

1. 在本仓库的 **Releases** 页面下载名称以 `windows-x64.zip` 结尾的压缩包。
2. 完整解压到一个文件夹，运行其中的 `blender.exe`。请保留同目录的 DLL、版本资源目录和其他依赖。
3. 将场景渲染引擎设为 **EEVEE**，打开材质节点编辑器，通过 **添加 → NPR** 使用节点。

源码包以 `source.zip` 结尾。Release 中的 `SHA256SUMS.txt` 可用于核对下载文件的 SHA256。

## 角色阴影入门

1. 在头发或衣物材质中添加 **Shadow Caster**，选择对应的投射通道。该节点无需接线。
2. 在接收阴影的材质中添加 **Character Shadow**，选择接收通道，并指定场景中的 **Sun** 灯光。
3. 将基础颜色接入 `Color`，用 `Shadow Tint` 调整阴影颜色；可将颜色输出接入 Emission，再连接 Material Output。

`Shadow` 输出中 **1 表示阴影**，`Visibility` 输出中 **1 表示受光**。如果将颜色输出接入 Principled BSDF，材质仍会受到 EEVEE 普通光照和阴影的影响。

## 适用范围

- NPR 节点面向 EEVEE；Cycles 不在当前 NPR 功能支持范围内。
- Character Shadow 使用 Sun 定向光，并提供场景级头发／衣物通道，不是每个角色独立的一套阴影通道。
- SDF 面部阴影需要与模型匹配的 SDF 贴图；项目不附带角色模型、贴图或演示工程。
- 当前 Windows 构建未启用 Cycles GPU 内核预编译，完整选项见 [构建配置](docs/build-options.cmake)。

当前版本已通过后台启动与 8 个 NPR 节点的创建检查。这些检查不代表所有显卡、驱动和材质组合都已完成视觉验证。

## 从源码构建

源码位于 [`source/`](source/)，包含 NPR 修改和新增节点。Windows 工具链、依赖库版本与构建命令见 [构建指南](docs/BUILD.md)。

```text
source/     Blender 源码及 NPR 修改
docs/       构建指南、版本信息和校验记录
```

## 问题反馈

提交 Issue 时，请附上程序版本、显卡与驱动版本、使用的渲染引擎，以及复现步骤。涉及材质效果时，建议提供节点截图或可公开分享的最小复现工程。

## 许可证与致谢

本项目基于 [Blender](https://www.blender.org/) 开发，是非官方修改版本，与 Blender 官方发布无关。

许可证见 [COPYING](COPYING) 与 [GNU GPL](GPL-license.txt)。第三方组件保留各自的许可证和版权声明，详见源码中的 `doc/license/` 及运行版的 `license/` 目录。
