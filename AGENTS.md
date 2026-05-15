# 项目说明 - 无人机航拍目标检测与智能分析系统

## 项目概述
基于 Qt C++ + TensorRT 的无人机航拍目标检测系统，支持图片和视频检测，带 AI 对话分析功能。

## 技术栈
- **UI框架**: Qt 6.9.3 (MSVC 2022 64-bit)
- **推理引擎**: TensorRT 10.14.1.48
- **CUDA**: v12.9
- **OpenCV**: 通过 `../steeltrue/opencv_config.props` 配置
- **数据库**: SQLite (Qt SQL模块)
- **构建**: Visual Studio 2022, MSBuild v143, C++17

## 项目路径
- **根目录**: `D:\Microsoft Visual Studio2022\vs_qt_projects\kitchen\`
- **解决方案**: `kitchen.sln`
- **项目文件**: `kitchen.vcxproj`
- **模型目录**: `models/`
- **输出目录**: `x64/Debug/` 或 `x64/Release/`

## 模型信息
- **模型文件**: `models/yolov8s_150.engine` (TensorRT格式)
- **ONNX源文件**: `models/yolov8s_150.onnx`
- **PyTorch源文件**: `models/yolov8s_150.pt`
- **输入尺寸**: 640x640
- **输出格式**: `(1, 14, 33600)` - 14=4(bbox)+10(classes), 33600个检测框, 列优先存储
- **置信度阈值**: 0.5 (可通过AI对话修改)
- **NMS阈值**: 0.45
- **无需sigmoid**: 模型输出已经是概率值

## 检测类别 (VisDrone 10类)
0. 行人
1. 人群
2. 自行车
3. 轿车
4. 面包车
5. 卡车
6. 三轮车
7. 遮阳三轮车
8. 公交车
9. 摩托车

## 源文件清单

### 核心文件
| 文件 | 说明 |
|------|------|
| `main.cpp` | 程序入口 |
| `mainwindow.h/cpp` | 主窗口，所有UI逻辑 |
| `MainWindow.ui` | Qt Designer UI文件 |
| `Detection.h` | 检测结果结构体、DetectionConfig配置类 |

### 推理模块
| 文件 | 说明 |
|------|------|
| `InferenceEngine.h/cpp` | TensorRT推理封装，加载engine、前处理、后处理 |

### 数据库模块
| 文件 | 说明 |
|------|------|
| `DatabaseManager.h/cpp` | SQLite数据库，存储检测任务和结果 |

### AI对话模块
| 文件 | 说明 |
|------|------|
| `AnalysisAgent.h/cpp` | 智能分析员，处理用户查询 |

### 图片检测
| 文件 | 说明 |
|------|------|
| `DetectionThreadPool.h/cpp` | 单线程异步检测管理器(名为ThreadPool但实际是单线程) |
| `ImagePreviewDialog.h/cpp` | 图片放大预览对话框 |
| `HistoryDialog.h/cpp` | 历史记录对话框 |

### 视频检测
| 文件 | 说明 |
|------|------|
| `VideoDetectionManager.h/cpp` | 视频检测和播放管理 |
| `VideoPreviewDialog.h/cpp` | 视频放大预览对话框 |

## 关键架构说明

### TensorRT 10.x API变化
- 使用 `delete` 代替 `destroy()`
- 使用 `getNbIOTensors()` 代替 `getNbBindings()`
- `deserializeCudaEngine()` 只需一个参数
- 需要 `initLibNvInferPlugins()` 初始化插件

### YOLOv8输出解析
```cpp
// 列优先访问: output[classIdx * numDetections + detectionIdx]
float cx = m_outputHost[0 * m_numDetections + i];
float cy = m_outputHost[1 * m_numDetections + i];
float w  = m_outputHost[2 * m_numDetections + i];
float h  = m_outputHost[3 * m_numDetections + i];
float conf = m_outputHost[(4 + classIdx) * m_numDetections + i];
```

### 视频检测策略
- 每3帧检测一次，中间帧复用上次检测结果
- 检测结果保存到 `m_allResults` 向量，支持回放

### UI主题
- 深色太空主题: `#0B1120` 背景
- 科技青色: `#00E5FF`
- 检测绿色: `#00FF88`
- 警告橙色: `#FFB000`
- 错误红色: `#FF6B6B`

## 构建命令
```powershell
& "D:\Microsoft Visual Studio2022\chanpin\MSBuild\Current\Bin\MSBuild.exe" "D:\Microsoft Visual Studio2022\vs_qt_projects\kitchen\kitchen.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
```

## 重要注意事项

1. **重新构建前必须关闭 kitchen.exe** - 否则会报 LNK1104 错误
2. **不要使用 Qt::UniqueConnection** - 与 lambda 槽函数不兼容
3. **TensorRT context 不是线程安全的** - 多线程需要每个线程独立的 engine 和 context
4. **UI全中文** - 所有界面文字必须是中文
5. **布局使用 QSplitter** - 左右面板可拖动调整
6. **模型路径查找** - 从 exe 目录向上递归查找 `models/` 文件夹
7. **添加新文件后需更新 kitchen.vcxproj** - 包含 ClCompile、QtMoc、ClInclude 等

## 添加新文件的步骤
1. 创建 `.h` 和 `.cpp` 文件
2. 在 `kitchen.vcxproj` 中添加:
   - `<ClCompile Include="xxx.cpp" />` 到源文件组
   - `<QtMoc Include="xxx.h" />` 到MOC组 (如果有Q_OBJECT)
   - `<ClInclude Include="xxx.h" />` 到头文件组 (如果是纯头文件)
3. 在需要使用的文件中 `#include` 新头文件

## 已知问题
- 检测精度: 空区域可能出现低置信度误检（模型问题，非代码问题）
- 视频检测: 每3帧检测一次，非逐帧检测（性能优化）
