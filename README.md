# FL Plugin Atlas

插件图谱——面向 Windows 桌面版 FL Studio 的插件分类管理器。

按厂商、制作用途或两者组合查找插件，并将选定的预设入口发布为 FL Studio Plugin database 收藏，通过 Browser、F8 和混音器插槽加载。

Windows x64 版本提供带中文面板的 CLAP 效果器，以及共用同一 SQLite 索引的命令行工具。

## 在 FL Studio 中使用

1. 在 FL Studio 的 Plugin Manager 中查看标为 **CLAP** 的扫描目录。本机显示的是 `C:\Program Files\Common Files\CLAP`。将 `FL Plugin Atlas.clap` 安装到该目录后，运行 **Find installed plugins**。本机实测添加用户目录后搜索仍为零项。
2. 在 Mixer 的空效果插槽选择 **More plugins → FL Plugin Atlas**。点“扫描插件”读取默认库，或点“选择目录”指定自己的 Plugin database 目录。选定目录后会自动扫描。
3. 从下拉框选厂商，在左侧点击一个用途。点击另一个用途即可切换，点“全部”查看当前厂商的所有用途。插件本身可以有多个用途。批量编辑时，在中间列表多选条目，在底部输入标签或厂商名，再打开“编辑分类…”。
4. 扫描完成后，管理器在后台按厂商和型号搜索 plugin.watch 的产品图，也可点“联网补图”重试。预览依次使用自选图片、FL 缩略图和已缓存的网络产品图。未匹配时显示识别封面。厂商不明或扫描身份有冲突的条目可用“选择图片”手动补全。网络缓存位于当前用户的 `FL Plugin Atlas/Online images` 目录。
5. 在列表选中一个预设，点击图片上方的“添加”，松开鼠标，再点击 Channel Rack 空白处（乐器）或 Mixer 空效果插槽（效果器）。按 Esc 或右键取消。这个操作把原始 `.fst` 交给点击位置对应的 FL 接收区域。
6. 插件作为立体声效果器透传输入。库扫描在后台执行，分类修改保存在当前用户的本地索引。

## 在 PowerShell 中使用

在包含 `atlas.exe` 的目录运行：

```powershell
.\atlas.exe scan
.\atlas.exe vendors
.\atlas.exe purposes
.\atlas.exe list --vendor FabFilter --tag 均衡
```

`scan` 默认读取 Windows“文档”中的 `Image-Line\FL Studio\Presets\Plugin database`。使用自定义 FL 数据目录时，每条命令加上同一个 `--library "完整的 Plugin database 路径"`。默认索引位于 `%LOCALAPPDATA%\FL Plugin Atlas\catalog.sqlite3`；可用 `--db` 指定其他位置。FL 新增插件后再次运行 `scan` 即可更新。

从 `list` 最后一列复制入口路径，可为插件添加用途或修正厂商。以下示例的路径需要与自己的列表一致：

```powershell
.\atlas.exe tag --entry "Installed/Effects/VST3/FabFilter Pro-Q.fst" --add 母带
.\atlas.exe list --vendor FabFilter --tag 均衡 --tag 母带
.\atlas.exe list --tag 待分类
```

重复传入 `--entry` 可以批量打标签。`--remove` 可以移除自动识别的用途，重扫仍保留该选择。`vendor` 修正单个插件的厂商，`alias` 统一某个厂商的名称；通过 `atlas --help` 查看语法。

## 构建与测试

Windows 使用 Visual Studio 2022 的 C++ 桌面工具、含 WinSQLite 的 Windows 10/11 SDK，以及 CMake 3.18 或更高版本。以下命令在项目根目录执行。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
cmake -E chdir build ctest -C Release --output-on-failure
.\build\Release\atlas.exe --help
```

首次配置 Windows 构建时，CMake 会获取固定版本的 CLAP 头文件。离线构建可用 `-DCLAP_SDK_DIR=已有SDK目录`。插件输出与 CLI 位于同一构建目录。当前面板支持浏览与分类，发布到 FL 收藏目录仍待实现。架构约束见 [AGENTS.md](AGENTS.md)。

