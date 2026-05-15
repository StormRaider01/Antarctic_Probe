"""
GUI.py  —  Antarctic Probe Mission Control
==========================================
Standalone GUI for ESP-NOW probe data transfer.
Backend logic lives in Backend.py (imported as bk).
Theme colours are controlled by three variables at the top.

Modes:
  - Prepare Dive : sends date/time to probe, resets memory
  - Retrieve Data: triggers ESP-NOW sync from dongle

Tabs:
  - Raw Data  : scrollable table of all records
  - Graph     : matplotlib chart with y-axis selector,
                all-samples / individual-sample radio,
                < > cycle buttons, and a constant-values panel
  - Processing : Kiyuran's section for live data processing and ML model outputs (TBD)
"""

import asyncio
import threading
import tkinter as tk
from datetime import datetime
from pathlib import Path

import customtkinter as ctk
import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import matplotlib.pyplot as plt

import Backend as bk

# ======================================================================
# Theme colours (for ease of control)
COLOUR_TOOLBAR       = "#1A3A5C"
COLOUR_APP_BG        = "#B2D8D8"
COLOUR_PANEL         = "#F5F0E8"
COLOUR_TOOLBAR_TEXT  = "#FFFFFF"
COLOUR_TOOLBAR_HOVER = "#2A5A8C"
COLOUR_PANEL_TEXT    = "#1A1A2E"
COLOUR_TERMINAL_TEXT = "#2C5F2E"
COLOUR_STATUS_ON     = "#00E676"
COLOUR_STATUS_OFF    = "#EF5350"
COLOUR_ACCENT        = "#1E90FF"

# ======================================================================
# Graph configuration
SAMPLES_PER_GROUP = 15   # number of readings per fluorescence sample group

# ======================================================================

