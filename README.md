# nbnsd
Simple NetBIOS responder for Linux.
Makes your device reachable by name from any Windows Host.
Based on http://www.mostang.com/~davidm/nbnsd/

Quick setup:

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

# Tips
If you want to enable NetBIOS-resolving on your Linux box, follow these steps:

- install `smbclient` and `samba`
- in `/etc/nsswitch.conf`, add `wins` to the `hosts` line.
- in `/etc/samba/smbd.conf`, uncomment the line `name resolve order = wins lmhosts bcast`
- enable and start `winbindd`, `smbd`, and `nmbd`.

# License
[MIT](https://opensource.org/licenses/MIT)
