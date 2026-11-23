#!/usr/bin/env python3
"""用临时文件系统验证启动修改可恢复，用本机测试进程验证服务不会误停其它程序。"""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BootTest(unittest.TestCase):
    def test_install_restore_and_foreign_change(self):
        installer = load('boot_install_test', 'board/install_boot.py')
        with tempfile.TemporaryDirectory(prefix='vision_boot_fs_') as directory:
            installer.SYSTEM_ROOT = Path(directory)
            def put(name, content):
                file = installer.path(name)
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(content)
                file.chmod(0o755)
                return file
            original_init = b'#!/bin/sh\necho "serial ready" > /dev/serial0\n(/maixapp/apps/launcher/launcher_daemon &)\n'
            put('/etc/init.d/S06maixapp', original_init)
            put('/maixapp/apps/launcher/launcher', b'original-launcher-binary')
            installer.path('/maixapp/apps/launcher/launcher_daemon').symlink_to('launcher')
            put(installer.APP_NAME + '/vision_fly_detect', b'app')
            put(installer.APP_NAME + '/board/S98vision_fly', (ROOT / 'board/S98vision_fly').read_bytes())
            put(installer.APP_NAME + '/configs/maixcam.yaml', b'user calibration must remain unchanged')
            before = {name: installer.fingerprint(installer.path(name)) for name in installer.TARGETS}
            with patch('os.sync'):
                installer.install()
                manifest = (installer.path(installer.BACKUP_NAME) / 'originals.json').read_bytes()
                self.assertIn(installer.TAG, installer.path(installer.TARGETS[1]).read_text())
                self.assertFalse(installer.path(installer.TARGETS[2]).is_symlink())
                self.assertTrue(installer.path(installer.TARGETS[-1]).exists())
                installer.install()
                self.assertEqual(manifest, (installer.path(installer.BACKUP_NAME) / 'originals.json').read_bytes())
                installer.restore()
                for name, sha in before.items():
                    self.assertEqual(installer.fingerprint(installer.path(name)), sha)
                self.assertEqual(os.readlink(installer.path(installer.TARGETS[2])), 'launcher')
                self.assertEqual(installer.path(installer.APP_NAME + '/configs/maixcam.yaml').read_bytes(),
                                 b'user calibration must remain unchanged')
                installer.install()
                installer.path(installer.TARGETS[0]).write_text('unrelated external change')
                with self.assertRaisesRegex(RuntimeError, '其它操作修改'):
                    installer.restore()
                self.assertEqual(installer.path(installer.TARGETS[0]).read_text(), 'unrelated external change')

    def test_service_lifecycle_and_pid_reuse(self):
        service = load('boot_service_test', 'board/vision_service.py')
        with tempfile.TemporaryDirectory(prefix='vision_boot_app_') as directory:
            service.APP = Path(directory)
            app = service.APP
            (app / 'configs').mkdir()
            (app / 'model.mud').write_text('test model')
            (app / 'configs/maixcam.yaml').write_text('%YAML:1.0\n---\nmodel_path: "model.mud"\npose_mode: "static"\nuart_enabled: 1\nuart_port: 0\n')
            source = app / 'dummy.c'
            source.write_text('#include <signal.h>\n#include <unistd.h>\nstatic volatile sig_atomic_t done=0;\nvoid stop(int s){(void)s;done=1;}\nint main(void){signal(SIGINT,stop);while(!done)usleep(10000);return 0;}\n')
            subprocess.run(['cc', str(source), '-o', str(app / 'vision_fly_detect')], check=True)
            foreign = subprocess.Popen(['/bin/sleep', '30'])
            try:
                service.start()
                pids = service.app_processes()
                self.assertEqual(len(pids), 1)
                service.start()
                self.assertEqual(service.app_processes(), pids)
                metadata = json.loads((app / 'active_run.json').read_text())
                self.assertEqual(metadata['uart_port'], 0)
                self.assertEqual(metadata['startup'], 'sysv_autostart')
                # 即便PID文件误指向其它进程，也只停止准确匹配的本工程程序。
                (app / 'infantry.pid').write_text(str(foreign.pid))
                service.stop()
                self.assertEqual(service.app_processes(), [])
                self.assertIsNone(foreign.poll())
                self.assertFalse((app / 'infantry.pid').exists())
                for pid in pids:
                    try: os.waitpid(pid, 0)
                    except ChildProcessError: pass
                service.stop()
            finally:
                service.stop()
                foreign.terminate(); foreign.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
