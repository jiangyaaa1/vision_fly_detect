# 参考工程许可

本项目参考 `/home/jhh/code/sp_vision_25-main` 的模块划分、代码格式与坐标变换思路，沿用其 `.clang-format`（删除本机工具不兼容的结束标记）。参考项目许可原文如下。定位实现为本项目重新编写；MaixCDK与OpenCV保持各自许可证，本工程未将SDK源码打包。

MIT License

Copyright (c) 2025 TongjiSuperPower

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## SZURPVision Infantry-v8n model

Source: https://github.com/SZURPVision/26_NNDeployment_Lib_and_Detection_Models

The user supplied Infantry-v8n-fp16-20260726-D1.8w-B16.onnx. Model metadata and upstream root license are preserved in models/infantry/source_metadata.json and UPSTREAM_LICENSE. The source graph metadata identifies Ultralytics and AGPL-3.0; the upstream repository root carries MIT. Conversion provenance and checksums are recorded in manifest.json. The dedicated decoder is implemented for the documented 21-channel output contract; it does not incorporate the upstream OpenVINO/TensorRT runtime or temporal white-armor filtering.
