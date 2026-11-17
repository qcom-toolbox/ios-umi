#!/usr/bin/env python3
"""Put the SWEET phone add-ons straight into a root image, so a fresh install boots fully working
(what the 2026-11-17 reinstall had to push over USB afterwards, NOTES.md).

  sudo tools/sweet-root-extras.py MOUNTED_ROOT [--no-chown]

Run after tools/sweet-root-tweaks.py (tools/mkiosroot-mac.sh does both). Sources (none of them in git:
firmware and builds stay on this Mac):
  GPU      SweetMetal2.bundle: $SWEET_M2V (default ~/ios-sweet-port/xnu-work/work/m2v-bridge-src):
           Info.plist, out-usecount/.../SweetMetal2, out-apollo/libvulkan_freedreno.dylib (Turnip 0004),
           out-limits/.../_CodeSignature/CodeResources; /var/local config (mtl2-env, mtl2-tu-debug, mtl2-clients)
  libs     phone/out: libsweet-sb, libsweet-island, libsweet-bt (/usr/local/lib), sweet-telephonyd (/usr/local/bin),
           SweetAudio.driver (/System/Library/Audio/Plug-Ins/HAL)
  firmware research/slpi-fw/umi, research/audio-umi/{adsp-fw,rfsa-adsp,vendor-init/adsp_avs_config.acdb},
           research/sensors-umi/{vendor-sensors,persist-sensors,soc0} -> /private/var/sweet/...
  launchd  backboardd CA_DISABLE_{DETACHED,DIRTY_REGIONS,GAIN_MAP_LAYERS,FRAMEBUFFER_COMPRESSION}=1;
           adprivacyd, promotedcontentd, fairplayd.H2, vmd removed; com.sweet.telephonyd added
  audio    VirtualAudio.plugin back from HAL.disabled (SweetAudio provides its devices)
  gestalt  the MobileGestalt cache with the baseband answers (no SOS): ~/ios-umi-backup/mg-cache-2026-11-10 +
           the 5 bytes of 2026-11-10 (only valid for build 20H392, CacheUUID 42F6FA57-...)
  network  a "Wi-Fi" service on en0 (DHCP) in SystemConfiguration/preferences.plist: on the 2026-11-17 reinstall
           configd never named any interface (NetworkInterfaces.plist empty) and Wi-Fi stayed "connecting"
  apps     data migration left to run at the first boot (com.apple.migration.plist removed) without
           CoreLocationMigrator (it hangs on locationd): installs the 31 staged apps (~3 min, progress bar)
A missing source is reported and skipped. --no-chown: test as a normal user (ownership not set).
"""
import os, plistlib, shutil, sys

HOME = os.path.expanduser('~')
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M2V = os.environ.get('SWEET_M2V', os.path.join(HOME, 'ios-sweet-port/xnu-work/work/m2v-bridge-src'))
MG_BACKUP = os.path.join(HOME, 'ios-umi-backup/mg-cache-2026-11-10/com.apple.MobileGestalt.plist')
MG_UUID = '42F6FA57-C5A1-4134-9BF4-AB00F962EAE8'
MG_BYTES = {320: 3, 560: 1, 632: 1, 2600: 1, 4792: 1}
CHOWN = True
missing = []


def own(path, uid, gid, mode):
    if CHOWN:
        os.chown(path, uid, gid, follow_symlinks=False)
    os.chmod(path, mode)


def mkdir(root, p, mode=0o755, uid=0, gid=0):
    full = os.path.join(root, p.lstrip('/'))
    os.makedirs(full, exist_ok=True)
    own(full, uid, gid, mode)


def put(root, p, src=None, data=None, mode=0o644, uid=0, gid=0):
    full = os.path.join(root, p.lstrip('/'))
    if src is not None and not os.path.isfile(src):
        missing.append(src)
        print('  MISSING', src)
        return
    os.makedirs(os.path.dirname(full), exist_ok=True)
    if os.path.lexists(full):
        os.unlink(full)
    if src is not None:
        shutil.copyfile(src, full)
    else:
        open(full, 'wb').write(data)
    own(full, uid, gid, mode)


def tree(root, src, dst, mode=0o644):
    if not os.path.isdir(src):
        missing.append(src)
        print('  MISSING', src)
        return
    mkdir(root, dst)
    for d, dirs, files in os.walk(src):
        rel = os.path.relpath(d, src)
        base = dst if rel == '.' else dst + '/' + rel
        mkdir(root, base)
        for n in files:
            put(root, base + '/' + n, src=os.path.join(d, n), mode=mode)


