import sqlite3
from config import DB_NAME

class DatabaseManager:
    def __init__(self):
        self.conn = sqlite3.connect(DB_NAME, check_same_thread=False)
        self.create_tables()

    def get_cursor(self):
        return self.conn.cursor()

    def commit(self):
        self.conn.commit()

    def create_tables(self):
        c = self.conn.cursor()
        c.execute('''CREATE TABLE IF NOT EXISTS pacientes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            nombre TEXT, edad INTEGER, dni TEXT, sexo TEXT,
            fecha_registro TEXT, id_centro INTEGER
        )''')

        c.execute('''CREATE TABLE IF NOT EXISTS mediciones (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            id_paciente INTEGER, id_medicion_24h INTEGER,
            fecha TEXT, bpm REAL, spo2 REAL, resp REAL, temp REAL,
            sbp REAL, dbp REAL, num_muestras INTEGER,
            FOREIGN KEY(id_paciente) REFERENCES pacientes(id)
        )''')

        try:
            c.execute("ALTER TABLE mediciones ADD COLUMN id_medicion_24h INTEGER")
        except sqlite3.OperationalError:
            pass

        c.execute('''CREATE TABLE IF NOT EXISTS centros_medicos (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            nombre TEXT, direccion TEXT
        )''')

        c.execute('''CREATE TABLE IF NOT EXISTS configuraciones_medicion (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            id_paciente INTEGER, mediciones_por_hora INTEGER,
            total_tramas INTEGER, fecha_inicio TEXT,
            en_espera INTEGER DEFAULT 1,
            FOREIGN KEY(id_paciente) REFERENCES pacientes(id)
        )''')
        self.conn.commit()