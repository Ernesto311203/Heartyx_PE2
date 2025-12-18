import asyncio
import threading
import time
import struct
import numpy as np
from bleak import BleakScanner, BleakClient
from config import DEVICE_NAME, TX_CHAR_UUID, RX_CHAR_UUID
from signal_processing import process_single_sequence

class BLEManager:
    def __init__(self, db_manager):
        self.db = db_manager
        self.client = None
        self.connected = False
        self.loop = asyncio.new_event_loop()
        self.session_buffer = {}
        self.seqs_recibidas = 0
        self.expected_sequences = 72
        self.last_packet_time = None
        self.data_complete_event = asyncio.Event()
        
        t = threading.Thread(target=self._start_loop, daemon=True)
        t.start()

    def _start_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def run_async(self, coro):
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result()

    async def connect(self):
        print(f"Buscando {DEVICE_NAME}...")
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: (d.name or "") == DEVICE_NAME, timeout=10
        )
        if not device: return False
        
        self.client = BleakClient(device, timeout=30)
        try:
            await self.client.connect()
            self.connected = True
            await self.client.start_notify(TX_CHAR_UUID, self.notification_handler)
            return True
        except Exception as e:
            print(f"Error connect: {e}")
            self.connected = False
            return False

    async def disconnect(self):
        if self.client and self.client.is_connected:
            await self.client.disconnect()
        self.connected = False

    async def send_config(self, num_sequences):
        if not self.connected: return False
        try:
            payload = bytes([0x01]) + int(num_sequences).to_bytes(4, "little")
            char = self.client.services.get_characteristic(RX_CHAR_UUID)
            resp = "write" in char.properties
            await self.client.write_gatt_char(RX_CHAR_UUID, payload, response=resp)
            self.expected_sequences = num_sequences
            return True
        except Exception as e:
            print(f"Error config: {e}")
            return False

    async def request_download(self, expected):
        self.expected_sequences = expected
        self.session_buffer = {}
        self.seqs_recibidas = 0
        self.data_complete_event.clear()
        self.last_packet_time = time.time()
        
        if not self.connected: return False
        try:
            await self.client.write_gatt_char(RX_CHAR_UUID, bytes([0x02]), response=True)
        except: return False

        start = time.time()
        while not self.data_complete_event.is_set():
            if time.time() - start > 180: break 
            if time.time() - self.last_packet_time > 20: break 
            await asyncio.sleep(0.2)
            
        valid = sum(1 for d in self.session_buffer.values() 
                    if len(d["ppg1"])>=4096 and len(d["ppg2"])>=4096)
        return valid > 0

    def notification_handler(self, data):
        if len(data) < 8: return
        kind = data[0]
        seq = int.from_bytes(data[2:4], "little")
        payload = data[8:]
        self.last_packet_time = time.time()

        if seq not in self.session_buffer:
            self.session_buffer[seq] = {"ppg1": bytearray(), "ppg2": bytearray(), "temp": None}
        
        entry = self.session_buffer[seq]
        if kind == 1: entry["ppg1"].extend(payload)
        elif kind == 2: entry["ppg2"].extend(payload)
        elif kind == 3 and len(payload)>=4:
            entry["temp"] = struct.unpack("<f", payload[:4])[0]
        elif kind == 0:
            self.seqs_recibidas += 1
            print(f"Seq {seq} OK ({self.seqs_recibidas}/{self.expected_sequences})")
            if self.seqs_recibidas >= self.expected_sequences:
                self.loop.call_soon_threadsafe(self.data_complete_event.set)

    def process_and_save(self, pid, id_24h):
        print(f"Procesando {len(self.session_buffer)} secuencias...")
        count = 0
        c = self.db.get_cursor()
        
        for seq, d in self.session_buffer.items():
            ppg1 = np.frombuffer(d["ppg1"], dtype="<u4").astype(np.int32)
            ppg2 = np.frombuffer(d["ppg2"], dtype="<u4").astype(np.int32)
            temp = d["temp"]
            
            if len(ppg1) < 1024 or temp is None: continue
            
            res = process_single_sequence(ppg1, ppg2, temp)
            
            c.execute("""INSERT INTO mediciones 
                (id_paciente, id_medicion_24h, fecha, bpm, spo2, resp, temp, sbp, dbp, num_muestras)
                VALUES (?, ?, datetime('now'), ?, ?, ?, ?, ?, ?, 1)""",
                (pid, id_24h, res["hr"], res["spo2"], res["rr"], res["temp"], res["sbp"], res["dbp"]))
            count += 1
            
        self.db.commit()
        return count