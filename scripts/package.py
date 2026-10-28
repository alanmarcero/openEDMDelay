"""Package native bundles, exact-source pointers and dependency notices."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import zipfile

platform = sys.argv[1]
root = Path(__file__).resolve().parent.parent
build = root / 'build'
artifacts = build / 'openEDMDelay_artefacts' / 'Release'
package = build / 'package'
if package.exists():
    shutil.rmtree(package)
package.mkdir()
formats = [('VST3', 'openEDMDelay.vst3')]
if platform.startswith('macOS'):
    formats += [('AU', 'openEDMDelay.component'), ('Standalone', 'openEDMDelay.app')]
else:
    formats += [('Standalone', 'openEDMDelay.exe' if platform.startswith('Windows') else 'openEDMDelay')]
for folder, name in formats:
    source = artifacts / folder / name
    if source.is_dir():
        shutil.copytree(source, package / name)
    else:
        shutil.copy2(source, package / name)
shutil.copytree(root / 'licenses', package / 'licenses')
for name in ['LICENSE', 'THIRD_PARTY.md', 'README.md']:
    shutil.copy2(root / name, package / name)
juce = Path(os.environ.get('JUCE_SOURCE_DIR', str(build / '_deps' / 'juce-src')))
if not (juce / 'LICENSE.md').is_file():
    raise SystemExit('JUCE license missing; refusing to publish without notices')
for source in juce.rglob('*'):
    if source.is_file() and any(term in source.name.lower() for term in ['license', 'licence', 'copying', 'notice', 'copyright']):
        target = package / 'licenses' / 'JUCE' / source.relative_to(juce)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
for relative in ['modules/juce_graphics/image_formats/jpglib/README', 'modules/juce_core/zip/zlib/README']:
    target = package / 'licenses' / 'JUCE' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(juce / relative, target)
sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
(package / 'SOURCE.txt').write_text(f'openEDMDelay source and build instructions: https://github.com/alanmarcero/openEDMDelay/tree/{sha}\nJUCE 8.0.9 source: https://github.com/juce-framework/JUCE/tree/8.0.9\nmacOS builds are ad hoc signed, not notarized.\n', encoding='utf-8')
(root / 'dist').mkdir(exist_ok=True)
archive = root / 'dist' / f'openEDMDelay-{platform}.zip'
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as output:
    for source in sorted(package.rglob('*')):
        if source.is_file():
            output.write(source, source.relative_to(package))
print(archive)
