# nbnsd [![Build Status](https://travis-ci.org/cfstras/nbnsd.svg?branch=master)](https://travis-ci.org/cfstras/nbnsd)
Simple NetBIOS responder for Linux.
Makes your device reachable by name from any Windows Host.
Based on http://www.mostang.com/~davidm/nbnsd/

Contains simple installer for Raspbian.

## Downloads

You can download prebuild binaries [on the releases page](https://github.com/cfstras/nbnsd/releases).

## Quick install

```bash
# on arm
wget https://github.com/cfstras/nbnsd/releases/download/v1.1/nbnsd.arm -O /usr/bin/nbnsd
chmod a+x /usr/bin/nbnsd
wget https://raw.githubusercontent.com/cfstras/nbnsd/master/nbnsd.service -O /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now nbnsd
```

## Build & install

Setup with compile:

```bash
make
sudo make install   # installs systemd service
systemctl enable nbnsd.service
systemctl start nbnsd.service
```

You might want to modify the service file -- the packaged one adds a fallback
hostname using the CPU serial number as found in `/proc/cpuinfo` on many SoC
boards such as Raspberry Pis.

You can remove the `ExecStartPre=` line, and the `-m -n "${MACHINE_HOST}"` arguments.

## Tips
If you want to enable NetBIOS-resolving on your Linux box, follow these steps:

- install `smbclient` and `samba`
- in `/etc/nsswitch.conf`, add `wins` to the `hosts` line.
- in `/etc/samba/smbd.conf`, uncomment the line `name resolve order = wins lmhosts bcast`
- enable and start `winbindd`, `smbd`, and `nmbd`.

## License
[MIT](https://opensource.org/licenses/MIT)
