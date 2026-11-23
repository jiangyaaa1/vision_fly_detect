"""将板端启动诊断显示在电脑窗口，保留硬件错误，避免统一误报USB故障。"""


def board_failure_detail(log):
    lines = log.replace('\x00', '').splitlines()
    for line in reversed(lines):
        if 'Exception:' in line:
            return line.split('Exception:', 1)[1].strip()
    for line in reversed(lines):
        if '_ERROR' in line or '[E]' in line:
            return line.strip()
    return ''


def startup_error_message(stdout, stderr):
    combined = (stdout + '\n' + stderr).replace('\x00', '')
    if 'UART3' in combined and 'SDIO' in combined:
        return ('启动失败：UART3的P19/P20与板载Wi-Fi（SDIO）引脚冲突。'
                '请先确认硬件线路已释放，或选择其它串口；也可暂设uart_enabled: 0恢复识别。')
    detail = board_failure_detail(combined)
    if detail:
        return '启动失败：' + detail
    useful = [line.strip() for line in stderr.splitlines() if line.strip()]
    if useful:
        return '启动失败：' + useful[-1]
    useful = [line.strip() for line in stdout.splitlines() if line.strip()]
    return '启动失败：' + (useful[-1] if useful else '未返回诊断信息，请查看.local/last_startup.log')
