"""
probe_gui.py
============
Standalone GUI for the Antarctic Probe BLE data transfer system.
Built with CustomTkinter. Calls probe_interface.py for all BLE operations.

Theme colours are controlled by three global variables at the top of this file.
"""

import asyncio
import threading
import tkinter as tk
from datetime import datetime
from pathlib import Path

import customtkinter as ctk

# ══════════════════════════════════════════════════════════════════════════════
#  THEME — change these three variables to restyle the entire app
# ══════════════════════════════════════════════════════════════════════════════
COLOUR_TOOLBAR   = "#1A3A5C"   # Top bar + side panel background (deep navy blue)
COLOUR_APP_BG    = "#B2D8D8"   # Main window background (soft cyan)
COLOUR_PANEL     = "#F5F0E8"   # Data area + terminal background (warm off-white)

# Derived accent colours — computed from the three above, no need to change
COLOUR_TOOLBAR_TEXT  = "#FFFFFF"
COLOUR_TOOLBAR_HOVER = "#2A5A8C"
COLOUR_PANEL_TEXT    = "#1A1A2E"
COLOUR_TERMINAL_TEXT = "#2C5F2E"
COLOUR_STATUS_ON     = "#00E676"
COLOUR_STATUS_OFF    = "#EF5350"
COLOUR_ACCENT        = "#1E90FF"

# ══════════════════════════════════════════════════════════════════════════════
#  Mock probe interface — replace with real imports once BLE is ready
# ══════════════════════════════════════════════════════════════════════════════
import random, time

MOCK_DEVICES = ["AntarcticProbe", "AntarcticProbe-Dev", "TestProbe-001"]

async def mock_get_status():
    await asyncio.sleep(0.5)
    return {"connected": True, "battery_pct": random.randint(60, 95), "status_raw": "OK"}

async def mock_sync_probe(db_path=None, on_log=None):
    records = []
    total = random.randint(8, 20)
    if on_log: on_log(f"Connecting to probe...")
    await asyncio.sleep(0.8)
    if on_log: on_log(f"Connected. Reading metadata — {total} records found.")
    await asyncio.sleep(0.4)
    if on_log: on_log("BLE transfer started.")
    for i in range(total):
        await asyncio.sleep(0.15)
        ts = int(time.time() * 1000) + i * 60000
        record = {
            "sequence":     i,
            "timestamp_ms": ts,
            "temperature":  round(random.uniform(-2.0, 4.0), 2),
            "depth":        round(random.uniform(10.0, 200.0), 2),
            "salinity":     round(random.uniform(33.0, 35.5), 3),
            "dissolved_o2": round(random.uniform(250.0, 380.0), 1),
            "chlorophyll":  round(random.uniform(0.01, 2.50), 3),
            "ph":           round(random.uniform(7.9, 8.2), 3),
        }
        records.append(record)
        if on_log: on_log(f"  Packet {i+1}/{total} received  [seq={i}]")
    await asyncio.sleep(0.3)
    if on_log: on_log(f"Transfer complete. {total} records synced.")
    return records

async def mock_scan_devices():
    await asyncio.sleep(1.2)
    return MOCK_DEVICES


# ══════════════════════════════════════════════════════════════════════════════
#  Helper — run async from a background thread without blocking the GUI
# ══════════════════════════════════════════════════════════════════════════════
def run_async(coro, callback=None):
    def _thread():
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            result = loop.run_until_complete(coro)
            if callback:
                callback(result, None)
        except Exception as exc:
            if callback:
                callback(None, exc)
        finally:
            loop.close()
    t = threading.Thread(target=_thread, daemon=True)
    t.start()


