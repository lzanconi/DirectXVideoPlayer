import socket
import json
import threading
import tkinter as tk

# --- Configuration ---
PORT = 5555
HOST = '127.0.0.1'

# --- UI Constants ---
WINDOW_WIDTH = 1050
WINDOW_HEIGHT = 150
CANVAS_WIDTH = 1000
TRACK_MARGIN = 50
TRACK_LENGTH = CANVAS_WIDTH - (2 * TRACK_MARGIN)  # 900px of travel

# Physical data scaling
MIN_VAL = 0.0
MAX_VAL = 2900.0

# Global state and thread safety
current_position = 0.0
pos_lock = threading.Lock()

def tcp_server_thread():
    global current_position
    
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # Lowers latency by sending small packets immediately
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.bind((HOST, PORT))
        s.listen()
        print(f"[*] Python Position Server listening on {HOST}:{PORT}")

        while True:
            conn, addr = s.accept()
            with conn:
                print(f"[+] Connected to C++ Client: {addr}")
                buffer = ""
                
                while True:
                    try:
                        data = conn.recv(1024)
                        if not data:
                            print("[-] Client disconnected.")
                            break
                        
                        buffer += data.decode('utf-8')
                        
                        while '\0' in buffer:
                            msg, buffer = buffer.split('\0', 1)
                            if msg.strip():
                                try:
                                    parsed_json = json.loads(msg)
                                    positions = parsed_json.get('positions', [])
                                    if positions:
                                        with pos_lock:
                                            current_position = float(positions[0])
                                except (json.JSONDecodeError, ValueError) as e:
                                    print(f"[!] Parse error: {e}")
                    except ConnectionResetError:
                        break

def update_ui():
    """Reads the global current_position and updates the UI cursor."""
    global current_position
    
    # Thread-safe read of the current position
    with pos_lock:
        local_pos = current_position
    
    # Map the physical value to the 1000px canvas
    clamped_pos = max(MIN_VAL, min(MAX_VAL, local_pos))
    percentage = (clamped_pos - MIN_VAL) / (MAX_VAL - MIN_VAL)
    
    # X coordinate = Left Margin + (Percentage * available track length)
    x_coord = TRACK_MARGIN + (percentage * TRACK_LENGTH)
    
    # Update cursor position (Red circle)
    canvas.coords(cursor, x_coord - 10, 40, x_coord + 10, 60)
    
    # Update text label
    pos_label.config(text=f"Current Position: {local_pos:.4f}")
    
    # Schedule next update (~60 FPS)
    root.after(16, update_ui)

# --- GUI Setup ---
root = tk.Tk()
root.title("C++ Position Tracker (1000px)")
root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")

# Text Label
pos_label = tk.Label(root, text="Current Position: 0.0000", font=("Arial", 14))
pos_label.pack(pady=10)

# Drawing Canvas
canvas = tk.Canvas(root, width=CANVAS_WIDTH, height=100, bg="#222222", highlightthickness=0)
canvas.pack()

# Draw the track line based on our margins
canvas.create_line(TRACK_MARGIN, 50, CANVAS_WIDTH - TRACK_MARGIN, 50, fill="gray", width=4)

# Draw the moving cursor
cursor = canvas.create_oval(0, 0, 0, 0, fill="red", outline="white", width=2)

# Start Networking Thread
server_thread = threading.Thread(target=tcp_server_thread, daemon=True)
server_thread.start()

# Start UI loop
update_ui()
root.mainloop()