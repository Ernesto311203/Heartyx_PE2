import io
import sqlite3
import numpy as np
import matplotlib
matplotlib.use("Agg")
from matplotlib.figure import Figure
from reportlab.lib.pagesizes import A4
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet
from config import LOGO_PATH

def exportar_pdf(pid, id24, db_path):
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    
    c.execute("SELECT nombre, edad, dni, sexo FROM pacientes WHERE id=?", (pid,))
    pat = c.fetchone()
    if not pat: return
    nombre, edad, dni, sexo = pat

    c.execute("""SELECT bpm, spo2, resp, temp, sbp, dbp FROM mediciones
                 WHERE id_paciente=? AND id_medicion_24h=? ORDER BY id""", (pid, id24))
    data = c.fetchall()
    conn.close()
    
    if not data: return

    cols = list(zip(*data))
    bpm, spo2, rr, temp, sbp, dbp = cols

    filename = f"Reporte_24h_{dni}_M{id24}.pdf"
    doc = SimpleDocTemplate(filename, pagesize=A4, topMargin=40)
    styles = getSampleStyleSheet()
    story = []

    try: story.append(Image(LOGO_PATH, width=120, height=80))
    except: pass
    
    story.append(Spacer(1, 20))
    story.append(Paragraph("<b>REPORTE MONITOREO 24 HORAS</b>", styles["Title"]))
    
    info = [["Nombre:", nombre], ["DNI:", dni], ["Edad:", str(edad)], ["Sexo:", sexo]]
    t = Table(info, colWidths=[100, 300])
    t.setStyle(TableStyle([('BOX', (0,0), (-1,-1), 1, colors.black),
                           ('BACKGROUND', (0,0), (0,-1), colors.lightgrey)]))
    story.append(t)
    story.append(Spacer(1, 20))

    def add_plot(title, y, ylab):
        fig = Figure(figsize=(6, 2.5), dpi=100)
        ax = fig.add_subplot(111)
        ax.plot(y, "-o", markersize=3)
        ax.set_title(title)
        ax.set_ylabel(ylab)
        ax.grid(True)
        buf = io.BytesIO()
        fig.savefig(buf, format="png", bbox_inches="tight")
        buf.seek(0)
        story.append(Image(buf, width=450, height=180))
        story.append(Spacer(1, 15))

    add_plot("Frecuencia Cardiaca (BPM)", bpm, "BPM")
    add_plot("SpO2 (%)", spo2, "%")
    add_plot("Presión Arterial (SBP/DBP)", sbp, "mmHg")
    
    doc.build(story)
    print(f"PDF generado: {filename}")