def gpu(root):
    print('GPU: SweetMetal2.bundle + /var/local')
    b = '/System/Library/Extensions/SweetMetal2.bundle'
    mkdir(root, b)
    mkdir(root, b + '/_CodeSignature')
    put(root, b + '/Info.plist', src=os.path.join(M2V, 'Info.plist'))
    put(root, b + '/SweetMetal2', src=os.path.join(M2V, 'out-usecount/SweetMetal2.bundle/SweetMetal2'), mode=0o755)
    put(root, b + '/libvulkan_freedreno.dylib', src=os.path.join(M2V, 'out-apollo/libvulkan_freedreno.dylib'), mode=0o755)
    put(root, b + '/_CodeSignature/CodeResources',
        src=os.path.join(M2V, 'out-limits/SweetMetal2.bundle/_CodeSignature/CodeResources'))
    mkdir(root, '/private/var/local', 0o777)
    mkdir(root, '/private/var/local/m2v-cache', 0o777)
    put(root, '/private/var/local/mtl2-env', data=b'TU_DARWIN_SERVICE=SweetAdreno\n')
    put(root, '/private/var/local/mtl2-tu-debug', data=b'noubwc\n')
    put(root, '/private/var/local/mtl2-clients', data=b'*,-FitnessWidget,-chronod\n')
    put(root, '/private/var/local/mtl2.log', data=b'', mode=0o666)


def libs(root):
    print('libraries, sweet-telephonyd, SweetAudio.driver')
    out = os.path.join(REPO, 'phone/out')
    mkdir(root, '/usr/local')
    mkdir(root, '/usr/local/lib')
    mkdir(root, '/usr/local/bin')
    for n in ('libsweet-sb.dylib', 'libsweet-island.dylib', 'libsweet-bt.dylib'):
        put(root, '/usr/local/lib/' + n, src=os.path.join(out, n), mode=0o755)
    put(root, '/usr/local/bin/sweet-telephonyd', src=os.path.join(out, 'sweet-telephonyd'), mode=0o755)
    hal = '/System/Library/Audio/Plug-Ins/HAL/SweetAudio.driver'
    mkdir(root, hal)
    put(root, hal + '/Info.plist', src=os.path.join(out, 'SweetAudio.driver/Info.plist'))
    put(root, hal + '/SweetAudio', src=os.path.join(out, 'SweetAudio.driver/SweetAudio'), mode=0o755)
    va_off = os.path.join(root, 'Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin')
    va_on = os.path.join(root, 'Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin')
    if os.path.isdir(va_off) and not os.path.exists(va_on):
        os.makedirs(os.path.dirname(va_on), exist_ok=True)
        shutil.copytree(va_off, va_on, symlinks=True)
        print('  VirtualAudio.plugin copied back to /Library/Audio/Plug-Ins/HAL')


def firmware(root):
    print('firmware -> /private/var/sweet')
    r = os.path.join(REPO, 'research')
    mkdir(root, '/private/var/sweet')
    tree(root, os.path.join(r, 'slpi-fw/umi'), '/private/var/sweet/slpi')
    tree(root, os.path.join(r, 'audio-umi/adsp-fw'), '/private/var/sweet/adsp')
    mkdir(root, '/private/var/sweet/adsp-rpc')
    tree(root, os.path.join(r, 'audio-umi/rfsa-adsp'), '/private/var/sweet/adsp-rpc/adsp')
    mkdir(root, '/private/var/sweet/adsp-rpc/acdb')
    put(root, '/private/var/sweet/adsp-rpc/acdb/adsp_avs_config.acdb',
        src=os.path.join(r, 'audio-umi/vendor-init/adsp_avs_config.acdb'))
    mkdir(root, '/private/var/sweet/sensors')
    for sub in ('vendor-sensors', 'persist-sensors', 'soc0'):
        tree(root, os.path.join(r, 'sensors-umi', sub), '/private/var/sweet/sensors/' + sub)


