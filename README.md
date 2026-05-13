# Kitchen

基于 Qt + TensorRT 的目标检测桌面应用，用于批量图片检测、统计与报告导出。

## 功能
- 图片/文件夹导入与批量检测
- 检测结果可视化与目标列表
- 统计面板与类别汇总
- CSV 导出与 HTML 报告生成
- 对话式分析查询

## 环境要求
- Windows
- Visual Studio 2022
- Qt (与项目一致的版本)
- CUDA + TensorRT
- OpenCV

## 构建
1. 打开 `kitchen.sln`
2. 配置好 Qt/CUDA/TensorRT/OpenCV 的 include/lib 路径
3. 生成项目

## 运行
- 将模型文件放在 `models/` 目录下
  - `yolov8s_150.engine`
  - `yolov8s_150.onnx`
- 启动后导入图片或文件夹，点击“开始检测”

## 注意
- 模型加载路径会在程序目录的 `models/` 及其上级目录中自动查找。
- TensorRT 版本升级时，`engine` 需要重新生成。

## 许可证
私有项目，暂未指定许可证。
