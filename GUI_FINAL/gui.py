import customtkinter as ctk
import threading
from datetime import datetime
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from reports import exportar_pdf
from config import DB_NAME

class WearableApp(ctk.CTk):
    def __init__(self, db_manager, ble_manager):
        super().__init__()
        self.db = db_manager
        self.ble = ble_manager
        
        self.title("Heartyx - PE2")
        self.geometry("900x600")
        
        self.main_frame = ctk.CTkFrame(self)
        self.main_frame.pack(fill="both", expand=True, padx=20, pady=20)
        
        self.status_label = None
        self.mostrar_menu_principal()

    def clear_frame(self):
        for widget in self.main_frame.winfo_children():
            widget.destroy()
        self.add_status_bar()

    def add_status_bar(self):
        top = ctk.CTkFrame(self.main_frame, fg_color="transparent")
        top.pack(anchor="ne", fill="x", pady=5)
        
        self.status_label = ctk.CTkLabel(top, text="Desconectado", text_color="red")
        self.status_label.pack(side="left", padx=10)
        self.update_status_visual()
        
        ctk.CTkButton(top, text="Conectar", width=80, command=self.conectar).pack(side="right", padx=5)
        ctk.CTkButton(top, text="Desconectar", width=80, command=self.desconectar).pack(side="right")

    def update_status_visual(self):
        if self.status_label:
            if self.ble.connected:
                self.status_label.configure(text="🟢 Conectado", text_color="green")
            else:
                self.status_label.configure(text="🔴 Desconectado", text_color="red")

    def conectar(self):
        def task():
            self.status_label.configure(text="Conectando...", text_color="yellow")
            ok = self.ble.run_async(self.ble.connect())
            self.update_status_visual()
        threading.Thread(target=task, daemon=True).start()

    def desconectar(self):
        def task():
            self.ble.run_async(self.ble.disconnect())
            self.update_status_visual()
        threading.Thread(target=task, daemon=True).start()

    def mostrar_menu_principal(self):
        self.clear_frame()
        ctk.CTkLabel(self.main_frame, text="Menú Principal", font=("Arial", 22, "bold")).pack(pady=20)
        ctk.CTkButton(self.main_frame, text="Registrar Paciente", command=self.vista_registro).pack(pady=10)
        ctk.CTkButton(self.main_frame, text="Buscar Paciente", command=self.vista_busqueda).pack(pady=10)
        ctk.CTkButton(self.main_frame, text="Agregar Centro Médico", command=self.vista_centro).pack(pady=10)

    def vista_registro(self):
        self.clear_frame()
        ctk.CTkLabel(self.main_frame, text="Nuevo Paciente", font=("Arial", 20)).pack(pady=10)
        
        nombre = ctk.CTkEntry(self.main_frame, placeholder_text="Nombre")
        nombre.pack(pady=5)
        dni = ctk.CTkEntry(self.main_frame, placeholder_text="DNI")
        dni.pack(pady=5)
        edad = ctk.CTkEntry(self.main_frame, placeholder_text="Edad")
        edad.pack(pady=5)
        sexo = ctk.CTkComboBox(self.main_frame, values=["M", "F"])
        sexo.pack(pady=5)
        
        c = self.db.get_cursor()
        c.execute("SELECT nombre FROM centros_medicos")
        centros = [r[0] for r in c.fetchall()] or ["(Ninguno)"]
        centro_cb = ctk.CTkComboBox(self.main_frame, values=centros)
        centro_cb.pack(pady=5)

        def guardar():
            c.execute("SELECT id FROM centros_medicos WHERE nombre=?", (centro_cb.get(),))
            cid = c.fetchone()
            cid = cid[0] if cid else None
            c.execute("INSERT INTO pacientes (nombre, edad, dni, sexo, fecha_registro, id_centro) VALUES (?,?,?,?,datetime('now'),?)",
                      (nombre.get(), edad.get(), dni.get(), sexo.get(), cid))
            self.db.commit()
            self.mostrar_menu_principal()

        ctk.CTkButton(self.main_frame, text="Guardar", command=guardar).pack(pady=20)
        ctk.CTkButton(self.main_frame, text="Volver", command=self.mostrar_menu_principal).pack()

    def vista_busqueda(self):
        self.clear_frame()
        ctk.CTkLabel(self.main_frame, text="Buscar por DNI", font=("Arial", 20)).pack(pady=10)
        dni_entry = ctk.CTkEntry(self.main_frame)
        dni_entry.pack(pady=10)
        
        def buscar():
            c = self.db.get_cursor()
            c.execute("SELECT * FROM pacientes WHERE dni=?", (dni_entry.get(),))
            row = c.fetchone()
            if row: self.vista_paciente(row)
            else: ctk.CTkLabel(self.main_frame, text="No encontrado", text_color="red").pack()

        ctk.CTkButton(self.main_frame, text="Buscar", command=buscar).pack(pady=10)
        ctk.CTkButton(self.main_frame, text="Volver", command=self.mostrar_menu_principal).pack()

    def vista_paciente(self, paciente):
        self.clear_frame()
        pid, nom, edad, dni, sex, _, _ = paciente
        ctk.CTkLabel(self.main_frame, text=f"{nom} ({edad} años)", font=("Arial", 20, "bold")).pack(pady=10)
        
        c = self.db.get_cursor()
        c.execute("SELECT DISTINCT id_medicion_24h, MIN(fecha) FROM mediciones WHERE id_paciente=? GROUP BY id_medicion_24h ORDER BY id_medicion_24h DESC", (pid,))
        bloques = c.fetchall()
        
        scroll = ctk.CTkScrollableFrame(self.main_frame, height=200)
        scroll.pack(fill="x", padx=20)
        
        for bid, fecha in bloques:
            if bid is None: continue
            btn_txt = f"Ver medición del {fecha}"
            ctk.CTkButton(scroll, text=btn_txt, command=lambda b=bid: self.ver_graficas(pid, b)).pack(pady=2)

        c.execute("SELECT mediciones_por_hora, total_tramas FROM configuraciones_medicion WHERE id_paciente=? AND en_espera=1 ORDER BY id DESC LIMIT 1", (pid,))
        cfg = c.fetchone()
        
        if cfg:
            ctk.CTkLabel(self.main_frame, text=f"Medición pendiente: {cfg[1]} tramas", text_color="yellow").pack(pady=10)
            ctk.CTkButton(self.main_frame, text="▶ DESCARGAR DATOS", fg_color="green", 
                          command=lambda: self.iniciar_descarga(pid)).pack(pady=5)
        else:
            ctk.CTkButton(self.main_frame, text="+ Nueva Medición 24h", command=lambda: self.vista_configurar(pid)).pack(pady=10)

        ctk.CTkButton(self.main_frame, text="Volver", command=self.vista_busqueda).pack(pady=10)

    def vista_configurar(self, pid):
        self.clear_frame()
        ctk.CTkLabel(self.main_frame, text="Configurar Medición", font=("Arial", 20)).pack(pady=10)
        entry = ctk.CTkEntry(self.main_frame, placeholder_text="Muestras/hora (1-60)")
        entry.pack(pady=10)
        msg = ctk.CTkLabel(self.main_frame, text="")
        msg.pack()

        def enviar():
            try:
                v = int(entry.get())
                total = v * 24
                if self.ble.run_async(self.ble.send_config(total)):
                    c = self.db.get_cursor()
                    c.execute("INSERT INTO configuraciones_medicion (id_paciente, mediciones_por_hora, total_tramas, fecha_inicio, en_espera) VALUES (?,?,?,datetime('now'),1)", (pid, v, total))
                    self.db.commit()
                    self.ble.run_async(self.ble.disconnect()) 
                    self.update_status_visual()
                    self.vista_busqueda() 
                else:
                    msg.configure(text="Error BLE", text_color="red")
            except: msg.configure(text="Valor inválido", text_color="red")

        ctk.CTkButton(self.main_frame, text="Enviar", command=enviar).pack(pady=10)
        ctk.CTkButton(self.main_frame, text="Cancelar", command=lambda: self.vista_busqueda()).pack()

    def iniciar_descarga(self, pid):
        c = self.db.get_cursor()
        c.execute("SELECT total_tramas FROM configuraciones_medicion WHERE id_paciente=? AND en_espera=1 ORDER BY id DESC LIMIT 1", (pid,))
        row = c.fetchone()
        total = row[0] if row else 72
        
        lbl = ctk.CTkLabel(self.main_frame, text="Descargando...", text_color="orange")
        lbl.pack()

        def task():
            if self.ble.run_async(self.ble.request_download(total)):
                c.execute("UPDATE configuraciones_medicion SET en_espera=0 WHERE id_paciente=? AND en_espera=1", (pid,))
                c.execute("SELECT MAX(id) FROM configuraciones_medicion")
                id24 = c.fetchone()[0]
                
                count = self.ble.process_and_save(pid, id24)
                lbl.configure(text=f"Completado. {count} guardados.", text_color="green")
            else:
                lbl.configure(text="Error descarga", text_color="red")
        
        threading.Thread(target=task, daemon=True).start()

    def vista_centro(self):
        self.clear_frame()
        ctk.CTkLabel(self.main_frame, text="Nuevo Centro", font=("Arial", 20)).pack(pady=10)
        nom = ctk.CTkEntry(self.main_frame, placeholder_text="Nombre")
        nom.pack(pady=5)
        dir = ctk.CTkEntry(self.main_frame, placeholder_text="Dirección")
        dir.pack(pady=5)
        
        def guardar():
            self.db.get_cursor().execute("INSERT INTO centros_medicos (nombre, direccion) VALUES (?,?)", (nom.get(), dir.get()))
            self.db.commit()
            self.mostrar_menu_principal()
            
        ctk.CTkButton(self.main_frame, text="Guardar", command=guardar).pack(pady=10)
        ctk.CTkButton(self.main_frame, text="Volver", command=self.mostrar_menu_principal).pack()

    def ver_graficas(self, pid, id24):
        win = ctk.CTkToplevel(self)
        win.geometry("900x700")
        
        c = self.db.get_cursor()
        c.execute("SELECT bpm, spo2, resp FROM mediciones WHERE id_paciente=? AND id_medicion_24h=? ORDER BY id", (pid, id24))
        data = c.fetchall()
        
        if not data: return
        bpm = [x[0] for x in data]
        
        fig = Figure(figsize=(7, 4), dpi=100)
        ax = fig.add_subplot(111)
        ax.plot(bpm, label="BPM")
        ax.legend()
        
        canvas = FigureCanvasTkAgg(fig, master=win)
        canvas.draw()
        canvas.get_tk_widget().pack(fill="both", expand=True)
        
        ctk.CTkButton(win, text="Exportar PDF", command=lambda: exportar_pdf(pid, id24, DB_NAME)).pack(pady=10)