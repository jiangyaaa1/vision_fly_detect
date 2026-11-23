"""在访问默认USB地址前检查本机直连网络，避免请求误入VPN并报SSH握手错误。"""
import ipaddress
import json
import subprocess

USB_HOSTS = ('10.154.34.1', '10.154.35.1')


def check_usb_route(host, addresses, routes):
    if host not in USB_HOSTS:
        return  # 自定义Wi-Fi/以太网地址不按USB地址检查。
    target = ipaddress.ip_address(host)
    interfaces = set()
    for entry in addresses:
        for address in entry.get('addr_info', []):
            if address.get('family') != 'inet':
                continue
            interface = ipaddress.ip_interface(str(address['local']) + '/' + str(address['prefixlen']))
            if target in interface.network and interface.ip != target:
                interfaces.add(entry['ifname'])
    if not interfaces:
        raise RuntimeError(f'未检测到MaixCAM的USB网络（目标{host}）。请确认Lite供电，使用数据USB线直接连接电脑，等待板子开机后重试。')
    if routes and routes[0].get('dev') and routes[0]['dev'] not in interfaces:
        actual = routes[0]['dev']
        raise RuntimeError(f'MaixCAM的USB网络已出现，但{host}被路由到{actual}，没有走USB接口。请让VPN/代理直连该地址后重试。')


def ensure_usb_route(host):
    if host not in USB_HOSTS:
        return
    try:
        addresses = subprocess.run(['ip', '-j', 'address', 'show'], capture_output=True,
                                   text=True, timeout=3)
        routes = subprocess.run(['ip', '-j', 'route', 'get', host], capture_output=True,
                                text=True, timeout=3)
        if addresses.returncode:
            return  # 无ip工具或无查看权限时，由正常SSH流程提供诊断。
        address_data = json.loads(addresses.stdout)
        route_data = json.loads(routes.stdout) if routes.returncode == 0 else []
    except (OSError, subprocess.TimeoutExpired, ValueError):
        return
    check_usb_route(host, address_data, route_data)
