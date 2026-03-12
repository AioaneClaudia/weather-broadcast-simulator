import os
import socket
import threading
import uuid
from pathlib import Path
from PyQt6 import QtWidgets, QtGui, QtCore
from PyQt6.QtCore import pyqtSignal, QThread
import argparse

# ----------------------------- Argumente -----------------------------
parser = argparse.ArgumentParser()
parser.add_argument('--new-id', action='store_true', help='Generate new client ID even if client_id.txt exists')
args, _ = parser.parse_known_args()

# ----------------------------- Config -----------------------------
SERVER_HOST = '127.0.0.1'
SERVER_TCP_PORT = 6000
LOCAL_UDP_PORT = 5005

CLIENT_ID_FILE = "client_id.txt"

DEFAULT_ICONS = {
    'Sunny': r"D:\Claudia\WeatherBroadcast\assets\icons\sun.png.png",
    'Cloudy': r"D:\Claudia\WeatherBroadcast\assets\icons\cloud.png.png",
    'Rain': r"D:\Claudia\WeatherBroadcast\assets\icons\rain.png.png",
    'Snow': r"D:\Claudia\WeatherBroadcast\assets\icons\snow.png.png",
}

VALID_CITIES = ["Bucharest", "Cluj", "Timisoara", "Iasi"]

# ----------------------------- Helper -----------------------------
def get_or_create_client_id():
    if not args.new_id and os.path.exists(CLIENT_ID_FILE):
        with open(CLIENT_ID_FILE, "r") as f:
            cid = f.read().strip()
            if cid:
                return cid
    cid = str(uuid.uuid4())[:8]
    with open(CLIENT_ID_FILE, "w") as f:
        f.write(cid)
    return cid