class ProbeApp(ctk.CTk):

    def __init__(self):
        super().__init__()
        self.title("Antarctic Probe — Mission Control")
        self.geometry("1200x750")
        self.minsize(950, 600)
        self.configure(fg_color=COLOUR_APP_BG)
        ctk.set_appearance_mode("dark")

        # ==============================================================
        # Initialise
        self._connected   = False
        self._busy        = False
        self._records     = []
        self._battery_pct = None
        self._mode        = None          # "dive" | "retrieve"

        # Graph state
        self._graph_group_index = 0       # current sample-group index
        self._graph_mode        = tk.StringVar(value="all")   # "all" | "single"
        self._graph_y_var       = tk.StringVar(value="temperature")

        self._build_top_toolbar()
        self._build_body()

        # Initial log message and button states
        # Button are initially disabled since the dongle first needs to come online
        self._log("System initialised. Looking for dongle...")
        self._connect_btn.configure(state="disabled")
        #self._dive_btn.configure(state="disabled")
        #self._retrieve_btn.configure(state="disabled")

        # Give the system time to render before looking for dongle
        self.after(1000, self._find_dongle)


    def _find_dongle(self):
        def _done(result, err):
            if err or not result:
                self.after(3000, self._find_dongle)   # try again in a few seconds
                return
            self._log("Dongle Found. Ready to connect to probe.")
            self._connect_btn.configure(state="normal")

        # Logic to look for dongle handled in backend
        bk.run_async(bk.connect_dongle(), _done)




    # ==============================================================================
    #  TOP TOOLBAR

    def _build_top_toolbar(self):
        # bar 
        bar = ctk.CTkFrame(self, fg_color=COLOUR_TOOLBAR, corner_radius=0, height=52)
        bar.pack(side="top", fill="x")
        bar.pack_propagate(False)

        # Title
        ctk.CTkLabel(
            bar, text="❄  ANTARCTIC PROBE", font=("Courier New", 15, "bold"),
            text_color=COLOUR_TOOLBAR_TEXT, fg_color="transparent"
        ).pack(side="left", padx=18)

        # Centering right
        right = ctk.CTkFrame(bar, fg_color="transparent")
        right.pack(side="right", padx=16)

        # Battery status (initial state)
        self._battery_label = ctk.CTkLabel(
            right, text="🔋  ---%", font=("Courier New", 12),
            text_color=COLOUR_TOOLBAR_TEXT, fg_color="transparent"
        )
        self._battery_label.pack(side="right", padx=(12, 0))

        # Connected status (initial state)
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






    # =============================================================================
    # Body

    def _build_body(self):
        # body
        body = ctk.CTkFrame(self, fg_color="transparent")
        body.pack(fill="both", expand=True)
        self._build_sidebar(body)
        self._build_main_panel(body)





    # ==============================================================================
    # Side bar

    def _build_sidebar(self, parent):
        # side
        side = ctk.CTkFrame(parent, fg_color=COLOUR_TOOLBAR, corner_radius=0, width=210)
        side.pack(side="left", fill="y")
        side.pack_propagate(False)

        pad = {"padx": 16, "pady": 6}

        def section(text):
            ctk.CTkFrame(side, fg_color="#2A4A6C", height=1).pack(fill="x", padx=16, pady=(12, 4))
            ctk.CTkLabel(
                side, text=text, font=("Courier New", 10, "bold"),
                text_color="#8AAFC8", fg_color="transparent"
            ).pack(anchor="w", padx=16, pady=(0, 2))

        


        # ==============================================================================
        # Connection controls

        section("CONNECTION")

        self._connect_btn = ctk.CTkButton(
            side, text="⏻  Connect", width=178,
            fg_color=COLOUR_ACCENT, hover_color="#1565C0",
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 13, "bold"),
            corner_radius=6, command=self._on_connect
        )
        self._connect_btn.pack(**pad)



        # ==============================================================================
        # Probe Modes

        section("PROBE MODE")

        # Sets probe to dive mode (resets memory, sets timestamp)
        self._dive_btn = ctk.CTkButton(
            side, text="↑  Prepare Dive", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color="#1565C0",
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_prepare_dive, state="normal"
        )
        self._dive_btn.pack(**pad)

        # Sets probe to retrieval mode (triggers ESP-NOW sync with dongle)
        self._retrieve_btn = ctk.CTkButton(
            side, text="↓  Retrieve Data", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_retrieve_data, state="normal"
        )
        self._retrieve_btn.pack(**pad)

        # Dev Mode: Simulate Data
        self._simulate_btn = ctk.CTkButton(
            side, text="🧪  Dev Mode: Simulate", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color="#C62828",
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_simulate_data
        )
        self._simulate_btn.pack(**pad)




        # ==============================================================================
        # File
        section("FILE")

        # Saves data to CSV with file dialog
        ctk.CTkButton(
            side, text="🗃️  Save to File", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_save_file
        ).pack(**pad)

        # Loads data from CSV with file dialog (appended to existing records)
        ctk.CTkButton(
            side, text="📂  Load from File", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_load_file
        ).pack(**pad)

        # Clears the raw data tab and graph
        ctk.CTkButton(
            side, text="✕  Clear Display", width=178,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color="#C62828",
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=6, command=self._on_clear
        ).pack(**pad)

        ctk.CTkLabel(
            side, text="v2.0 — SS1", font=("Courier New", 9),
            text_color="#4A6A8A", fg_color="transparent"
        ).pack(side="bottom", pady=10)







    # ==============================================================================
    # Main Panel with Tabs

    def _build_main_panel(self, parent):
        # main
        main = ctk.CTkFrame(parent, fg_color=COLOUR_APP_BG, corner_radius=0)
        main.pack(side="left", fill="both", expand=True, padx=12, pady=12)




        # TabView
        self._tabview = ctk.CTkTabview(
            main,
            fg_color=COLOUR_PANEL,
            segmented_button_fg_color=COLOUR_TOOLBAR,
            segmented_button_selected_color=COLOUR_ACCENT,
            segmented_button_selected_hover_color="#1565C0",
            segmented_button_unselected_color=COLOUR_TOOLBAR,
            segmented_button_unselected_hover_color=COLOUR_TOOLBAR_HOVER,
            text_color=COLOUR_TOOLBAR_TEXT,
        )
        self._tabview.pack(fill="both", expand=True, pady=(0, 10))

        self._tabview.add("Raw Data")
        self._tabview.add("Graph")



        """
        =====================================================================================================
        self._tabview.add("Processing")   # Kiyuran's section for live data processing and ML model outputs (TBD)
        self._build_processing_tab(self._tabview.tab("Processing"))   # Kiyuran's section (TBD)
        =====================================================================================================
        """




        self._build_raw_tab(self._tabview.tab("Raw Data"))
        self._build_graph_tab(self._tabview.tab("Graph"))



        # # ==============================================================================
        # Status log (shared, below tabs)
        ctk.CTkLabel(
            main, text="STATUS LOG", font=("Courier New", 10, "bold"),
            text_color=COLOUR_TOOLBAR, fg_color="transparent"
        ).pack(anchor="w", padx=4, pady=(0, 4))

        # term_frame
        term_frame = ctk.CTkFrame(main, fg_color=COLOUR_PANEL, corner_radius=8, height=160)
        term_frame.pack(fill="x")
        term_frame.pack_propagate(False)

        self._term_text = tk.Text(
            term_frame,
            bg=COLOUR_PANEL, fg=COLOUR_TERMINAL_TEXT,
            font=("Courier New", 11),
            relief="flat", bd=0, wrap="word", state="disabled",
        )
        self._term_text.pack(side="left", fill="both", expand=True, padx=10, pady=8)

        # term_scroll (scroll bar)
        term_scroll = ctk.CTkScrollbar(term_frame, command=self._term_text.yview)
        term_scroll.pack(side="right", fill="y", pady=8)
        self._term_text.configure(yscrollcommand=term_scroll.set)





    # ==============================================================================
    # Raw Data Tab
    def _build_raw_tab(self, tab):
        # data_frame
        data_frame = ctk.CTkFrame(tab, fg_color=COLOUR_PANEL, corner_radius=0)
        data_frame.pack(fill="both", expand=True)

        self._data_text = tk.Text(
            data_frame,
            bg=COLOUR_PANEL, fg=COLOUR_PANEL_TEXT,
            font=("Courier New", 11),
            relief="flat", bd=0, wrap="none", state="disabled",
            selectbackground=COLOUR_ACCENT,
        )
        self._data_text.pack(side="left", fill="both", expand=True, padx=10, pady=8)

        data_scroll = ctk.CTkScrollbar(data_frame, command=self._data_text.yview)
        data_scroll.pack(side="right", fill="y", pady=8)
        self._data_text.configure(yscrollcommand=data_scroll.set)
        self._write_data_header()





    # ==============================================================================
    # Graph tab
    def _build_graph_tab(self, tab):
        # Controls row
        ctrl = ctk.CTkFrame(tab, fg_color="transparent")
        ctrl.pack(fill="x", padx=8, pady=(8, 4))

        ctk.CTkLabel(ctrl, text="Y-Axis:", font=("Courier New", 11),
                     text_color=COLOUR_PANEL_TEXT).pack(side="left", padx=(0, 6))

        # drop down options for y-axis selector
        # Graph will automiatically update when selection changes
        y_options = ["temperature", "pressure", "excitation", "fluorescence"]
        self._y_menu = ctk.CTkOptionMenu(
            ctrl, variable=self._graph_y_var, values=y_options,
            command=lambda _: self._refresh_graph(),
            fg_color=COLOUR_TOOLBAR, button_color=COLOUR_TOOLBAR_HOVER,
            button_hover_color=COLOUR_ACCENT, text_color=COLOUR_TOOLBAR_TEXT,
            font=("Courier New", 11), width=150,
        )
        self._y_menu.pack(side="left", padx=(0, 20))




        # ==============================================================================
        # Radio buttons for which view, ALL or SINGLE group
        ctk.CTkLabel(ctrl, text="View:", font=("Courier New", 11),
                     text_color=COLOUR_PANEL_TEXT).pack(side="left", padx=(0, 4))

        for label, val in [("All Samples", "all"), ("Individual Group", "single")]:
            ctk.CTkRadioButton(
                ctrl, text=label, variable=self._graph_mode, value=val,
                command=self._on_graph_mode_change,
                font=("Courier New", 11), text_color=COLOUR_PANEL_TEXT,
                fg_color=COLOUR_ACCENT, border_color=COLOUR_TOOLBAR,
            ).pack(side="left", padx=6)




        # ==============================================================================
        # Cycle buttons 
        # Shown only in single mode
        self._cycle_frame = ctk.CTkFrame(ctrl, fg_color="transparent")
        self._cycle_frame.pack(side="left", padx=(12, 0))

        self._prev_btn = ctk.CTkButton(
            self._cycle_frame, text="◀", width=36,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=4, command=self._on_prev_group
        )
        self._prev_btn.pack(side="left", padx=2)

        # Only initially shows 1/1. When data loaded it will be updated
        self._group_label = ctk.CTkLabel(
            self._cycle_frame, text="Sample Group 1/1", font=("Courier New", 11),
            text_color=COLOUR_PANEL_TEXT
        )
        self._group_label.pack(side="left", padx=6)

        self._next_btn = ctk.CTkButton(
            self._cycle_frame, text="▶", width=36,
            fg_color=COLOUR_TOOLBAR_HOVER, hover_color=COLOUR_ACCENT,
            text_color=COLOUR_TOOLBAR_TEXT, font=("Courier New", 12),
            corner_radius=4, command=self._on_next_group
        )
        self._next_btn.pack(side="left", padx=2)
        self._cycle_frame.pack_forget()   # hidden until single mode selected




        # ==============================================================================
        # Matplotlib canvas
        self._fig, self._ax = plt.subplots(figsize=(8, 3.5))
        self._fig.patch.set_facecolor(COLOUR_PANEL)
        self._ax.set_facecolor("#EDE8E0")
        self._canvas = FigureCanvasTkAgg(self._fig, master=tab)
        self._canvas.get_tk_widget().pack(fill="both", expand=True, padx=8, pady=(0, 4))

        # Constants panel (shown in single mode)
        self._const_frame = ctk.CTkFrame(tab, fg_color=COLOUR_TOOLBAR, corner_radius=6, height=40)
        self._const_label = ctk.CTkLabel(
            self._const_frame, text="", font=("Courier New", 11),
            text_color=COLOUR_TOOLBAR_TEXT, fg_color="transparent"
        )
        self._const_label.pack(padx=12, pady=6)
        # hidden until single mode

    




    # ==============================================================================
    # Graph logic
    def _on_graph_mode_change(self):
        # Display or hide cycle buttons
        if self._graph_mode.get() == "single":
            self._cycle_frame.pack(side="left", padx=(12, 0))
            self._const_frame.pack(fill="x", padx=8, pady=(0, 6))
        else:
            self._cycle_frame.pack_forget()
            self._const_frame.pack_forget()
        self._graph_group_index = 0
        self._refresh_graph()

    # Number of sample groups
    def _num_groups(self):
        if not self._records:
            return 1
        return max(1, -(-len(self._records) // SAMPLES_PER_GROUP))  # ceiling div

    def _on_prev_group(self):
        if self._graph_group_index > 0:
            self._graph_group_index -= 1
            self._refresh_graph()

    def _on_next_group(self):
        if self._graph_group_index < self._num_groups() - 1:
            self._graph_group_index += 1
            self._refresh_graph()

    def _refresh_graph(self):
        if not self._records:
            return

        y_key  = self._graph_y_var.get()
        y_labels = {
            "temperature":  "Temperature (°C)",
            "pressure":     "Pressure (dbar)",
            "excitation":   "Excitation (raw ADC)",
            "fluorescence": "Fluorescence (raw ADC)",
        }

        if self._graph_mode.get() == "all":
            records = self._records
            title   = f"{y_key.capitalize()} — All Samples"
        else:
            n = self._num_groups()
            start  = self._graph_group_index * SAMPLES_PER_GROUP
            end    = start + SAMPLES_PER_GROUP
            records = self._records[start:end]
            title  = f"{y_key.capitalize()} — Group {self._graph_group_index + 1}/{n}"
            self._group_label.configure(text=f"Group {self._graph_group_index + 1}/{n}")

            # Update constants panel from first record in group
            # Displays constants of whatever is not displayed on y-axis
            if records:
                r = records[0]
                const_keys = [k for k in ["temperature", "pressure", "excitation", "fluorescence"] if k != y_key]
                parts = [f"{k.capitalize()}: {r[k]:.2f}" for k in const_keys if k in r]
                self._const_label.configure(text="   |   ".join(parts))




        xs = [r["timestamp_ms"] for r in records]
        ys = [r.get(y_key, 0) for r in records]

        # Filter out anomaly points
        anomaly_xs = [r["timestamp_ms"] for r in records if r.get("is_anomaly")]
        anomaly_ys = [r.get(y_key, 0) for r in records if r.get("is_anomaly")]

        # ==============================================================================
        # Graph styling
        self._ax.clear()
        self._ax.plot(xs, ys, color=COLOUR_ACCENT, linewidth=1.8, marker="o", markersize=3, label="Normal")
        
        # Overlay red scatter for anomalies
        if anomaly_xs:
            self._ax.scatter(anomaly_xs, anomaly_ys, color="red", zorder=5, s=30, label="Anomaly Detected")
            self._ax.legend(loc="upper right", fontsize=8)

        self._ax.set_title(title, fontsize=10, color=COLOUR_PANEL_TEXT, pad=6)
        self._ax.set_xlabel("Time since start (ms)", fontsize=9, color=COLOUR_PANEL_TEXT)
        self._ax.set_ylabel(y_labels.get(y_key, y_key), fontsize=9, color=COLOUR_PANEL_TEXT)
        self._ax.tick_params(colors=COLOUR_PANEL_TEXT, labelsize=8)
        for spine in self._ax.spines.values():
            spine.set_edgecolor("#CCCCCC")
        self._ax.set_facecolor("#EDE8E0")
        self._fig.patch.set_facecolor(COLOUR_PANEL)
        self._fig.tight_layout()
        self._canvas.draw()












    # ==============================================================================
    # Helping classes (backend)
    # ==============================================================================

    # Writes the header line for the raw data tab
    def _write_data_header(self):
        header = (
            f"{'SEQ':>5}  {'TIME (ms)':>12}  {'TEMP (°C)':>10}  "
            f"{'PRESSURE':>10}  {'EXCITATION':>12}  {'FLUORESCENCE':>14}\n"
        )
        divider = "─" * (len(header) - 1) + "\n"
        self._data_append(header + divider)



    # Logs a message to the status log with timestamp
    # eg. "[12:34:56] Connected to probe. Battery: 85%"
    def _log(self, msg):
        def _do():
            ts = datetime.now().strftime("%H:%M:%S")
            self._term_text.configure(state="normal")
            self._term_text.insert("end", f"[{ts}]  {msg}\n")
            self._term_text.configure(state="disabled")
            self._term_text.see("end")
        self.after(0, _do)


    # Appends a line of text to the raw data tab
    # Used when new records are received
    def _data_append(self, text):
        def _do():
            self._data_text.configure(state="normal")
            self._data_text.insert("end", text)
            self._data_text.configure(state="disabled")
            self._data_text.see("end")
        self.after(0, _do)



    # Displays a list of records in the raw data tab, and refreshes the graph
    def _display_records(self, records):
        for r in records:
            anomaly_flag = "  [ANOMALY]" if r.get('is_anomaly') else ""
            line = (
                f"{r['sequence']:>5}  {r['timestamp_ms']:>12}  "
                f"{r['temperature']:>10.2f}  {r['pressure']:>10.2f}  "
                f"{r['excitation']:>12.3f}  {r['fluorescence']:>14.3f}{anomaly_flag}\n"
            )
            self._data_append(line)
        self._refresh_graph()



    # Updates the connected status in the toolbar, and battery percentage
    def _set_connected(self, connected, battery=None):
        def _do():
            self._connected = connected
            if connected:
                self._status_dot.configure(text_color=COLOUR_STATUS_ON)
                self._status_text.configure(text="CONNECTED")
                self._dive_btn.configure(state="normal")
                self._retrieve_btn.configure(state="normal")
                self._connect_btn.configure(text="⏻  Disconnect")
            else:
                self._status_dot.configure(text_color=COLOUR_STATUS_OFF)
                self._status_text.configure(text="DISCONNECTED")
                self._dive_btn.configure(state="disabled")
                self._retrieve_btn.configure(state="disabled")
                self._connect_btn.configure(text="⏻  Connect")
            batt = battery if battery is not None else self._battery_pct        # if battery was obtained
            self._battery_label.configure(text=f"🔋  {batt}%" if batt is not None else "🔋  ---%")
        self.after(0, _do)



    # Sets status of app to busy
    # When busy, some buttons are disabled and status log shows ongoing action
    # This helps prevent multiple actions being triggered at once
    def _set_busy(self, busy):
        def _do():
            self._busy = busy
            state = "disabled" if busy else "normal"
            self._connect_btn.configure(state=state)
            if not busy and self._connected:
                self._dive_btn.configure(state="normal")
                self._retrieve_btn.configure(state="normal")
        self.after(0, _do)









    # ==============================================================================
    # Button actions
    # ==============================================================================

    # Connect button
    def _on_connect(self):
        if self._busy:
            return
        if self._connected:
            self._set_connected(False, battery=None)
            self._battery_pct = None

            # Send CMD to disconnect and de-init ESP NOW
            disconnect = bk.disconnect_probe()
            if (disconnect):
                self._log("Disconnected from probe.")
            return

        self._log("Connecting to probe ...")
        self._set_busy(True)


        def _done(result, err):
            self._set_busy(False)
            if err or result is None:
                self._log(f"Connection failed: {err}")
                self._set_connected(False)
                return
            self._log("[ACK] Probe acknowledged connection.")
            battery = result.get("battery_pct")
            self._battery_pct = battery
            self._set_connected(True, battery=battery)
            self._log(f"[BAT]: Probe battery is at {battery}%")

        bk.run_in_thread(bk.connect_probe, _done)



    # Prepare Dive button
    def _on_retrieve_data(self):
        if self._busy: #or not self._connected:
            return
        self._log("Sending RETRIEVE command to dongle...")
        self._set_busy(True)

        bk.send_retrieve()   # non-blocking, just writes CMD:RETRIEVE to serial

        def _log_cb(msg):
            self._log(msg)

        def _done(records, err):
            self._set_busy(False)
            if err or records is None:
                self._log(f"Sync failed: {err}")
                return
            # Records are appended live, so no need to append them again here
            self._log(f"Retrieval complete — {len(records)} records received.")

        bk.run_in_thread(
            lambda: bk.sync_probe(log_callback=_log_cb, on_record=self._handle_live_record),
            _done
        )


    def _on_prepare_dive(self):
        if self._busy:
            self._log("Busy")
            return

        now = datetime.now()
        date_str = now.strftime("%Y-%m-%d")
        time_str = now.strftime("%H:%M:%S")
        self._log(f"Sending PREPARE command — {date_str} {time_str}")
        self._set_busy(True)

        def _done(result, err):
            self._set_busy(False)
            if err or not result:
                self._log(f"PREPARE DIVE failed: {err}")
                return
            self._log("[ACK] Probe ready for dive.")

        bk.run_in_thread(
            lambda: bk.send_prepare_dive(date_str, time_str),
            _done
        )


    def _on_simulate_data(self):
        if self._busy:
            return
        self._log("Starting Dev Mode: Data Simulation...")
        self._set_busy(True)

        def _log_cb(msg):
            self._log(msg)

        def _done(records, err):
            self._set_busy(False)
            if err or records is None:
                self._log(f"Simulation failed: {err}")
                return
            self._log(f"Simulation complete — {len(records)} records processed.")

        bk.run_in_thread(
            lambda: bk.simulate_incoming_data(log_callback=_log_cb, on_record=self._handle_live_record),
            _done
        )

    def _handle_live_record(self, r):
        def _do():
            self._records.append(r)
            self._display_records([r])
        self.after(0, _do)



    # Save to file button
    def _on_save_file(self):
        from tkinter import filedialog
        import csv
        if not self._records:
            self._log("No data to save.")
            return
        path = filedialog.asksaveasfilename(
            title="Save probe data",
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            with open(path, mode="w", newline="") as f:
                fieldnames = ["sequence", "timestamp_ms", "temperature",
                              "pressure", "excitation", "fluorescence"]
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for r in self._records:
                    writer.writerow({k: r[k] for k in fieldnames})
            self._log(f"Saved {len(self._records)} records → {Path(path).name}")
        except Exception as e:
            self._log(f"Save failed: {e}")



    # Load from file button
    def _on_load_file(self):
        from tkinter import filedialog
        import csv
        path = filedialog.askopenfilename(
            title="Load probe data",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            with open(path, newline="") as f:
                reader = csv.DictReader(f)
                records = []
                for i, row in enumerate(reader):
                    records.append({
                        "sequence":     int(row.get("sequence", i)),
                        "timestamp_ms": int(row.get("timestamp_ms", 0)),
                        "temperature":  float(row.get("temperature", 0)),
                        "pressure":     float(row.get("pressure", 0)),
                        "excitation":   float(row.get("excitation", 0)),
                        "fluorescence": float(row.get("fluorescence", 0)),
                    })
            self._records.extend(records)
            self._display_records(records)
            self._log(f"Loaded {len(records)} records from {Path(path).name}")
        except Exception as e:
            self._log(f"Load failed: {e}")



    # Clear display button
    def _on_clear(self):
        self._data_text.configure(state="normal")
        self._data_text.delete("1.0", "end")
        self._data_text.configure(state="disabled")
        self._write_data_header()
        self._records.clear()
        self._ax.clear()
        self._canvas.draw()
        self._log("Display cleared.")




# ==============================================================================
if __name__ == "__main__":
    app = ProbeApp()
    app.mainloop()
