#!/usr/bin/env python3
"""Run only inside tests/run-isolated.sh: no access to the desktop Fcitx bus."""
import os
import sys
import time
import gi
from gi.repository import Gio, GLib

assert os.environ.get("SUMI_FCITX_ISOLATED_TEST") == "1"
SERVICE = "org.fcitx.Fcitx5"
PATH = "/org/fcitx/Fcitx5/SumiCandidates"
IFACE = "org.fcitx.Fcitx.SumiCandidates1"
IC = "org.fcitx.Fcitx.InputContext1"

def connection():
    return Gio.DBusConnection.new_for_address_sync(
        os.environ["DBUS_SESSION_BUS_ADDRESS"],
        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
        None, None)

def call(conn, path, interface, method, signature="()", args=()):
    return conn.call_sync(SERVICE, path, interface, method, GLib.Variant(signature, args),
                         None, Gio.DBusCallFlags.NONE, 2000, None).unpack()

def wait(predicate, timeout=3):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        while GLib.MainContext.default().iteration(False):
            pass
        if predicate():
            return
        time.sleep(.01)
    raise AssertionError("Timed out waiting for condition")

owner = connection()
for _ in range(100):
    try:
        call(owner, PATH, IFACE, "SetActive", "(sasb)", ("bad", [], True))
        break
    except GLib.Error:
        time.sleep(.03)
else:
    raise AssertionError("Addon did not start")

frames = []
revocations = []
owner.signal_subscribe(SERVICE, IFACE, "Revoked", PATH, None,
                      Gio.DBusSignalFlags.NONE, lambda *args: revocations.append(args[5].unpack()[0]))
owner.signal_subscribe(SERVICE, IFACE, "Candidates", PATH, None,
                      Gio.DBusSignalFlags.NONE, lambda *args: frames.append(args[5].unpack()))
client = connection()
commits = []
preedits = []
client_panels = []
client.signal_subscribe(SERVICE, IC, "UpdateClientSideUI", None, None,
                       Gio.DBusSignalFlags.NONE, lambda *args: client_panels.append(args[5].unpack()))
client.signal_subscribe(SERVICE, IC, "UpdateFormattedPreedit", None, None,
                       Gio.DBusSignalFlags.NONE, lambda *args: preedits.append(args[5].unpack()))
client.signal_subscribe(SERVICE, IC, "CommitString", None, None,
                       Gio.DBusSignalFlags.NONE, lambda *args: commits.append(args[5].unpack()[0]))

def create(program):
    path, _ = call(client, "/org/freedesktop/portal/inputmethod", "org.fcitx.Fcitx.InputMethod1",
                   "CreateInputContext", "(a(ss))", ([("program", program), ("display", "wayland:test")],))
    call(client, path, IC, "SetCapability", "(t)", ((1 << 1) | (1 << 4),))
    return path

def key(path, char):
    call(client, path, IC, "ProcessKeyEvent", "(uuubu)", (ord(char), 0, 0, False, 0))

def frame():
    wait(lambda: frames and frames[-1][2])
    return frames[-1]

assert call(owner, PATH, IFACE, "SetActive", "(sasb)", ("bad", ["*"], True)) == (False,)
assert call(owner, PATH, IFACE, "SetActive", "(sasb)", ("editor", ["sumi-test"], True)) == (True,)
ic = create("sumi-test")
call(client, ic, IC, "FocusIn")
call(client, "/controller", "org.fcitx.Fcitx.Controller1", "SetCurrentIM", "(s)", ("pinyin",))
call(client, "/controller", "org.fcitx.Fcitx.Controller1", "Activate")
for char in "nihao":
    key(ic, char)
f = frame()
assert f[0] == "editor" and any("你好" in row[2] for row in f[2]), f
assert any(items for items, cursor in preedits), preedits
print("PASS: real pinyin candidates, inline preedit chain preserved")
# Unrelated sender and stale frame cannot select a candidate.
intruder = connection()
call(intruder, PATH, IFACE, "Select", "(sti)", ("editor", f[1], f[2][0][0]))
call(owner, PATH, IFACE, "Select", "(sti)", ("editor", f[1] - 1, f[2][0][0]))
assert not commits
call(owner, PATH, IFACE, "Select", "(sti)", ("editor", f[1], f[2][0][0]))
wait(lambda: commits)
assert "你好" in commits[-1], commits
print("PASS: owner validation, stale serial rejection, real candidate commit")
# Engine resets its panel on commit; next composition must still be captured.
frames.clear()
key(ic, "n")
f = frame()
assert f[1] > 1
if f[5]:
    old_serial = f[1]
    call(owner, PATH, IFACE, "Page", "(stb)", ("editor", old_serial, True))
    wait(lambda: frames[-1][1] > old_serial)
    assert frames[-1][4]
print("PASS: callback reattached after panel reset and candidate paging")
# The lease ends on focus out; another widget of the same program is not captured.
call(client, ic, IC, "FocusOut")
wait(lambda: frames and not frames[-1][2] and "editor" in revocations)
frames.clear()
ic2 = create("sumi-test")
call(client, ic2, IC, "FocusIn")
key(ic2, "n")
time.sleep(.05)
while GLib.MainContext.default().iteration(False):
    pass
assert not frames
print("PASS: focus loss revokes exact IC and does not capture sibling editor")
# Destroy and owner disconnect both free the lease.
assert call(owner, PATH, IFACE, "SetActive", "(sasb)", ("editor2", ["sumi-test"], True)) == (True,)
call(client, ic2, IC, "DestroyIC")
wait(lambda: "editor2" in revocations)
assert call(owner, PATH, IFACE, "SetActive", "(sasb)", ("pending", ["other-program"], True)) == (True,)
owner.close_sync(None)
wait(lambda: call(intruder, PATH, IFACE, "SetActive", "(sasb)", ("replacement", ["sumi-test"], True)) == (True,))
assert call(intruder, PATH, IFACE, "SetActive", "(sasb)", ("replacement", [], False)) == (True,)
print("PASS: destroyed IC and disconnected owner release the lease")
# GTK client-side popups cannot be hidden through ClassicUI. A late request
# must fail without disturbing composition, then succeed after normal reset.
ic3 = create("sumi-client-side")
call(client, ic3, IC, "SetCapability", "(t)", ((1 << 1) | (1 << 4) | (1 << 39),))
call(client, ic3, IC, "FocusIn")
call(client, "/controller", "org.fcitx.Fcitx.Controller1", "Activate")
key(ic3, "n")
wait(lambda: client_panels and client_panels[-1][4])
assert call(intruder, PATH, IFACE, "SetActive", "(sasb)",
            ("client-side", ["sumi-client-side"], True)) == (False,)
commit_count = len(commits)
key(ic3, " ")
wait(lambda: len(commits) > commit_count)
call(client, ic3, IC, "Reset")
assert call(intruder, PATH, IFACE, "SetActive", "(sasb)",
            ("client-side-next", ["sumi-client-side"], True)) == (True,)
assert call(intruder, PATH, IFACE, "SetActive", "(sasb)",
            ("client-side-next", [], False)) == (True,)
print("PASS: existing GTK client-side popup preserved, retry after reset accepted")
client.close_sync(None)
intruder.close_sync(None)