# ----------------------------- UDP Listener -----------------------------
class UdpListenerThread(QThread):
    weather_received = pyqtSignal(dict)
    error = pyqtSignal(str)

    def __init__(self, local_port=LOCAL_UDP_PORT, parent=None):
        super().__init__(parent)
        self.local_port = local_port
        self._stopping = False
        self.sock = None

    def run(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                self.sock.bind(("", self.local_port))
            except Exception:
                self.sock.bind(("127.0.0.1", self.local_port))
            self.sock.settimeout(1.0)
            while not self._stopping:
                try:
                    data, _ = self.sock.recvfrom(8192)
                    if not data: continue
                    text = data.decode('utf-8', errors='ignore')
                    d = {}
                    for kv in text.split(';'):
                        if '=' in kv:
                            k, v = kv.split('=', 1)
                            d[k.strip()] = v.strip()
                    self.weather_received.emit(d)
                except socket.timeout:
                    continue
                except Exception as e:
                    self.error.emit(str(e))
                    break
        finally:
            if self.sock: self.sock.close()

    def stop(self):
        self._stopping = True
        self.wait(2000)

# ----------------------------- TCP Helper -----------------------------
def send_tcp_command(msg: str):
    try:
        with socket.create_connection((SERVER_HOST, SERVER_TCP_PORT), timeout=5) as s:
            s.sendall(msg.encode('utf-8'))
            data = s.recv(4096)
            return data.decode('utf-8', errors='ignore')
    except Exception as e:
        return f"ERROR:{e}"

# ----------------------------- GUI -----------------------------
class WeatherWindow(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Weather Client")
        self.resize(360, 240)

        self.client_id = get_or_create_client_id()
        self.city = "Bucharest"
        self.icons = DEFAULT_ICONS.copy()
        self.pixmaps = {}
        self.load_icons()

        self._build_ui()

        self.udp_thread = UdpListenerThread()
        self.udp_thread.weather_received.connect(self.on_weather)
        self.udp_thread.error.connect(self.on_udp_error)
        self.udp_thread.start()

        self.register_city(self.city)

    def _build_ui(self):
        layout = QtWidgets.QVBoxLayout()

        # Header
        header = QtWidgets.QHBoxLayout()
        self.city_label = QtWidgets.QLabel(f"City: {self.city}")
        header.addWidget(self.city_label)
        header.addStretch()
        self.status_label = QtWidgets.QLabel("Status: Not registered")
        header.addWidget(self.status_label)
        layout.addLayout(header)

        # Weather
        mid = QtWidgets.QHBoxLayout()
        self.icon_label = QtWidgets.QLabel()
        self.icon_label.setFixedSize(120,120)
        mid.addWidget(self.icon_label)

        stats = QtWidgets.QVBoxLayout()
        self.temp_label = QtWidgets.QLabel("Temp: -- °C")
        stats.addWidget(self.temp_label)
        self.hum_label = QtWidgets.QLabel("Humidity: -- %")
        stats.addWidget(self.hum_label)
        self.wind_label = QtWidgets.QLabel("Wind: -- km/h")
        stats.addWidget(self.wind_label)
        stats.addStretch()
        mid.addLayout(stats)
        layout.addLayout(mid)

        # Buttons
        btns = QtWidgets.QHBoxLayout()
        self.change_city_btn = QtWidgets.QPushButton("Change City")
        self.change_city_btn.clicked.connect(self.change_city)
        btns.addWidget(self.change_city_btn)

        self.add_fav_btn = QtWidgets.QPushButton("Add Favorite")
        self.add_fav_btn.clicked.connect(self.add_favorite)
        btns.addWidget(self.add_fav_btn)

        self.show_fav_btn = QtWidgets.QPushButton("Show Favorites")
        self.show_fav_btn.clicked.connect(self.show_favorites)
        btns.addWidget(self.show_fav_btn)

        self.disconnect_btn = QtWidgets.QPushButton("Disconnect")
        self.disconnect_btn.clicked.connect(self.disconnect)
        btns.addWidget(self.disconnect_btn)
        btns.addStretch()
        layout.addLayout(btns)

        self.setLayout(layout)
        self.set_condition_icon('Cloudy')

    def load_icons(self):
        for key,path in self.icons.items():
            p = Path(path)
            if p.exists():
                pm = QtGui.QPixmap(str(p))
                if not pm.isNull():
                    self.pixmaps[key] = pm

    def set_condition_icon(self, cond):
        pm = self.pixmaps.get(cond)
        if pm:
            self.icon_label.setPixmap(pm.scaled(self.icon_label.width(), self.icon_label.height(),
                                                QtCore.Qt.AspectRatioMode.KeepAspectRatio))
        else:
            pix = QtGui.QPixmap(self.icon_label.size())
            pix.fill(QtGui.QColor('white'))
            self.icon_label.setPixmap(pix)

    # ---------------- Weather Update ----------------
    def on_weather(self, data):
        if 'CITY' in data and data['CITY'] != self.city: return
        self.temp_label.setText(f"Temp: {data.get('TEMP','--')} °C")
        self.hum_label.setText(f"Humidity: {data.get('HUM','--')} %")
        self.wind_label.setText(f"Wind: {data.get('WIND','--')} km/h")
        self.set_condition_icon(self.normalize_cond(data.get('ICON','cloud')))
        self.status_label.setText("Status: Receiving UDP updates")

    def normalize_cond(self, c):
        c = c.lower()
        if 'sun' in c: return 'Sunny'
        if 'rain' in c: return 'Rain'
        if 'snow' in c: return 'Snow'
        return 'Cloudy'

    def on_udp_error(self, msg): self.status_label.setText(f"UDP error: {msg}")

    # ---------------- TCP Actions ----------------
    def register_city(self, city):
        msg = f"SETCITY:{self.client_id}:{city}"
        resp = send_tcp_command(msg)
        self.status_label.setText(resp)

    def add_favorite(self):
        city, ok = QtWidgets.QInputDialog.getItem(self, "Add Favorite", "Select city:", VALID_CITIES, 0, False)
        if ok and city:
            msg = f"ADDFAV:{self.client_id}:{city}"
            resp = send_tcp_command(msg)
            QtWidgets.QMessageBox.information(self, "Add Favorite", resp)

    def show_favorites(self):
        msg = f"GETFAV:{self.client_id}"
        resp = send_tcp_command(msg)
        QtWidgets.QMessageBox.information(self, "Your Favorites", resp)

    def change_city(self):
        city, ok = QtWidgets.QInputDialog.getItem(self, "Change City", "Select city:", VALID_CITIES, VALID_CITIES.index(self.city), False)
        if ok and city:
            self.city = city
            self.city_label.setText(f"City: {self.city}")
            self.register_city(city)

    def disconnect(self):
        try: self.udp_thread.stop()
        except: pass
        self.status_label.setText("Disconnected")

    def closeEvent(self, event):
        try: self.udp_thread.stop()
        except: pass
        event.accept()

# ----------------------------- Main -----------------------------
def main():
    app = QtWidgets.QApplication([])
    w = WeatherWindow()
    w.show()
    app.exec()

if __name__ == "__main__":
    main()
