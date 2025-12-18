import customtkinter as ctk
from database import DatabaseManager
from ble_manager import BLEManager
from gui import WearableApp

if __name__ == "__main__":
    ctk.set_appearance_mode("dark")
    
    db = DatabaseManager()
    
    ble = BLEManager(db)
    
    app = WearableApp(db, ble)
    app.mainloop()