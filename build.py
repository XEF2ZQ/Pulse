"""Build with the installed MSVC x64 toolchain and Windows SDK; no downloads."""
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent
vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
vs = Path(subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip())
msvc = sorted((vs/'VC/Tools/MSVC').iterdir())[-1]
sdk = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))/'Windows Kits/10'
sdkver = sorted((sdk/'Include').iterdir())[-1].name
# Windows environment names are case-insensitive. Some launchers supply both
# Path and PATH, which MSBuild rejects, so normalize keys for child processes.
env = {key.upper(): value for key, value in os.environ.items()}
env['PATH'] = str(msvc/'bin/Hostx64/x64')+';'+str(sdk/'bin'/sdkver/'x64')+';'+env.get('PATH','')
env['INCLUDE'] = ';'.join(map(str,[msvc/'include',sdk/'Include'/sdkver/'ucrt',sdk/'Include'/sdkver/'shared',sdk/'Include'/sdkver/'um']))
env['LIB'] = ';'.join(map(str,[msvc/'lib/x64',sdk/'Lib'/sdkver/'ucrt/x64',sdk/'Lib'/sdkver/'um/x64']))
build = root/'build-native'
ninja = shutil.which('ninja.exe')
if not ninja:
    ninja = str(vs/'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')
subprocess.run(['cmake','-S',str(root),'-B',str(build),'-G','Ninja',f'-DCMAKE_MAKE_PROGRAM={ninja}','-DCMAKE_CXX_COMPILER=cl.exe','-DCMAKE_BUILD_TYPE=Release'],env=env,check=True)
subprocess.run(['cmake','--build',str(build),'--parallel','2'],env=env,check=True)
subprocess.run(['ctest','--test-dir',str(build),'--output-on-failure'],env=env,check=True)
