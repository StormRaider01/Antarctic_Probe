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
            self.log("[ACK] Probe acknowledged connection.")
            battery = result.get("battery_pct")
            self._battery_pct = battery
            self._set_connected(True, battery=battery)
            self._log(f"[BAT]: Probe battery is at {battery}%")

        bk.run_in_thread(bk.connect_probe(), _done)