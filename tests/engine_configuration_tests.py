"""Migration guards tested without launching engines or changing host projects."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from unreal_engine import resolve_editor_executable


class EngineSelectionTests(unittest.TestCase):
    def fixture(self, root, version):
        files = (
            'Engine/Build/Build.version',
            'Engine/Build/BatchFiles/Build.bat',
            'Engine/Source/UnrealGame.Target.cs',
            'Engine/Source/UnrealEditor.Target.cs',
            'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll',
            'Engine/Binaries/Win64/UnrealEditor-Cmd.exe',
        )
        for name in files:
            p = root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('', encoding='utf-8')
        (root / files[0]).write_text(json.dumps(dict(zip(
            ('MajorVersion', 'MinorVersion', 'PatchVersion'), version))), encoding='utf-8')

    def test_incomplete_install_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'not ready'):
                resolve_editor_executable(engine_root=directory)

    def test_old_engine_and_wrong_patch_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for version in ((5, 6, 1), (5, 8, 2)):
                self.fixture(Path(directory), version)
                with self.subTest(version=version), self.assertRaisesRegex(RuntimeError, 'version mismatch'):
                    resolve_editor_executable(engine_root=directory)

    def test_partial_install_with_binaries_but_no_target_rules_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            self.fixture(Path(directory), (5, 8, 3))
            (Path(directory) / 'Engine/Source/UnrealGame.Target.cs').unlink()
            with self.assertRaisesRegex(RuntimeError, 'missing.*UnrealGame.Target'):
                resolve_editor_executable(engine_root=directory)

    def test_explicit_engine_overrides_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            self.fixture(Path(directory), (5, 8, 3))
            with patch.dict(os.environ, {'CAMELOT_ENGINE_ROOT': 'unusable-install'}):
                result = resolve_editor_executable(engine_root=directory)
            self.assertEqual(result, Path(directory).resolve() / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')

    def test_missing_executable_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            self.fixture(Path(directory), (5, 8, 3))
            (Path(directory) / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe').unlink()
            with self.assertRaisesRegex(RuntimeError, 'missing.*UnrealEditor-Cmd'):
                resolve_editor_executable(engine_root=directory)


if __name__ == '__main__':
    unittest.main()
