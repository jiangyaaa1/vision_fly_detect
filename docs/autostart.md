# MaixCAM Lite独立供电与开机自启动

已配置：正常开机后自动运行完整YOLO→模型角点→PnP→UART0相对坐标发送，不依赖电脑启动命令或USB网络。串口与模型使用当前板端configs/maixcam.yaml；默认UART0、A16发送/A17接收、115200/8N1，坐标单位米。

## 安装内容

- `/etc/init.d/S98vision_fly`：板子实际使用的SysV启动服务，调用工程board/vision_service.py。设置工作目录和动态库路径后启动原C++程序，记录infantry.pid及active_run.json。
- `/etc/init.d/S06maixapp`：保留板卡、串口、MaixVision服务等原初始化，只禁用Launcher启动行，以及写往UART0的serial ready文本。
- `/maixapp/apps/launcher/launcher`与`launcher_daemon`：替换为明确禁止启动的入口脚本，避免系统或界面再次拉起Launcher并占用UART0。原始程序单独备份，可恢复；不删除用户的模型、内参、外参或其它应用。
- `/root/vision_fly_boot_backup`：保存首次安装前的原件、SHA256清单、原始权限、符号链接信息及安装时的配置参考副本。重复安装不会覆盖最初备份；恢复前若检测到文件被其它操作修改，会拒绝覆盖。

这是开机启动服务，包含防重复启动和准确的进程归属校验。它不是崩溃后不断重启程序的守护进程；运行异常时应查看日志并修复原因，再重启服务。用户手动停止或改用纯预览时，本次开机内不会被自动抢回相机；下一次正常开机会再次启动完整识别。

## 电脑端命令

```bash
cd /home/jhh/code/vision_fly_detect
# 查看是否启用及识别进程状态
/usr/bin/python3 -s tools/setup_autostart.py status

# 部署当前程序并安装/更新自启动（正常使用不必每次执行）
/usr/bin/python3 -s tools/setup_autostart.py install

# 停止识别、恢复原启动文件和Launcher入口；模型和用户标定文件保留
/usr/bin/python3 -s tools/setup_autostart.py restore
```

恢复后重启板子，即恢复原Launcher开机行为。已有独立备份请保留；刷写新系统/固件后，应重新核对开机配置与固件版本，不要盲目恢复旧固件的启动文件。

## 板端命令与日志

通过SSH进入Lite后：

```bash
/etc/init.d/S98vision_fly status
/etc/init.d/S98vision_fly restart
/etc/init.d/S98vision_fly stop
/etc/init.d/S98vision_fly start
```

日志和状态文件位于`/root/vision_fly_detect_infantry`：

| 文件 | 内容 |
|---|---|
| autostart.log | 开机服务启动/停止及失败信息 |
| infantry.log | C++模型、相机、UART和PERF日志；本次启动会重新创建 |
| target_positions.jsonl | 每帧定位结果，valid标记是否有有效坐标 |
| uart0_received.log | 对端发回的文本与PING消息 |
| active_run.json | 本次启动配置及来源；开机服务来源为sysv_autostart |

开机服务并不改变相机安装外参或串口协议。当前静态姿态仍仅用于台架，飞行中需要世界方向坐标时必须接入实时姿态。没有目标或PnP失败时发送valid=0和nan，仍然是有串口数据，不代表串口未启动。

## 上实机

接飞控稳压5V供电并保证供电能力，UART0的A16→飞控RX、A17←飞控TX，GND共地，信号电平3.3V TTL。电源接通后需等待Linux和相机/模型完成启动，不是接通电源瞬间就有有效目标坐标。接收端按XYZ包头、CRC、时间戳和valid识别有效测量。

独立运行时无需电脑USB。需要查看画面时，只打开电脑窗口即可，不必重启板端：

```bash
/home/jhh/code/vision_fly_detect/tools/show_camera.sh --view-only
```

如果修改了本地C++或YAML，仍按原流程编译并部署，普通show_camera.sh会重新部署启动；自启动入口在下次开机继续读取板端的新主配置。preview_camera.sh仍仅为纯预览，会暂时停止完整识别，不发送坐标。

## 验证范围

安装器已通过临时系统文件测试：首次备份、重复安装、原始程序与符号链接恢复、拒绝覆盖外部改动；服务通过本机测试进程验证：启动、重复启动不增加实例、SIGINT正常停止、PID文件错误时不停止其它进程。

实机重启验证记录保存于电脑工程`.local/autostart_verification.json`：新boot_id、识别进程、sysv_autostart来源、Launcher进程、UART0占用与发送计数及相机首帧。该记录验证软件开机行为，不代替飞控电源、外部UART接线和飞控协议解析的实际验证。


2026-11-17实机验证通过：boot_id由7caa890a-c9e6-4506-b54d-a0d57944132a变为737ee5a0-4ac4-48a2-8bc0-c8da3d260ec7；识别进程PID524由sysv_autostart启动，Launcher进程列表为空，UART0仅由PID524持有。1秒内UART0 TX从4818增加至5654，收到17578字节实际JPEG帧。重启后没有调用电脑端start命令启动识别，验证的是板端启动服务。
