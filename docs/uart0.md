# UART0相对坐标收发（当前默认接口）

当前configs/maixcam.yaml已配置uart_port: 0，UART0设备/dev/ttyS0，A16发送、A17接收。TX接对端RX，RX接对端TX，GND共地，使用3.3V TTL、115200波特、8N1。

```yaml
uart_enabled: 1
uart_port: 0
uart_baudrate: 115200
uart_rx_log: "uart0_received.log"
```

串口默认发送机体FRD相对坐标，单位米，x前/y右/z下。坐标协议、CRC、PING/PONG、接收日志与电脑测试脚本沿用[串口完整协议](uart3.md)；该文档中的UART3引脚仅用于uart_port=3，当前UART0请使用本页A16/A17接线。

UART0可能用作系统终端，部署前需要检查/proc/cmdline、/sys/class/tty/console/active以及是否有getty占用。官方固件中注释或删除/boot/uEnv.txt里的consoledev可能启用UART0终端，不要把该操作当作释放串口的方法。

本次已确认板载Wi-Fi驱动占用SDIO1，选择UART3会在相机启动前报错。改回UART0可避开该引脚冲突，不必关闭Wi-Fi。电脑窗口现会显示板端真实错误，完整启动记录保存在.local/last_startup.log。

重新部署启动：

```bash
/home/jhh/code/vision_fly_detect/tools/show_camera.sh
```

切换串口编号只需改YAML并重新部署，不需要重新编译；首次更新包含新功能的C++程序时需要编译。


## 2026-11-17板端发送验证

冷启动后识别程序未运行，UART0由`/maixapp/apps/launcher/launcher daemon`占用，两秒内驱动TX计数维持14不变。已修复启动脚本对新旧Launcher命令的识别，以及断电后PID复用导致无法启动的问题；只按本工程准确可执行路径处理进程，不停止无关程序。

启动完整识别后，UART0_READY日志确认/dev/ttyS0、A16发送/A17接收、115200、body坐标。两秒内/proc/tty/driver/serial的UART0 TX从46238增加到47940（增加1702字节），RX为0；UART0只由本工程识别程序占用，无UART0_ERROR。

本次场景no_target，所以发送无效包（valid=0、x/y/z为nan），不能当作已经输出有效目标坐标。发送计数验证了板端驱动持续发送，未验证飞控引脚接收、通信协议解析或实际定位精度。完整记录位于.local/uart0_tx_verification.json。

纯预览preview_camera.sh不发送坐标；完整流程用tools/show_camera.sh启动。后续已配置开机自启动及Launcher禁用，详见[自启动说明](autostart.md)。