# ══════════════════════════════════════════════════════════════════════════════
#  Main Application
# ══════════════════════════════════════════════════════════════════════════════
class ProbeApp(ctk.CTk):

    def __init__(self):
        super().__init__()
        self.title("Antarctic Probe — Data Transfer")
        self.geometry("1100x700")
        self.minsize(900, 580)
        self.configure(fg_color=COLOUR_APP_BG)
        ctk.set_appearance_mode("dark")

        self._connected   = False
        self._busy        = False
        self._records     = []
        self._battery_pct = None

        self._build_top_toolbar()
        self._build_body()
        self._log("System initialised. Select a device and press Connect.")

    # ── Top Toolbar ───────────────────────────────────────────────────────
    def _build_top_toolbar(self):
        bar = ctk.CTkFrame(self, fg_color=COLOUR_TOOLBAR, corner_radius=0, height=52)
        bar.pack(side="top", fill="x")
        bar.pack_propagate(False)

        ctk.CTkLabel(
            bar, text="❄  ANTARCTIC PROBE", font=("Courier New", 15, "bold"),
            text_color=COLOUR_TOOLBAR_TEXT, fg_color="transparent"
        ).pack(side="left", padx=18)

        right = ctk.CTkFrame(bar, fg_color="transparent")
        right.pack(side="right", padx=16)

        self._battery_label = ctk.CTkLabel(
            right, text="🔋  ---%", font=("Courier New", 12),
            text_color=COLOUR_TOOLBAR_TEXT, fg_color="transparent"
        )
        self._battery_label.pack(side="right", padx=(12, 0))

        self._status_dot = ctk.CTkLabel(
            right, text="●", font=("Arial", 18),
            text_color=COLOUR_STATUS_OFF, fg_color="transparent"
        )
        self._status_dot.pack(side="right", padx=(0, 4))

        self._status_text = ctk.CTkLabel(
            right, text="DISCONNECTED", font=("Courier New", 11, "bold"),
            text_color=COLOUR_TOOLBAR_TEXT, fg_color="transparent"
        )
        self._status_text.pack(side="right")

    # ── Body ──────────────────────────────────────────────────────────────
    def _build_body(self):
        body = ctk.CTkFrame(self, fg_color="transparent")
        body.pack(fill="both", expand=True)
        self._build_sidebar(body)
        self._build_main_panel(body)

    # ── Sidebar ───────────────────────────────────────────────────────────
    def _build_sidebar(self, parent):
        side = ctk.CTkFrame(parent, fg_color=COLOUR_TOOLBAR, corner_radius=0, width=210)
        side.pack(side="left", fill="y")
        side.pack_propagate(False)

        pad = {"padx": 16, "pady": 6}

        ctk.CTkLabel(
            side, text="BLE DEVICE", font=("Courier New", 10, "bold"),
            text_color="#8AAFC8", fg_color="transparent"
        ).pack(anchor="w", padx=16, pady=(20, 2))

        self._device_var = ctk.StringVar(value="Select device...")
        self._device_dropdown = ctk.CTkOptionMenu(
            side,
            variable=self._device_var,
            values=["Select device..."],
            fg_color=COLOUR_TOOLBAR_HOVER,
            button_color=COLOUR_TOOLBAR_HOVER,
            button_hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT,
            font=("Courier New", 11),
            width=178,
        )
        self._device_dropdown.pack(**pad)

        self._scan_btn = ctk.CTkButton(
            side, text="⟳  Scan", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_scan
        )
        self._scan_btn.pack(**pad)

        ctk.CTkFrame(side, fg_color="#2A4A6C", height=1).pack(fill="x", padx=16, pady=14)

        ctk.CTkLabel(
            side, text="ACTIONS", font=("Courier New", 10, "bold"),
            text_color="#8AAFC8", fg_color="transparent"
        ).pack(anchor="w", padx=16, pady=(0, 2))

        self._connect_btn = ctk.CTkButton(
            side, text="⏻  Connect", width=178,
            fg_color=COLOUR_ACCENT, hover_color="#1565C0",
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 13, "bold"),
            corner_radius=6, command=self._on_connect
        )
        self._connect_btn.pack(**pad)

        self._sync_btn = ctk.CTkButton(
            side, text="↓  Sync Data", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_sync, state="disabled"
        )
        self._sync_btn.pack(**pad)

        ctk.CTkFrame(side, fg_color="#2A4A6C", height=1).pack(fill="x", padx=16, pady=14)

        ctk.CTkLabel(
            side, text="FILE", font=("Courier New", 10, "bold"),
            text_color="#8AAFC8", fg_color="transparent"
        ).pack(anchor="w", padx=16, pady=(0, 2))

        ctk.CTkButton(
            side, text="📂  Load from File", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_load_file
        ).pack(**pad)

        ctk.CTkButton(
            side, text="✕  Clear Display", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color="#C62828",
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_clear
        ).pack(**pad)

        ctk.CTkLabel(
            side, text="v1.0 — SS1", font=("Courier New", 9),
            text_color="#4A6A8A", fg_color="transparent"
        ).pack(side="bottom", pady=10)

    # ── Main Panel ────────────────────────────────────────────────────────
    def _build_main_panel(self, parent):
        main = ctk.CTkFrame(parent, fg_color=COLOUR_APP_BG, corner_radius=0)
        main.pack(side="left", fill="both", expand=True, padx=12, pady=12)

        ctk.CTkLabel(
            main, text="RECEIVED DATA", font=("Courier New", 10, "bold"),
            text_color=COLOUR_TOOLBAR, fg_color="transparent"
        ).pack(anchor="w", padx=4, pady=(0, 4))

        data_frame = ctk.CTkFrame(main, fg_color=COLOUR_PANEL, corner_radius=8)
        data_frame.pack(fill="both", expand=True, pady=(0, 10))

        self._data_text = tk.Text(
            data_frame,
            bg=COLOUR_PANEL, fg=COLOUR_PANEL_TEXT,
            font=("Courier New", 11),
            relief="flat", bd=0,
            wrap="none",
            state="disabled",
            selectbackground=COLOUR_ACCENT,
        )
        self._data_text.pack(side="left", fill="both", expand=True, padx=10, pady=8)

        data_scroll = ctk.CTkScrollbar(data_frame, command=self._data_text.yview)
        data_scroll.pack(side="right", fill="y", pady=8)
        self._data_text.configure(yscrollcommand=data_scroll.set)
        self._write_data_header()

        ctk.CTkLabel(
            main, text="STATUS LOG", font=("Courier New", 10, "bold"),
            text_color=COLOUR_TOOLBAR, fg_color="transparent"
        ).pack(anchor="w", padx=4, pady=(0, 4))

        term_frame = ctk.CTkFrame(main, fg_color=COLOUR_PANEL, corner_radius=8, height=180)
        term_frame.pack(fill="x")
        term_frame.pack_propagate(False)

        self._term_text = tk.Text(
            term_frame,
            bg=COLOUR_PANEL, fg=COLOUR_TERMINAL_TEXT,
            font=("Courier New", 11),
            relief="flat", bd=0,
            wrap="word",
            state="disabled",
        )
        self._term_text.pack(side="left", fill="both", expand=True, padx=10, pady=8)

        term_scroll = ctk.CTkScrollbar(term_frame, command=self._term_text.yview)
        term_scroll.pack(side="right", fill="y", pady=8)
        self._term_text.configure(yscrollcommand=term_scroll.set)

    # ── Helpers ───────────────────────────────────────────────────────────
    def _write_data_header(self):
        header = (
            f"{'SEQ':>5}  {'TIMESTAMP':>14}  {'TEMP (°C)':>10}  "
            f"{'DEPTH (m)':>10}  {'SAL (PSU)':>10}  "
            f"{'O2 (µmol/L)':>12}  {'CHLO (µg/L)':>12}  {'pH':>7}\n"
        )
        divider = "─" * 95 + "\n"
        self._data_append(header + divider)

    def _log(self, msg):
        def _do():
            ts = datetime.now().strftime("%H:%M:%S")
            self._term_text.configure(state="normal")
            self._term_text.insert("end", f"[{ts}]  {msg}\n")
            self._term_text.configure(state="disabled")
            self._term_text.see("end")
        self.after(0, _do)

    def _data_append(self, text):
        def _do():
            self._data_text.configure(state="normal")
            self._data_text.insert("end", text)
            self._data_text.configure(state="disabled")
            self._data_text.see("end")
        self.after(0, _do)

    def _display_records(self, records):
        for r in records:
            ts_s = r["timestamp_ms"] / 1000
            dt   = datetime.fromtimestamp(ts_s).strftime("%H:%M:%S")
            line = (
                f"{r['sequence']:>5}  {dt:>14}  {r['temperature']:>10.2f}  "
                f"{r['depth']:>10.2f}  {r['salinity']:>10.3f}  "
                f"{r['dissolved_o2']:>12.1f}  {r['chlorophyll']:>12.3f}  {r['ph']:>7.3f}\n"
            )
            self._data_append(line)

    def _set_connected(self, connected, battery=None):
        def _do():
            self._connected = connected
            if connected:
                self._status_dot.configure(text_color=COLOUR_STATUS_ON)
                self._status_text.configure(text="CONNECTED")
                self._sync_btn.configure(state="normal")
                self._connect_btn.configure(text="⏻  Disconnect")
            else:
                self._status_dot.configure(text_color=COLOUR_STATUS_OFF)
                self._status_text.configure(text="DISCONNECTED")
                self._sync_btn.configure(state="disabled")
                self._connect_btn.configure(text="⏻  Connect")
            if battery is not None:
                self._battery_label.configure(text=f"🔋  {battery}%")
            else:
                self._battery_label.configure(text="🔋  ---%")
        self.after(0, _do)

    def _set_busy(self, busy, btn_text=None):
        def _do():
            self._busy = busy
            state = "disabled" if busy else "normal"
            self._scan_btn.configure(state=state)
            self._connect_btn.configure(state=state)
            if not busy and btn_text:
                self._connect_btn.configure(text=btn_text)
        self.after(0, _do)

    # ── Handlers ──────────────────────────────────────────────────────────
    def _on_scan(self):
        if self._busy: return
        self._log("Scanning for BLE devices...")
        self._set_busy(True)

        def _done(devices, err):
            self._set_busy(False)
            if err or not devices:
                self._log(f"Scan failed or no devices found: {err}")
                return
            self._device_dropdown.configure(values=devices)
            self._device_var.set(devices[0])
            self._log(f"Found {len(devices)} device(s): {', '.join(devices)}")

        run_async(mock_scan_devices(), _done)

    def _on_connect(self):
        if self._busy: return
        if self._connected:
            self._set_connected(False)
            self._log("Disconnected from probe.")
            return
        device = self._device_var.get()
        if device == "Select device...":
            self._log("ERROR: No device selected. Scan first.")
            return
        self._log(f"Connecting to '{device}'...")
        self._set_busy(True)

        def _done(result, err):
            self._set_busy(False, btn_text="⏻  Disconnect")
            if err or not result:
                self._log(f"Connection failed: {err}")
                self._set_connected(False)
                return
            battery = result.get("battery_pct")
            self._set_connected(True, battery=battery)
            self._log(f"Connected to '{device}'. Battery: {battery}%")

        run_async(mock_get_status(), _done)

    def _on_sync(self):
        if self._busy or not self._connected: return
        self._log("Starting data sync...")
        self._set_busy(True)

        def _log_cb(msg): self._log(msg)

        async def _do_sync():
            return await mock_sync_probe(on_log=_log_cb)

        def _done(records, err):
            self._set_busy(False, btn_text="⏻  Disconnect")
            if err or records is None:
                self._log(f"Sync failed: {err}")
                return
            self._records.extend(records)
            self._display_records(records)

        run_async(_do_sync(), _done)

    def _on_load_file(self):
        from tkinter import filedialog
        import csv
        path = filedialog.askopenfilename(
            title="Load probe data",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")]
        )
        if not path: return
        self._log(f"Loading from file: {Path(path).name}")
        try:
            with open(path, newline="") as f:
                reader = csv.DictReader(f)
                records = []
                for row in reader:
                    records.append({
                        "sequence":     int(row.get("sequence", 0)),
                        "timestamp_ms": int(row.get("timestamp_ms", 0)),
                        "temperature":  float(row.get("temperature_C", 0)),
                        "depth":        float(row.get("depth_m", 0)),
                        "salinity":     float(row.get("salinity_PSU", 0)),
                        "dissolved_o2": float(row.get("dissolved_o2_umolL", 0)),
                        "chlorophyll":  float(row.get("chlorophyll_ugL", 0)),
                        "ph":           float(row.get("ph", 0)),
                    })
            self._display_records(records)
            self._log(f"Loaded {len(records)} records from file.")
        except Exception as e:
            self._log(f"Failed to load file: {e}")

    def _on_clear(self):
        self._data_text.configure(state="normal")
        self._data_text.delete("1.0", "end")
        self._data_text.configure(state="disabled")
        self._write_data_header()
        self._records.clear()
        self._log("Display cleared.")


if __name__ == "__main__":
    app = ProbeApp()
    app.mainloop()