def launchd(root):
    print('launchd.plist: backboardd CA settings, jobs')
    path = os.path.join(root, 'System/Library/xpc/launchd.plist')
    d = plistlib.load(open(path, 'rb'))
    L, P = d['LaunchDaemons'], '/System/Library/LaunchDaemons/'
    for j in ('com.apple.ap.adprivacyd', 'com.apple.ap.promotedcontentd', 'com.apple.fairplayd.H2',
              'com.apple.voicemail.vmd'):
        L.pop(P + j + '.plist', None)
    L[P + 'com.apple.backboardd.plist'].setdefault('EnvironmentVariables', {}).update({
        'CA_DISABLE_DETACHED': '1', 'CA_DISABLE_DIRTY_REGIONS': '1', 'CA_DISABLE_GAIN_MAP_LAYERS': '1',
        'CA_DISABLE_FRAMEBUFFER_COMPRESSION': '1'})
    job = os.path.join(REPO, 'phone/sweet-telephonyd/com.sweet.telephonyd.plist')
    L[P + 'com.sweet.telephonyd.plist'] = plistlib.load(open(job, 'rb'))
    st = os.stat(path)
    with open(path, 'wb') as f:
        plistlib.dump(d, f, fmt=plistlib.FMT_BINARY)
    own(path, st.st_uid, st.st_gid, st.st_mode & 0o7777)


def gestalt(root):
    print('MobileGestalt cache with the baseband answers')
    if not os.path.isfile(MG_BACKUP):
        missing.append(MG_BACKUP)
        print('  MISSING', MG_BACKUP)
        return
    mg = plistlib.load(open(MG_BACKUP, 'rb'))
    if mg.get('CacheUUID') != MG_UUID or mg.get('CacheVersion') != '20H392':
        print('  skipped: the backup is not for this build')
        return
    cd = bytearray(mg['CacheData'])
    for off, v in MG_BYTES.items():
        cd[off] = v
    mg['CacheData'] = bytes(cd)
    d = '/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches'
    full = os.path.join(root, d.lstrip('/'))
    os.makedirs(full, exist_ok=True)
    put(root, d + '/com.apple.MobileGestalt.plist', data=plistlib.dumps(mg, fmt=plistlib.FMT_BINARY))


def network(root):
    print('network: Wi-Fi service on en0 (DHCP)')
    import uuid
    path = os.path.join(root, 'private/var/preferences/SystemConfiguration/preferences.plist')
    d = plistlib.load(open(path, 'rb')) if os.path.exists(path) else {'__VERSION__': 20191120}
    if d.get('NetworkServices'):
        print('  preferences.plist already has services: left alone')
        return
    svc, st = str(uuid.uuid4()).upper(), str(uuid.uuid4()).upper()
    d['NetworkServices'] = {svc: {
        'DNS': {}, 'IPv4': {'ConfigMethod': 'DHCP'}, 'IPv6': {'ConfigMethod': 'Automatic'},
        'Interface': {'DeviceName': 'en0', 'Hardware': 'AirPort', 'Type': 'Ethernet', 'UserDefinedName': 'Wi-Fi'},
        'Proxies': {'ExceptionsList': ['*.local', '169.254/16'], 'FTPPassive': 1}, 'UserDefinedName': 'Wi-Fi'}}
    d['Sets'] = {st: {'Network': {'Global': {'IPv4': {'ServiceOrder': [svc]}}, 'Interface': {'en0': {'AirPort': {}}},
                                  'Service': {svc: {'__LINK__': '/NetworkServices/' + svc}}},
                      'UserDefinedName': 'Automatic'}}
    d['CurrentSet'] = '/Sets/' + st
    os.makedirs(os.path.dirname(path), exist_ok=True)
    put(root, '/private/var/preferences/SystemConfiguration/preferences.plist',
        data=plistlib.dumps(d, fmt=plistlib.FMT_BINARY))


def apps(root):
    print('apps: data migration at the first boot, without CoreLocationMigrator')
    for p in ('private/var/mobile/Library/Preferences/com.apple.migration.plist',
              'System/Library/DataClassMigrators/CoreLocationMigrator.migrator/CoreLocationMigrator'):
        full = os.path.join(root, p)
        if os.path.lexists(full):
            os.unlink(full)
            print('  removed /' + p)


def main():
    global CHOWN
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    CHOWN = '--no-chown' not in sys.argv
    if len(args) != 1 or not os.path.isdir(os.path.join(args[0], 'System/Library/xpc')):
        sys.exit(__doc__)
    root = args[0]
    gpu(root)
    libs(root)
    firmware(root)
    launchd(root)
    gestalt(root)
    network(root)
    apps(root)
    if missing:
        print('%d source(s) missing (skipped):' % len(missing))
        for m in missing:
            print('  ' + m)
    else:
        print('all add-ons in place')


if __name__ == '__main__':
    main()
