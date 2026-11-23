# UART3收发相对坐标

UART3作为可选接口保留，当前主配置已切回UART0（uart_port=0），详见[UART0说明](uart0.md)。本页的UART3接线与示例仅用于明确选择uart_port=3且已释放SDIO引脚的场景。视觉定位每帧向串口线程提交当前结果；独立串口线程发送最新坐标并接收文本，不在图像推理线程等待串口收发。历史检测配置未开启串口。

## 接线与串口

MaixCAM Lite使用Linux设备`/dev/ttyS3`。程序通过MaixCDK `pinmap::set_pin_function`将P19设为UART3_TX、P20设为UART3_RX。程序同时设置发送和接收引脚的复用功能。

| Lite引脚 | 对端接线 |
|---|---|
| P19，UART3_TX | 对端RX |
| P20，UART3_RX | 对端TX |
| GND | 对端GND |

使用3.3V TTL电平，不接RS232正负电压或5V信号。请按Lite引脚图查找P19/P20，USB网络地址不是串口设备；UART3不是USB转接板上的UART0 TX/RX接口。对端串口名称与板端UART3不需要相同，只需TX/RX交叉并匹配波特率。

官方依据：[MaixCDK MaixCAM引脚映射源码](https://github.com/sipeed/MaixCDK/blob/main/components/peripheral/port/maixcam/maix_pinmap.cpp)、[UART说明](https://wiki.sipeed.com/maixpy/doc/zh/peripheral/uart.html)。底层传输使用Linux原始串口的非阻塞读写与poll，支持半包写入和错误检查；采集、NN及引脚复用继续使用MaixCDK。

默认115200波特，8数据位、无校验、1停止位，无硬件/软件流控。接入另一个单片机时，也配置为115200、8N1。

## UART3与Wi-Fi的引脚冲突

P19/P20也分别是SDIO1_D2/SDIO1_D1。用户提供的引脚图底部注明：Wi-Fi版本的P18～P23用于连接Wi-Fi模块，应避免作为普通IO使用。因此UART3不能与这组SDIO Wi-Fi同时工作；只修改串口设备名或仅把wlan0设为down，不代表硬件线路已经释放。

程序在改写P19/P20复用之前检查`/sys/bus/sdio/devices`。发现SDIO设备时会明确报错并停止本次启动，不自动卸载驱动、关闭Wi-Fi或改写系统启动配置。即使未发现SDIO设备，也需要按你的板卡版本及原理图确认P19/P20已引出且未被其它电路驱动。Wi-Fi版本若未完成硬件释放，请通过配置选择其它可用串口。

官方依据：[MaixCDK引脚映射](https://github.com/sipeed/MaixCDK/blob/main/components/peripheral/port/maixcam/maix_pinmap.cpp)、[官方PINMAP说明](https://wiki.sipeed.com/maixpy/doc/en/peripheral/pinmap.html)。

## 中文配置

编辑`configs/maixcam.yaml`末尾：

```yaml
uart_enabled: 1
uart_port: 3
uart_baudrate: 115200
uart_tx_hz: 20
uart_stale_ms: 300
uart_coordinate_frame: "body"
uart_rx_log: "uart3_received.log"
```

`uart_port`选择MaixCAM端串口编号，修改后重启即可，无需重新编译：

| 编号 | 设备 | TX | RX |
|---|---|---|---|
| 0 | /dev/ttyS0 | A16 | A17 |
| 1 | /dev/ttyS1 | A19 | A18 |
| 2 | /dev/ttyS2 | A28 | A29 |
| 3 | /dev/ttyS3 | P19 | P20 |

UART0需要检查系统终端占用；UART3需要检查SDIO/Wi-Fi冲突。对端单片机的USART编号可以不同，不影响交叉接线。uart_rx_log是独立文件名，切换端口时可一并修改。

`uart_tx_hz`是消息发送频率，与识别帧率不同。识别低于20帧时可能重复发送同一图像时间戳的坐标，不能当作新的定位测量；结果超过300毫秒没有更新时发送无效包。没有目标、PnP失败时也发送无效包，不使用上一次目标坐标或零坐标冒充有效结果。

坐标单位米，默认`body`为机体原点→目标的相对位移，标记FRD，x前、y右、z下。`camera`从相机光心出发，标记CAM，x右、y下、z前。

需要世界方向的相对位移时改为`world_relative`，发送机体原点→目标的NED位移，而非目标绝对位置。必须有有效姿态；没有姿态时发送valid=0。当前主配置使用静态姿态时标记NED_STATIC，只能用于静止台架，飞行时需要实时姿态输入。默认body坐标不依赖世界姿态。

串口接收当前只记录文本、处理PING，不修改相机、姿态或目标筛选设置。实时姿态输入仍沿用既有UDP接口。

## 发送协议

ASCII文本，以CRLF（字节0D 0A）结尾：

```text
XYZ,seq,timestamp_ns,valid,frame,x,y,z*HHHH\r\n
```

| 字段 | 含义 |
|---|---|
| seq | uint32发送序号，从0开始，溢出回到0，进程重启也从0开始 |
| timestamp_ns | 图像时间戳，板端单调时钟纳秒；启动或结果过期时的无效心跳使用当前单调时间 |
| valid | 1表示当前坐标有效，0表示不可使用坐标 |
| frame | FRD、CAM、NED或NED_STATIC |
| x,y,z | 米，四位小数；valid=0时均为小写nan |
| HHHH | CRC-16/CCITT-FALSE，4位大写十六进制 |

CRC参数：poly=0x1021、init=0xFFFF、RefIn=false、RefOut=false、XorOut=0。校验覆盖从`XYZ`的X到星号前最后一个字符，不包含星号、校验字符或CRLF。标准向量`123456789`结果0x29B1。坐标的小数位数不代表实际测距精度。

接收端按换行拆包，验证CRC、valid与frame后再使用坐标。连续300毫秒收不到新图像时间戳的有效结果，应清除旧坐标；收到无效包应立即清除。接收端不要因相同时间戳被重复发送就延长旧测量的有效期。串口断线或程序退出可能收不到最后一包，因此接收端也需要超时处理。

## 接收文本和PING

对端发送`PING\n`或`PING\r\n`，板端回复`PONG\r\n`。PING/PONG为普通文本控制消息，不带坐标CRC。

其它单行文本同样接收并写入`uart3_received.log`，每行包含本板接收时间戳和转义后的文本。单行最多256字节（含可选结尾CR）；超长内容整行丢弃，到下一个换行后恢复，不把残留后缀当成新指令。换行之前收到的半行会等待后续字节。接收日志也同步输出到`infantry.log`中的UART3行。

## 编译与启动

```bash
cd /home/jhh/code/vision_fly_detect
./tools/build_maixcam.sh
./tools/show_camera.sh
```

新C++功能需要编译。编译后点窗口“启动板端”也可以部署；`--view-only`仅查看，不更新板端程序。串口参数修改需要重新启动板端，曝光的实时应用按钮只更新曝光。

板端启动日志出现`UART3_READY /dev/ttyS3 TX=P19 RX=P20 ...`表示软件打开并配置了设备，不代表外部接线已经通过收发验证。发生运行时串口故障会打印`UART3_ERROR`并停止该串口线程，视觉流程继续运行；修复连接后重新启动程序。

## 电脑USB转TTL测试

把电脑USB转TTL适配器按上表连接到板子。下面的port是电脑转接器设备，不能填写板端的`/dev/ttyS3`或USB网络地址：

```bash
cd /home/jhh/code/vision_fly_detect
/usr/bin/python3 -s tools/uart_monitor.py --port /dev/ttyUSB0 --ping --send "hello MaixCAM"
```

脚本依赖pyserial，使用电脑可用的相应Python环境；如缺少，可在虚拟环境安装`pyserial`后运行。它校验坐标CRC并输出JSON，收到PONG说明双向通信成功。没有目标时应看到valid=false、xyz=null。板端日志应包含PING和hello MaixCAM。

不要同时启动两个进程读取同一个串口，也不要在视觉程序已占用相机时再开第二个相机进程。
